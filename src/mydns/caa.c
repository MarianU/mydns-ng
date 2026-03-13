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

#include "named.h"
#include "caa.h"

/* Make this nonzero to enable debugging for this source file */
#define DEBUG_CAA 1

/**************************************************************************************************
	CAA_GET_TAG_TYPE
	Returns the CAA tag type enum for a tag string.
**************************************************************************************************/
caa_tag_type_t
caa_get_tag_type(const char *tag) {
	if (!tag)
		return CAA_TAG_UNKNOWN;

	if (!strcasecmp(tag, "issue"))
		return CAA_TAG_ISSUE;
	if (!strcasecmp(tag, "issuewild"))
		return CAA_TAG_ISSUEWILD;
	if (!strcasecmp(tag, "iodef"))
		return CAA_TAG_IODEF;

	return CAA_TAG_UNKNOWN;
}
/*--- caa_get_tag_type() ------------------------------------------------------------------------*/


/**************************************************************************************************
	CAA_FREE
	Free resources in a CAA_RECORD structure.
**************************************************************************************************/
void
caa_free(CAA_RECORD *caa) {
	if (!caa)
		return;

	RELEASE(caa->tag);
	RELEASE(caa->value);
	caa->tag = NULL;
	caa->value = NULL;
	caa->tag_len = 0;
	caa->value_len = 0;
}
/*--- caa_free() --------------------------------------------------------------------------------*/


/**************************************************************************************************
	CAA_PARSE
	Parse CAA record from database RDATA field.
	Database format: "<flags> <tag> <quoted_value>" or "<flags> <tag> <value>"
	
	Examples:
	  "0 issue \"letsencrypt.org\""
	  "128 issue \"ca.example.com\""
	  "0 issuewild \";\""
	  "0 iodef \"mailto:security@example.com\""
	  "0 issue letsencrypt.org"  (without quotes)
	
	Returns 0 on success, -1 on error.
**************************************************************************************************/
int
caa_parse(const char *data, uint16_t datalen, CAA_RECORD *caa) {
	char *copy = NULL;
	char *p = NULL;
	char *flags_str = NULL;
	char *tag = NULL;
	char *value = NULL;
	char *value_start = NULL;
	char *value_end = NULL;
	int flags_val = 0;
	size_t tag_len = 0;
	size_t value_len = 0;

	if (!data || !caa || datalen == 0)
		return -1;

	/* Initialize output structure */
	memset(caa, 0, sizeof(CAA_RECORD));

	/* Make a working copy of the data */
	copy = STRNDUP(data, datalen);
	if (!copy)
		return -1;

	p = copy;

	/* Skip leading whitespace */
	while (*p && isspace((unsigned char)*p))
		p++;

	/* Parse flags (first field) */
	flags_str = p;
	while (*p && !isspace((unsigned char)*p))
		p++;
	if (*p) {
		*p++ = '\0';
	}

	/* Validate and convert flags */
	flags_val = atoi(flags_str);
	if (flags_val < 0 || flags_val > 255) {
		RELEASE(copy);
		return -1;
	}
	caa->flags = (uint8_t)flags_val;

	/* Skip whitespace between flags and tag */
	while (*p && isspace((unsigned char)*p))
		p++;

	/* Parse tag (second field) */
	tag = p;
	while (*p && !isspace((unsigned char)*p))
		p++;
	if (*p) {
		*p++ = '\0';
	}

	/* Validate tag */
	tag_len = strlen(tag);
	if (tag_len == 0 || tag_len > CAA_MAX_TAG_LEN) {
		RELEASE(copy);
		return -1;
	}

	/* Tag must be lowercase ASCII per RFC 8659 - convert to lowercase */
	caa->tag = ALLOCATE(tag_len + 1, char[]);
	if (!caa->tag) {
		RELEASE(copy);
		return -1;
	}
	for (size_t i = 0; i < tag_len; i++) {
		caa->tag[i] = tolower((unsigned char)tag[i]);
	}
	caa->tag[tag_len] = '\0';
	caa->tag_len = (uint8_t)tag_len;

	/* Skip whitespace between tag and value */
	while (*p && isspace((unsigned char)*p))
		p++;

	/* Parse value (rest of the string, may be quoted) */
	value_start = p;

	/* Handle quoted value */
	if (*value_start == '"') {
		value_start++;  /* Skip opening quote */
		value_end = strrchr(value_start, '"');
		if (value_end) {
			value_len = value_end - value_start;
		} else {
			/* No closing quote, use rest of string */
			value_len = strlen(value_start);
		}
	} else {
		/* Unquoted value - use rest of string, trimming trailing whitespace */
		value_len = strlen(value_start);
		while (value_len > 0 && isspace((unsigned char)value_start[value_len - 1])) {
			value_len--;
		}
	}

	/* Allocate and copy value */
	caa->value = ALLOCATE(value_len + 1, char[]);
	if (!caa->value) {
		RELEASE(caa->tag);
		caa->tag = NULL;
		RELEASE(copy);
		return -1;
	}
	memcpy(caa->value, value_start, value_len);
	caa->value[value_len] = '\0';
	caa->value_len = (uint16_t)value_len;

	RELEASE(copy);

#if DEBUG_ENABLED && DEBUG_CAA
	DebugX("caa", 1, _("caa_parse(): flags=%u, tag='%s' (%u bytes), value='%s' (%u bytes)"),
		   caa->flags, caa->tag, caa->tag_len, caa->value, caa->value_len);
#endif

	return 0;
}
/*--- caa_parse() -------------------------------------------------------------------------------*/


