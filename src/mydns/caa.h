/**************************************************************************************************
	CAA Record Support for MyDNS-NG

	CAA (Certification Authority Authorization) records allow domain owners to specify
	which Certificate Authorities (CAs) are authorized to issue certificates for their
	domain. Defined in RFC 8659.

	Copyright (C) 2024  MyDNS-NG Contributors

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

#ifndef _MYDNS_CAA_H
#define _MYDNS_CAA_H

#include "mydns.h"

/*
 * CAA Record Wire Format (RFC 8659):
 *
 * +0-1-2-3-4-5-6-7-+
 * |     FLAGS      |   1 byte  - Bit 0 is "critical" flag
 * +----------------+
 * |   TAG LENGTH   |   1 byte  - Length of the tag field
 * +----------------+
 * |      TAG       |   variable (tag length bytes) - ASCII tag name
 * +----------------+
 * |     VALUE      |   variable (remaining bytes) - ASCII value
 * +----------------+
 */

/* CAA record flags */
#define CAA_FLAG_CRITICAL	0x80	/* Bit 0 (high bit): critical flag */

/* Maximum lengths */
#define CAA_MAX_TAG_LEN		255	/* Maximum tag length (fits in 1 byte) */
#define CAA_MAX_VALUE_LEN	65535	/* Maximum value length (practical limit) */

/* Standard CAA tag types */
typedef enum {
	CAA_TAG_UNKNOWN = 0,
	CAA_TAG_ISSUE,		/* "issue" - Authorize CA to issue certificates */
	CAA_TAG_ISSUEWILD,	/* "issuewild" - Authorize CA for wildcard certificates */
	CAA_TAG_IODEF		/* "iodef" - Incident Object Description Exchange Format */
} caa_tag_type_t;

/* Parsed CAA record structure */
typedef struct _caa_record {
	uint8_t		flags;		/* CAA flags (bit 0 = critical) */
	uint8_t		tag_len;	/* Length of tag */
	char		*tag;		/* Tag string (e.g., "issue", "issuewild", "iodef") */
	char		*value;		/* Value string (e.g., "letsencrypt.org") */
	uint16_t	value_len;	/* Length of value */
} CAA_RECORD;

/**
 * caa_parse - Parse CAA record from database RDATA field
 * @data: Database data string in format "<flags> <tag> <value>"
 * @datalen: Length of the data string
 * @caa: Output CAA_RECORD structure (caller must free with caa_free)
 *
 * Database format: "<flags> <tag> <quoted_value>"
 * Examples:
 *   "0 issue \"letsencrypt.org\""
 *   "128 issue \"ca.example.com\""
 *   "0 issuewild \";\""
 *   "0 iodef \"mailto:security@example.com\""
 *
 * Returns: 0 on success, -1 on error
 */
extern int caa_parse(const char *data, uint16_t datalen, CAA_RECORD *caa);

/**
 * caa_free - Free CAA_RECORD resources
 * @caa: CAA_RECORD structure to free
 */
extern void caa_free(CAA_RECORD *caa);

/**
 * caa_build_rr - Build CAA record in DNS wire format for responses
 * @caa: Parsed CAA record
 * @dest: Destination buffer (must be large enough)
 * @destlen: Available space in destination buffer
 *
 * Returns: Number of bytes written, or -1 on error
 */
extern int caa_build_rr(const CAA_RECORD *caa, char *dest, size_t destlen);

/**
 * caa_validate - Validate CAA record data format
 * @caa: CAA_RECORD structure to validate
 *
 * Validates:
 * - Tag is a known type or valid unknown tag
 * - Tag length is within bounds
 * - Value is present for known tag types
 * - Flags are valid
 *
 * Returns: 0 if valid, -1 if invalid
 */
extern int caa_validate(const CAA_RECORD *caa);

/**
 * caa_get_tag_type - Get the CAA tag type enum for a tag string
 * @tag: Tag string
 *
 * Returns: caa_tag_type_t enum value
 */
extern caa_tag_type_t caa_get_tag_type(const char *tag);

/**
 * caa_is_critical - Check if the critical flag is set
 * @caa: CAA_RECORD structure
 *
 * Returns: 1 if critical flag is set, 0 otherwise
 */
static inline int caa_is_critical(const CAA_RECORD *caa) {
	return (caa->flags & CAA_FLAG_CRITICAL) != 0;
}

/**
 * caa_wire_size - Calculate the wire format size for a CAA record
 * @caa: CAA_RECORD structure
 *
 * Returns: Size in bytes of the wire format representation
 */
static inline size_t caa_wire_size(const CAA_RECORD *caa) {
	/* flags (1) + tag_len (1) + tag + value */
	return 1 + 1 + caa->tag_len + caa->value_len;
}

#endif /* _MYDNS_CAA_H */

/* vi:set ts=3: */
