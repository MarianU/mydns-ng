/**************************************************************************************************
	rrl.c: Response Rate Limiting implementation for DNS amplification attack protection.

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
#include "rrl.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_RRL	1

/* Global RRL configuration - defined in conf.c, declared extern here */
extern int	rrl_enabled;			/* Is RRL enabled? */
extern uint32_t	rrl_responses_per_second;	/* Rate limit */
extern uint32_t	rrl_window;			/* Time window in seconds */
extern uint32_t	rrl_slip;			/* Slip ratio */
extern int	rrl_ipv4_prefix;		/* IPv4 prefix length */
extern int	rrl_ipv6_prefix;		/* IPv6 prefix length */

/* RRL statistics */
uint32_t	rrl_dropped = 0;			/* Total dropped responses */
uint32_t	rrl_slipped = 0;			/* Total slipped (TC) responses */

/* RRL hash table */
static RRL_TABLE *rrl_table = NULL;

/* Slip counter for determining when to send TC vs drop */
static uint32_t rrl_slip_counter = 0;

/* Last log time to avoid log flooding */
static time_t rrl_last_log_time = 0;
static uint32_t rrl_log_suppressed = 0;

/**************************************************************************************************
	RRL_HASH_IP
	Compute hash value for a masked IP address.
**************************************************************************************************/
static uint32_t
rrl_hash_ip(uint8_t *ip, int is_ipv6, uint32_t slots) {
	uint32_t hash = 0;
	int i;
	int len = is_ipv6 ? 16 : 4;

	/* Simple FNV-1a hash */
	hash = FNV_32_INIT;
	for (i = 0; i < len; i++) {
		hash ^= ip[i];
		hash *= FNV_32_PRIME;
	}

	return hash % slots;
}
/*--- rrl_hash_ip() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_MASK_IPV4
	Apply prefix mask to IPv4 address.
**************************************************************************************************/
static void
rrl_mask_ipv4(struct sockaddr_in *addr, uint8_t *masked_ip, int prefix_len) {
	uint32_t ip = ntohl(addr->sin_addr.s_addr);
	uint32_t mask;

	if (prefix_len >= 32) {
		mask = 0xFFFFFFFF;
	} else if (prefix_len == 0) {
		mask = 0;
	} else {
		mask = 0xFFFFFFFF << (32 - prefix_len);
	}

	ip &= mask;

	/* Store in network byte order */
	masked_ip[0] = (ip >> 24) & 0xFF;
	masked_ip[1] = (ip >> 16) & 0xFF;
	masked_ip[2] = (ip >> 8) & 0xFF;
	masked_ip[3] = ip & 0xFF;
}
/*--- rrl_mask_ipv4() ---------------------------------------------------------------------------*/


#if HAVE_IPV6
/**************************************************************************************************
	RRL_MASK_IPV6
	Apply prefix mask to IPv6 address.
**************************************************************************************************/
static void
rrl_mask_ipv6(struct sockaddr_in6 *addr, uint8_t *masked_ip, int prefix_len) {
	int full_bytes = prefix_len / 8;
	int remaining_bits = prefix_len % 8;
	int i;

	/* Copy full bytes */
	for (i = 0; i < full_bytes && i < 16; i++) {
		masked_ip[i] = addr->sin6_addr.s6_addr[i];
	}

	/* Apply partial mask if needed */
	if (full_bytes < 16 && remaining_bits > 0) {
		uint8_t mask = 0xFF << (8 - remaining_bits);
		masked_ip[full_bytes] = addr->sin6_addr.s6_addr[full_bytes] & mask;
		full_bytes++;
	}

	/* Zero remaining bytes */
	for (i = full_bytes; i < 16; i++) {
		masked_ip[i] = 0;
	}
}
/*--- rrl_mask_ipv6() ---------------------------------------------------------------------------*/
#endif