/**************************************************************************************************
	CAA_VALIDATE
	Validate CAA record data format.
	Returns 0 if valid, -1 if invalid.
**************************************************************************************************/
int
caa_validate(const CAA_RECORD *caa) {
	caa_tag_type_t tag_type;

	if (!caa)
		return -1;

	/* Check tag is present and within bounds */
	if (!caa->tag || caa->tag_len == 0 || caa->tag_len > CAA_MAX_TAG_LEN)
		return -1;

	/* Check value is present */
	if (!caa->value)
		return -1;

	/* Get tag type */
	tag_type = caa_get_tag_type(caa->tag);

	/* For known tag types, validate value format */
	switch (tag_type) {
	case CAA_TAG_ISSUE:
	case CAA_TAG_ISSUEWILD:
		/* Value should be a domain name or ";" for deny-all */
		/* We allow empty values or ";" as valid */
		break;

	case CAA_TAG_IODEF:
		/* Value should be a URL (mailto: or https:) */
		/* Basic validation: must have some content */
		if (caa->value_len == 0)
			return -1;
		break;

	case CAA_TAG_UNKNOWN:
		/* Unknown tags are allowed per RFC 8659 */
		/* Validate tag contains only valid characters (alphanumeric) */
		for (size_t i = 0; i < caa->tag_len; i++) {
			if (!isalnum((unsigned char)caa->tag[i]))
				return -1;
		}
		break;
	}

	/* Flags validation: only bit 0 (critical) is defined in RFC 8659 */
	/* Bits 1-7 must be zero according to the RFC */
	if (caa->flags & ~CAA_FLAG_CRITICAL) {
		/* Reserved bits are set - this is a warning condition per RFC */
		/* We allow it but the critical flag handling is important */
	}

	return 0;
}
/*--- caa_validate() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	CAA_BUILD_RR
	Build CAA record in DNS wire format for responses.
	
	Wire format:
	+0-1-2-3-4-5-6-7-+
	|     FLAGS      |   1 byte
	+----------------+
	|   TAG LENGTH   |   1 byte
	+----------------+
	|      TAG       |   variable (tag length bytes)
	+----------------+
	|     VALUE      |   variable (remaining bytes)
	+----------------+
	
	Returns number of bytes written, or -1 on error.
**************************************************************************************************/
int
caa_build_rr(const CAA_RECORD *caa, char *dest, size_t destlen) {
	size_t total_len;

	if (!caa || !dest)
		return -1;

	/* Calculate total length */
	total_len = caa_wire_size(caa);

	/* Check if destination buffer is large enough */
	if (destlen < total_len)
		return -1;

	/* Write flags (1 byte) */
	*dest++ = caa->flags;

	/* Write tag length (1 byte) */
	*dest++ = caa->tag_len;

	/* Write tag (tag_len bytes) */
	memcpy(dest, caa->tag, caa->tag_len);
	dest += caa->tag_len;

	/* Write value (value_len bytes) */
	memcpy(dest, caa->value, caa->value_len);

#if DEBUG_ENABLED && DEBUG_CAA
	DebugX("caa", 1, _("caa_build_rr(): built %zu bytes (flags=%u, tag='%s', value='%s')"),
		   total_len, caa->flags, caa->tag, caa->value);
#endif

	return (int)total_len;
}
/*--- caa_build_rr() ----------------------------------------------------------------------------*/

/* vi:set ts=3: */
