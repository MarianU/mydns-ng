/**************************************************************************************************
	edns.c: EDNS0 (Extension mechanisms for DNS) support per RFC 6891.

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at Your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**************************************************************************************************/

#include "named.h"
#include "edns.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_EDNS	1

/* Global EDNS0 configuration - defined in conf.c */
extern int	edns_enabled;			/* EDNS0 enabled by default */
extern uint16_t	edns_max_udp_size;		/* Server's max UDP payload size */


/**************************************************************************************************
	EDNS_INIT
	Initialize the EDNS0 subsystem.
**************************************************************************************************/
int
edns_init(void) {
	if (!edns_enabled) {
		return 0;
	}

#if DEBUG_ENABLED && DEBUG_EDNS
	DebugX("edns", 1, _("EDNS0 initialized: max-udp-size=%u"),
		edns_max_udp_size);
#endif

	Verbose(_("EDNS0 enabled: max-udp-size=%u"), edns_max_udp_size);

	return 0;
}
/*--- edns_init() -------------------------------------------------------------------------------*/


/**************************************************************************************************
	EDNS_CLEANUP
	Clean up the EDNS0 subsystem.
**************************************************************************************************/
void
edns_cleanup(void) {
	/* Nothing to clean up currently */
}
/*--- edns_cleanup() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	EDNS_PARSE
	Parse OPT record from the additional section of an incoming query.
	
	Parameters:
	  data    - Pointer to the start of the DNS message
	  len     - Total length of the DNS message
	  offset  - Offset to start of additional section (after question section)
	  arcount - Number of records in additional section
	  edns    - Output: EDNS data structure to populate

	Returns:
	  0 on success (even if no EDNS0 present)
	  -1 on error (malformed OPT record)
	
	Per RFC 6891:
	  - OPT record NAME must be 0 (root)
	  - OPT record TYPE must be 41
	  - There must be at most one OPT record in a message
	  - OPT must not appear in question, answer, or authority sections
**************************************************************************************************/
int
edns_parse(unsigned char *data, size_t len, size_t offset, uint16_t arcount,
	   EDNS_DATA *edns) {
	unsigned char *src;
	uint16_t i;
	uint16_t rr_type, rr_class, rdlen;
	uint32_t ttl;
	int opt_found = 0;

	/* Initialize EDNS data to defaults (no EDNS) */
	edns->has_edns = 0;
	edns->version = 0;
	edns->udp_size = DNS_MAXPACKETLEN_UDP;	/* Default to 512 if no EDNS */
	edns->flags = 0;
	edns->ext_rcode = 0;

	/* If EDNS is disabled or no additional records, nothing to parse */
	if (!edns_enabled || arcount == 0) {
		return 0;
	}

	/* Position at start of additional section */
	src = data + offset;

	/* Scan through additional section looking for OPT record */
	for (i = 0; i < arcount; i++) {
		uint8_t name_byte;

		/* Check we have at least one byte for name */
		if (src >= data + len) {
			return -1;	/* Truncated */
		}

		/* Read first byte of name */
		name_byte = *src;

		/* Check if this could be an OPT record (name must be root = 0) */
		if (name_byte == 0) {
			/* Name is root (single zero byte), check if this is OPT */
			src++;	/* Skip the zero byte */

			/* Need at least: type(2) + class(2) + ttl(4) + rdlen(2) = 10 bytes */
			if (src + 10 > data + len) {
				return -1;	/* Truncated */
			}

			/* Read TYPE */
			DNS_GET16(rr_type, src);

			if (rr_type == DNS_QTYPE_OPT) {
				/* Found OPT record */
				if (opt_found) {
					/* RFC 6891: only one OPT allowed per message */
#if DEBUG_ENABLED && DEBUG_EDNS
					DebugX("edns", 1, _("Multiple OPT records in message"));
#endif
					return -1;
				}
				opt_found = 1;

				/* CLASS = requestor's UDP payload size */
				DNS_GET16(rr_class, src);
				edns->udp_size = rr_class;

				/* Enforce minimum UDP size */
				if (edns->udp_size < EDNS_MIN_UDP_SIZE) {
					edns->udp_size = EDNS_MIN_UDP_SIZE;
				}

				/* TTL field contains: ext-rcode(8) | version(8) | flags(16) */
				DNS_GET32(ttl, src);
				edns->ext_rcode = (ttl >> 24) & 0xFF;
				edns->version = (ttl >> 16) & 0xFF;
				edns->flags = ttl & 0xFFFF;

				/* RDLENGTH */
				DNS_GET16(rdlen, src);

				/* Skip RDATA (options) - we don't process options yet */
				if (src + rdlen > data + len) {
					return -1;	/* Truncated */
				}
				src += rdlen;

				edns->has_edns = 1;

#if DEBUG_ENABLED && DEBUG_EDNS
				DebugX("edns", 1, _("EDNS0 parsed: version=%u, udp_size=%u, flags=0x%04x, DO=%d"),
					edns->version, edns->udp_size, edns->flags,
					EDNS_DO_BIT(edns->flags) ? 1 : 0);
#endif

				/* Check EDNS version */
				if (edns->version > EDNS_VERSION) {
					/* We only support version 0 */
					/* The response will include BADVERS */
#if DEBUG_ENABLED && DEBUG_EDNS
					DebugX("edns", 1, _("EDNS0 unsupported version %u"), edns->version);
#endif
				}
			} else {
				/* Not OPT, need to skip this RR */
				src -= 2;	/* Back up past type */
				src--;		/* Back up past name */
				goto skip_rr;
			}
		} else {
skip_rr:
			/* Skip this RR - need to parse past name first */
			/* Handle compressed names and regular names */
			while (src < data + len) {
				name_byte = *src;
				if (name_byte == 0) {
					src++;	/* Skip terminating zero */
					break;
				} else if ((name_byte & DNS_POINTER_MASK) == DNS_POINTER_MASK) {
					src += 2;	/* Skip pointer (2 bytes) */
					break;
				} else {
					src += 1 + name_byte;	/* Skip label */
				}
			}

			/* Now skip: type(2) + class(2) + ttl(4) + rdlen(2) + rdata */
			if (src + 10 > data + len) {
				return -1;	/* Truncated */
			}
			src += 2;	/* type */
			src += 2;	/* class */
			src += 4;	/* ttl */
			DNS_GET16(rdlen, src);
			if (src + rdlen > data + len) {
				return -1;	/* Truncated */
			}
			src += rdlen;
		}
	}

	return 0;
}
/*--- edns_parse() ------------------------------------------------------------------------------*/


