/**************************************************************************************************
	db_health.c: Database health monitoring for MyDNS-NG

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

#include "mydns.h"
#include "db_health.h"

/* Make this nonzero to enable debugging for this source file */
#define DEBUG_DB_HEALTH 1

/* Global database health state */
DB_HEALTH db_health;

/* Configuration values with defaults */
uint32_t db_stale_cache_ttl = 3600;		/* Serve stale data for up to 1 hour */
uint32_t db_reconnect_base_interval = 1;	/* Start reconnection at 1 second */
uint32_t db_reconnect_max_interval = 60;	/* Maximum backoff of 60 seconds */
int db_cache_warm_on_startup = 0;		/* Don't warm cache on startup by default */
int db_cache_warm_on_reconnect = 1;		/* Warm cache after reconnection by default */

/**************************************************************************************************
	DB_HEALTH_INIT
	Initialize database health tracking. Call once during startup.
**************************************************************************************************/
void
db_health_init(void) {
	db_health.state = DB_STATE_CONNECTED;
	db_health.last_failure = 0;
	db_health.next_retry = 0;
	db_health.retry_count = 0;
	db_health.backoff_seconds = 0;
	db_health.total_disconnections = 0;
	db_health.stale_cache_hits = 0;
	db_health.reconnect_successes = 0;

#if DEBUG_ENABLED && DEBUG_DB_HEALTH
	DebugX("db_health", 1, _("Database health monitoring initialized"));
	DebugX("db_health", 1, _("  stale-cache-ttl=%u, reconnect-base=%u, reconnect-max=%u"),
		db_stale_cache_ttl, db_reconnect_base_interval, db_reconnect_max_interval);
#endif
}
/*--- db_health_init() --------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_IS_AVAILABLE
	Check if database is currently available for queries.
	Returns 1 if available, 0 if in backoff or disconnected.
**************************************************************************************************/
int
db_is_available(void) {
	if (db_health.state == DB_STATE_CONNECTED)
		return 1;

	/* If in backoff, check if it's time to retry */
	if (db_health.state == DB_STATE_BACKOFF) {
		time_t now = time(NULL);
		if (now >= db_health.next_retry) {
			/* Time to try again */
			return 1;
		}
	}

	return 0;
}
/*--- db_is_available() -------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_SHOULD_RETRY
	Check if enough time has passed to attempt a retry.
	Returns 1 if we should retry, 0 if still in backoff period.
**************************************************************************************************/
int
db_should_retry(void) {
	time_t now = time(NULL);

	/* If connected, no need to retry */
	if (db_health.state == DB_STATE_CONNECTED)
		return 0;

	/* Check if backoff period has elapsed */
	if (now >= db_health.next_retry) {
#if DEBUG_ENABLED && DEBUG_DB_HEALTH
		DebugX("db_health", 1, _("Backoff period elapsed, attempting reconnection (retry #%u)"),
			db_health.retry_count + 1);
#endif
		return 1;
	}

#if DEBUG_ENABLED && DEBUG_DB_HEALTH
	DebugX("db_health", 2, _("Still in backoff period, %ld seconds remaining"),
		(long)(db_health.next_retry - now));
#endif
	return 0;
}
/*--- db_should_retry() -------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_MARK_FAILURE
	Mark the database as having failed.
	Updates backoff timers and state using exponential backoff.
**************************************************************************************************/
void
db_mark_failure(void) {
	time_t now = time(NULL);

	/* Only increment disconnection count on first failure */
	if (db_health.state == DB_STATE_CONNECTED) {
		db_health.total_disconnections++;
		db_health.backoff_seconds = db_reconnect_base_interval;
		Warn(_("Database connection lost (total disconnections: %llu)"),
			(unsigned long long)db_health.total_disconnections);
	} else {
		/* Exponential backoff: double the interval */
		db_health.backoff_seconds *= 2;
		if (db_health.backoff_seconds > db_reconnect_max_interval)
			db_health.backoff_seconds = db_reconnect_max_interval;
	}

	db_health.state = DB_STATE_BACKOFF;
	db_health.last_failure = now;
	db_health.next_retry = now + db_health.backoff_seconds;
	db_health.retry_count++;

#if DEBUG_ENABLED && DEBUG_DB_HEALTH
	DebugX("db_health", 1, _("Database marked as failed, backoff=%u seconds, next retry in %u seconds"),
		db_health.backoff_seconds, db_health.backoff_seconds);
#endif

	Warn(_("Database unavailable, next reconnection attempt in %u seconds"),
		db_health.backoff_seconds);
}
/*--- db_mark_failure() -------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_MARK_SUCCESS
	Mark the database as successfully connected.
	Resets backoff state.
**************************************************************************************************/
void
db_mark_success(void) {
	int was_disconnected = (db_health.state != DB_STATE_CONNECTED);

	if (was_disconnected) {
		db_health.reconnect_successes++;
		Notice(_("Database connection restored after %u retries (total reconnections: %llu)"),
			db_health.retry_count,
			(unsigned long long)db_health.reconnect_successes);
	}

	db_health.state = DB_STATE_CONNECTED;
	db_health.retry_count = 0;
	db_health.backoff_seconds = 0;
	db_health.next_retry = 0;

#if DEBUG_ENABLED && DEBUG_DB_HEALTH
	if (was_disconnected) {
		DebugX("db_health", 1, _("Database connection restored, backoff state reset"));
	}
#endif
}
/*--- db_mark_success() -------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_STATE_STR
	Get current database state as a string (for logging/status).
**************************************************************************************************/
const char *
db_state_str(db_state_t state) {
	switch (state) {
		case DB_STATE_CONNECTED:
			return _("connected");
		case DB_STATE_DISCONNECTED:
			return _("disconnected");
		case DB_STATE_BACKOFF:
			return _("backoff");
		default:
			return _("unknown");
	}
}
/*--- db_state_str() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	DB_HEALTH_STATUS
	Report database health statistics. Called during status reporting.
**************************************************************************************************/
void
db_health_status(void) {
	time_t now = time(NULL);
	const char *state_str = db_state_str(db_health.state);

	Notice(_("Database health: state=%s, disconnections=%llu, reconnections=%llu, stale_cache_hits=%llu"),
		state_str,
		(unsigned long long)db_health.total_disconnections,
		(unsigned long long)db_health.reconnect_successes,
		(unsigned long long)db_health.stale_cache_hits);

	if (db_health.state == DB_STATE_BACKOFF) {
		long secs_remaining = (long)(db_health.next_retry - now);
		if (secs_remaining < 0) secs_remaining = 0;
		Notice(_("  Currently in backoff, %ld seconds until next retry (retry #%u)"),
			secs_remaining, db_health.retry_count);
	}
}
/*--- db_health_status() ------------------------------------------------------------------------*/

/* vi:set ts=3: */