/**************************************************************************************************
	RRL_IP_EQUAL
	Compare two masked IP addresses.
**************************************************************************************************/
static int
rrl_ip_equal(uint8_t *ip1, uint8_t *ip2, int is_ipv6) {
	int len = is_ipv6 ? 16 : 4;
	return memcmp(ip1, ip2, len) == 0;
}
/*--- rrl_ip_equal() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_INIT
	Initialize the RRL subsystem.
**************************************************************************************************/
int
rrl_init(void) {
	if (!rrl_enabled) {
		return 0;
	}

	/* Allocate hash table */
	rrl_table = ALLOCATE(sizeof(RRL_TABLE), RRL_TABLE);
	rrl_table->slots = RRL_DEFAULT_SLOTS;
	rrl_table->count = 0;
	rrl_table->buckets = ALLOCATE_N(rrl_table->slots, sizeof(RRL_BUCKET *), RRL_BUCKET *);

#if DEBUG_ENABLED && DEBUG_RRL
	DebugX("rrl", 1, _("RRL initialized: %u responses/sec, %u sec window, slip=%u, IPv4/%d, IPv6/%d"),
		rrl_responses_per_second, rrl_window, rrl_slip,
		rrl_ipv4_prefix, rrl_ipv6_prefix);
#endif

	Verbose(_("Response Rate Limiting enabled: %u responses/sec, %u sec window"),
		rrl_responses_per_second, rrl_window);

	return 0;
}
/*--- rrl_init() --------------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_CLEANUP
	Clean up the RRL subsystem.
**************************************************************************************************/
void
rrl_cleanup(void) {
	uint32_t i;
	RRL_BUCKET *b, *next;

	if (!rrl_table) {
		return;
	}

	/* Free all buckets */
	for (i = 0; i < rrl_table->slots; i++) {
		for (b = rrl_table->buckets[i]; b; b = next) {
			next = b->next;
			RELEASE(b);
		}
	}

	RELEASE(rrl_table->buckets);
	RELEASE(rrl_table);
	rrl_table = NULL;
}
/*--- rrl_cleanup() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_FIND_BUCKET
	Find or create a bucket for the given masked IP.
**************************************************************************************************/
static RRL_BUCKET *
rrl_find_bucket(uint8_t *masked_ip, int is_ipv6, uint32_t hash) {
	RRL_BUCKET *b;

	/* Search existing buckets */
	for (b = rrl_table->buckets[hash]; b; b = b->next) {
		if (b->is_ipv6 == is_ipv6 && rrl_ip_equal(b->ip, masked_ip, is_ipv6)) {
			return b;
		}
	}

	/* Create new bucket */
	b = ALLOCATE(sizeof(RRL_BUCKET), RRL_BUCKET);
	b->hash = hash;
	memcpy(b->ip, masked_ip, is_ipv6 ? 16 : 4);
	b->is_ipv6 = is_ipv6;
	b->count = 0;
	b->window_start = current_time;

	/* Insert at head of chain */
	b->next = rrl_table->buckets[hash];
	rrl_table->buckets[hash] = b;
	rrl_table->count++;

	return b;
}
/*--- rrl_find_bucket() -------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_LOG_RATE_LIMITED
	Log rate limiting event (with flood protection).
**************************************************************************************************/
static void
rrl_log_rate_limited(uint8_t *ip, int is_ipv6, int action) {
	char ipstr[INET6_ADDRSTRLEN];

	/* Suppress logs if we logged recently */
	if (current_time - rrl_last_log_time < 10) {
		rrl_log_suppressed++;
		return;
	}

	/* Log the event */
	if (is_ipv6) {
#if HAVE_IPV6
		inet_ntop(AF_INET6, ip, ipstr, sizeof(ipstr));
#else
		strncpy(ipstr, "IPv6", sizeof(ipstr));
#endif
	} else {
		struct in_addr addr;
		memcpy(&addr.s_addr, ip, 4);
		inet_ntop(AF_INET, &addr, ipstr, sizeof(ipstr));
	}

	if (rrl_log_suppressed > 0) {
		Notice(_("RRL: %s from %s/%d (%u similar events suppressed)"),
			action == RRL_ACTION_SLIP ? _("slip (TC)") : _("drop"),
			ipstr, is_ipv6 ? rrl_ipv6_prefix : rrl_ipv4_prefix,
			rrl_log_suppressed);
		rrl_log_suppressed = 0;
	} else {
		Notice(_("RRL: %s from %s/%d"),
			action == RRL_ACTION_SLIP ? _("slip (TC)") : _("drop"),
			ipstr, is_ipv6 ? rrl_ipv6_prefix : rrl_ipv4_prefix);
	}

	rrl_last_log_time = current_time;
}
/*--- rrl_log_rate_limited() --------------------------------------------------------------------*/


/* Track last cleanup time for lazy cleanup */
static time_t rrl_last_cleanup_time = 0;

/**************************************************************************************************
	RRL_CHECK
	Check if a response should be rate limited.
	Returns 0 on success, sets action to:
	  RRL_ACTION_ALLOW - send response normally
	  RRL_ACTION_SLIP  - send truncated response (TC bit set)
	  RRL_ACTION_DROP  - silently drop response
**************************************************************************************************/
int
rrl_check(struct sockaddr *client, int *action) {
	uint8_t masked_ip[16];
	int is_ipv6 = 0;
	uint32_t hash;
	RRL_BUCKET *b;
	uint32_t max_queries;

	*action = RRL_ACTION_ALLOW;

	/* If RRL is disabled, always allow */
	if (!rrl_enabled || !rrl_table) {
		return 0;
	}

	/* Lazy cleanup: run periodically (every rrl_window seconds) */
	if (current_time - rrl_last_cleanup_time > (time_t)rrl_window) {
		rrl_periodic_cleanup();
		rrl_last_cleanup_time = current_time;
	}

	/* Extract and mask the client IP */
	memset(masked_ip, 0, sizeof(masked_ip));

	if (client->sa_family == AF_INET) {
		rrl_mask_ipv4((struct sockaddr_in *)client, masked_ip, rrl_ipv4_prefix);
		is_ipv6 = 0;
#if HAVE_IPV6
	} else if (client->sa_family == AF_INET6) {
		rrl_mask_ipv6((struct sockaddr_in6 *)client, masked_ip, rrl_ipv6_prefix);
		is_ipv6 = 1;
#endif
	} else {
		/* Unknown address family, allow */
		return 0;
	}

	/* Compute hash and find/create bucket */
	hash = rrl_hash_ip(masked_ip, is_ipv6, rrl_table->slots);
	b = rrl_find_bucket(masked_ip, is_ipv6, hash);

	/* Check if window has expired */
	if (current_time - b->window_start >= (time_t)rrl_window) {
		b->count = 0;
		b->window_start = current_time;
	}

	/* Increment count */
	b->count++;

	/* Calculate maximum allowed queries in window */
	max_queries = rrl_responses_per_second * rrl_window;

	/* Check if over limit */
	if (b->count > max_queries) {
		/* Determine whether to slip (TC) or drop */
		if (rrl_slip > 0) {
			rrl_slip_counter++;
			if (rrl_slip_counter >= rrl_slip) {
				rrl_slip_counter = 0;
				*action = RRL_ACTION_SLIP;
				rrl_slipped++;
			} else {
				*action = RRL_ACTION_DROP;
				rrl_dropped++;
			}
		} else {
			/* Slip disabled, always drop */
			*action = RRL_ACTION_DROP;
			rrl_dropped++;
		}

		rrl_log_rate_limited(masked_ip, is_ipv6, *action);

#if DEBUG_ENABLED && DEBUG_RRL
		DebugX("rrl", 1, _("RRL: action=%d, count=%u, max=%u"),
			*action, b->count, max_queries);
#endif
	}

	return 0;
}
/*--- rrl_check() -------------------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_PERIODIC_CLEANUP
	Remove expired buckets from the hash table (called periodically).
**************************************************************************************************/
void
rrl_periodic_cleanup(void) {
	uint32_t i;
	RRL_BUCKET *b, *prev, *next;
	uint32_t removed = 0;

	if (!rrl_enabled || !rrl_table) {
		return;
	}

	/* Scan all slots and remove expired buckets */
	for (i = 0; i < rrl_table->slots; i++) {
		prev = NULL;
		for (b = rrl_table->buckets[i]; b; b = next) {
			next = b->next;

			/* Remove if window has expired (with some margin) */
			if (current_time - b->window_start > (time_t)(rrl_window * 2)) {
				if (prev) {
					prev->next = next;
				} else {
					rrl_table->buckets[i] = next;
				}
				RELEASE(b);
				rrl_table->count--;
				removed++;
			} else {
				prev = b;
			}
		}
	}

#if DEBUG_ENABLED && DEBUG_RRL
	if (removed > 0) {
		DebugX("rrl", 1, _("RRL cleanup: removed %u expired buckets, %u remaining"),
			removed, rrl_table->count);
	}
#endif
}
/*--- rrl_periodic_cleanup() --------------------------------------------------------------------*/


/**************************************************************************************************
	RRL_STATUS
	Report RRL statistics (called on SIGUSR1).
**************************************************************************************************/
void
rrl_status(void) {
	if (!rrl_enabled) {
		return;
	}

	Notice(_("RRL: %u dropped, %u slipped, %u buckets in use"),
		rrl_dropped, rrl_slipped,
		rrl_table ? rrl_table->count : 0);
}
/*--- rrl_status() ------------------------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */
