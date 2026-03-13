/**************************************************************************************************
	db_health.h: Database health monitoring for MyDNS-NG

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>
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

#ifndef _DB_HEALTH_H
#define _DB_HEALTH_H

#include <stdint.h>
#include <time.h>

/* Database connection states */
typedef enum {
	DB_STATE_CONNECTED,		/* Database is available */
	DB_STATE_DISCONNECTED,		/* Database is unavailable */
	DB_STATE_BACKOFF		/* In backoff period, not retrying yet */
} db_state_t;

/* Database health tracking structure */
typedef struct {
	db_state_t	state;			/* Current connection state */
	time_t		last_failure;		/* Time of last failure */
	time_t		next_retry;		/* Time when next retry is allowed */
	uint32_t	retry_count;		/* Number of consecutive retries */
	uint32_t	backoff_seconds;	/* Current backoff interval */
	uint64_t	total_disconnections;	/* Total number of disconnection events */
	uint64_t	stale_cache_hits;	/* Number of times stale cache was served */
	uint64_t	reconnect_successes;	/* Number of successful reconnections */
} DB_HEALTH;

/* Global database health state */
extern DB_HEALTH db_health;

/* Configuration values */
extern uint32_t db_stale_cache_ttl;		/* Seconds to serve stale data */
extern uint32_t db_reconnect_base_interval;	/* Base reconnection interval */
extern uint32_t db_reconnect_max_interval;	/* Maximum reconnection interval */
extern int db_cache_warm_on_startup;		/* Warm cache on startup */
extern int db_cache_warm_on_reconnect;		/* Warm cache after reconnection */

/**
 * Initialize database health tracking.
 * Call once during startup.
 */
void db_health_init(void);

/**
 * Check if database is currently available for queries.
 * Returns 1 if available, 0 if in backoff or disconnected.
 */
int db_is_available(void);

/**
 * Check if enough time has passed to attempt a retry.
 * Returns 1 if we should retry, 0 if still in backoff period.
 */
int db_should_retry(void);

/**
 * Mark the database as having failed.
 * Updates backoff timers and state.
 */
void db_mark_failure(void);

/**
 * Mark the database as successfully connected.
 * Resets backoff state and optionally triggers cache warming.
 */
void db_mark_success(void);

/**
 * Get current database state as a string (for logging/status).
 */
const char *db_state_str(db_state_t state);

/**
 * Report database health statistics.
 * Called during status reporting.
 */
void db_health_status(void);

#endif /* _DB_HEALTH_H */

/* vi:set ts=3: */
