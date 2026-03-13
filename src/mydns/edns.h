/**************************************************************************************************
	edns.h: EDNS0 (Extension mechanisms for DNS) support per RFC 6891.

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

#ifndef _EDNS_H
#define _EDNS_H

#include <stdint.h>
#include <stddef.h>

/* EDNS0 constants per RFC 6891 */
#define EDNS_VERSION			0		/* Current EDNS version */
#define EDNS_DEFAULT_UDP_SIZE		4096		/* Default maximum UDP payload size */
#define EDNS_MIN_UDP_SIZE		512		/* Minimum UDP payload size */
#define EDNS_MAX_UDP_SIZE		65535		/* Maximum possible UDP payload size */

/* OPT pseudo-RR type (already defined as DNS_QTYPE_OPT = 41 in mydns.h) */
#define EDNS_OPT_RR_TYPE		41

/* EDNS flags (upper 16 bits of TTL field) */
#define EDNS_FLAG_DO			0x8000		/* DNSSEC OK bit */

/* OPT record fixed size: 1 byte name (root) + 2 bytes type + 2 bytes class + 4 bytes TTL + 2 bytes rdlen */
#define EDNS_OPT_RR_FIXED_SIZE		11

/*
 * EDNS0 OPT pseudo-RR structure (RFC 6891 Section 6.1.2)
 *
 * The OPT pseudo-RR format:
 *   NAME          = 0 (root domain, single zero byte)
 *   TYPE          = OPT (41)
 *   CLASS         = requestor's UDP payload size
 *   TTL           = extended RCODE (8 bits) | version (8 bits) | DO (1 bit) | Z (15 bits)
 *   RDLENGTH      = length of RDATA
 *   RDATA         = {attribute, value} pairs (options)
 */
typedef struct _edns_opt {
	uint16_t	udp_size;		/* UDP payload size (from CLASS field) */
	uint8_t		ext_rcode;		/* Extended RCODE (upper 8 bits of TTL) */
	uint8_t		version;		/* EDNS version (bits 16-23 of TTL) */
	uint16_t	flags;			/* EDNS flags including DO bit (lower 16 bits of TTL) */
	uint16_t	rdlen;			/* RDATA length */
	unsigned char	*rdata;			/* RDATA (options, may be NULL) */
} EDNS_OPT;

/* EDNS data stored in TASK structure */
typedef struct _edns_data {
	int		has_edns;		/* Client sent EDNS0 OPT record */
	uint8_t		version;		/* EDNS version from client */
	uint16_t	udp_size;		/* Client's advertised UDP payload size */
	uint16_t	flags;			/* EDNS flags from client (includes DO bit) */
	uint8_t		ext_rcode;		/* Extended RCODE for response */
} EDNS_DATA;

/* Macro to check DO (DNSSEC OK) bit */
#define EDNS_DO_BIT(flags)		(((flags) & EDNS_FLAG_DO) != 0)

/* Global EDNS0 configuration */
extern int		edns_enabled;		/* Is EDNS0 enabled? */
extern uint16_t		edns_max_udp_size;	/* Server's maximum UDP payload size */

/* API functions */
extern int	edns_init(void);
extern void	edns_cleanup(void);
extern int	edns_parse(unsigned char *data, size_t len, size_t offset, uint16_t arcount,
			   EDNS_DATA *edns);
extern int	edns_build_opt(unsigned char *buf, size_t buflen, EDNS_DATA *edns,
			       uint8_t rcode);
extern size_t	edns_get_max_response_size(EDNS_DATA *edns, int is_tcp);

#endif /* _EDNS_H */

/* vi:set ts=3: */