/**************************************************************************************************
	EDNS_BUILD_OPT
	Build an OPT record for the response.
	
	Parameters:
	  buf     - Buffer to write OPT record into
	  buflen  - Size of buffer
	  edns    - EDNS data from the request (used to know if client sent EDNS)
	  rcode   - The RCODE for this response (upper 8 bits go in OPT TTL)

	Returns:
	  Number of bytes written to buffer, or 0 if EDNS not applicable
	  -1 on error (buffer too small)

	OPT RR format (RFC 6891 Section 6.1.2):
	  +---+---+---+---+---+---+---+---+---+---+---+
	  |         NAME          | 0 (root domain)   |
	  +---+---+---+---+---+---+---+---+---+---+---+
	  |         TYPE          | OPT (41)          |
	  +---+---+---+---+---+---+---+---+---+---+---+
	  |         CLASS         | UDP payload size  |
	  +---+---+---+---+---+---+---+---+---+---+---+
	  |         TTL           | ext-RCODE|ver|flags
	  +---+---+---+---+---+---+---+---+---+---+---+
	  |       RDLENGTH        | 0 (no options)    |
	  +---+---+---+---+---+---+---+---+---+---+---+
**************************************************************************************************/
int
edns_build_opt(unsigned char *buf, size_t buflen, EDNS_DATA *edns, uint8_t rcode) {
	unsigned char *dest;
	uint32_t ttl;

	/* Only add OPT if EDNS is enabled and client sent EDNS */
	if (!edns_enabled || !edns->has_edns) {
		return 0;
	}

	/* Check buffer size */
	if (buflen < EDNS_OPT_RR_FIXED_SIZE) {
		return -1;
	}

	dest = buf;

	/* NAME = 0 (root domain, single zero byte) */
	*dest++ = 0;

	/* TYPE = OPT (41) */
	DNS_PUT16(dest, DNS_QTYPE_OPT);

	/* CLASS = server's UDP payload size */
	DNS_PUT16(dest, edns_max_udp_size);

	/* TTL = extended-RCODE (8) | version (8) | DO (1) | Z (15)
	 * The extended RCODE is the upper 8 bits of a 12-bit RCODE.
	 * Lower 4 bits are in the DNS header.
	 * For now, we only use 4-bit RCODEs, so ext_rcode is typically 0.
	 */
	ttl = ((uint32_t)edns->ext_rcode << 24) |
	      ((uint32_t)EDNS_VERSION << 16) |
	      (edns->flags & EDNS_FLAG_DO);	/* Echo back DO bit if set */

	DNS_PUT32(dest, ttl);

	/* RDLENGTH = 0 (no options) */
	DNS_PUT16(dest, 0);

#if DEBUG_ENABLED && DEBUG_EDNS
	DebugX("edns", 1, _("EDNS0 OPT built: size=%u, version=%u, flags=0x%04x"),
		edns_max_udp_size, EDNS_VERSION, (edns->flags & EDNS_FLAG_DO));
#endif

	return EDNS_OPT_RR_FIXED_SIZE;
}
/*--- edns_build_opt() --------------------------------------------------------------------------*/


/**************************************************************************************************
	EDNS_GET_MAX_RESPONSE_SIZE
	Get the maximum response size based on EDNS settings.
	
	Parameters:
	  edns   - EDNS data from the request
	  is_tcp - Non-zero if this is a TCP connection

	Returns:
	  Maximum response size in bytes
**************************************************************************************************/
size_t
edns_get_max_response_size(EDNS_DATA *edns, int is_tcp) {
	size_t max_size;

	/* TCP always uses the larger limit */
	if (is_tcp) {
		return DNS_MAXPACKETLEN_TCP;
	}

	/* If EDNS is disabled or client didn't send EDNS, use standard UDP limit */
	if (!edns_enabled || !edns->has_edns) {
		return DNS_MAXPACKETLEN_UDP;
	}

	/* Use the minimum of client's advertised size and server's max */
	max_size = edns->udp_size;
	if (max_size > edns_max_udp_size) {
		max_size = edns_max_udp_size;
	}

	/* Enforce minimum */
	if (max_size < DNS_MAXPACKETLEN_UDP) {
		max_size = DNS_MAXPACKETLEN_UDP;
	}

	return max_size;
}
/*--- edns_get_max_response_size() --------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */
