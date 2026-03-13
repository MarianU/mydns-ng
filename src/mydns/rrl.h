/**************************************************************************************************
	rrl.h: Response Rate Limiting for DNS amplification attack protection.

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

#ifndef _RRL_H
#define _RRL_H

#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* RRL action codes */
#define RRL_ACTION_ALLOW	0	/* Allow the response */
#define RRL_ACTION_SLIP		1	/* Send truncated response (TC bit) */
#define RRL_ACTION_DROP		2	/* Silently drop the response */

/* Default hash table size */
#define RRL_DEFAULT_SLOTS	1024

/* Rate bucket for tracking client queries */
typedef struct _rrl_bucket {
	uint32_t	hash;		/* Hash of masked IP */
	uint8_t		ip[16];		/* Client IP (masked to prefix) */
	int		is_ipv6;	/* IPv4 or IPv6 */
	uint32_t	count;		/* Queries in current window */
	time_t		window_start;	/* Start of current window */
	struct _rrl_bucket *next;	/* Hash collision chain */
} RRL_BUCKET;

/* RRL hash table */
typedef struct _rrl_table {
	uint32_t	slots;		/* Number of slots in table */
	uint32_t	count;		/* Number of buckets in use */
	RRL_BUCKET	**buckets;	/* Array of bucket pointers */
} RRL_TABLE;

/* Global RRL configuration */
extern int		rrl_enabled;		/* Is RRL enabled? */
extern uint32_t		rrl_responses_per_second; /* Rate limit */
extern uint32_t		rrl_window;		/* Time window in seconds */
extern uint32_t		rrl_slip;		/* Slip ratio (every Nth over-limit gets TC) */
extern int		rrl_ipv4_prefix;	/* IPv4 prefix length for grouping */
extern int		rrl_ipv6_prefix;	/* IPv6 prefix length for grouping */

/* RRL statistics */
extern uint32_t		rrl_dropped;		/* Total dropped responses */
extern uint32_t		rrl_slipped;		/* Total slipped (TC) responses */

/* API functions */
extern int	rrl_init(void);
extern void	rrl_cleanup(void);
extern int	rrl_check(struct sockaddr *client, int *action);
extern void	rrl_periodic_cleanup(void);
extern void	rrl_status(void);

#endif /* _RRL_H */

/* vi:set ts=3: */
