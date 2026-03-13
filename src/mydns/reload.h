/**************************************************************************************************
	reload.h: Configuration hot reload for MyDNS-NG

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

#ifndef _MYDNS_RELOAD_H
#define _MYDNS_RELOAD_H

#include <stdint.h>
#include <time.h>

/*
 * Configuration Hot Reload for MyDNS-NG
 *
 * Hot-reloadable options (safe to change at runtime):
 *   - log-level (change verbosity via log config)
 *   - rrl-enabled, rrl-responses-per-second, rrl-window, rrl-slip (rate limiting)
 *   - rrl-ipv4-prefix, rrl-ipv6-prefix (client grouping)
 *   - cache-size, cache-expire (cache tuning - will clear and reinit caches)
 *   - zone-cache-size, zone-cache-expire
 *   - reply-cache-size, reply-cache-expire
 *
 * NOT hot-reloadable (require restart):
 *   - listen addresses (bind/listen)
 *   - database connection settings (db-host, db-user, db-password, database)
 *   - user/group settings (require privilege changes)
 *   - http-listen, http-port (HTTP server binding)
 *   - allow-axfr, allow-tcp, allow-update (protocol features)
 */

/* Reload state */
typedef enum {
	RELOAD_STATE_IDLE = 0,		/* No reload in progress */
	RELOAD_STATE_PENDING,		/* Reload requested (signal received) */
	RELOAD_STATE_LOADING,		/* Currently loading new config */
	RELOAD_STATE_APPLYING,		/* Applying validated config */
	RELOAD_STATE_SUCCESS,		/* Last reload succeeded */
	RELOAD_STATE_FAILED		/* Last reload failed */
} reload_state_t;

/* Reload result codes */
typedef enum {
	RELOAD_OK = 0,			/* Reload successful */
	RELOAD_ERR_PARSE,		/* Failed to parse config file */
	RELOAD_ERR_VALIDATE,		/* Config validation failed */
	RELOAD_ERR_APPLY,		/* Failed to apply config */
	RELOAD_ERR_ROLLBACK		/* Rollback after apply failure */
} reload_result_t;

/* Configuration change tracking */
typedef struct _reload_changes {
	int	log_changed;
	int	rrl_changed;
	int	cache_changed;
	int	zone_cache_changed;
	int	reply_cache_changed;
} RELOAD_CHANGES;

/* Reload statistics (for metrics) */
typedef struct _reload_stats {
	uint64_t	reload_total;		/* Total reload attempts */
	uint64_t	reload_success;		/* Successful reloads */
	uint64_t	reload_failed;		/* Failed reloads */
	time_t		last_reload_time;	/* Unix timestamp of last reload attempt */
	time_t		last_success_time;	/* Unix timestamp of last successful reload */
	reload_state_t	state;			/* Current reload state */
	reload_result_t	last_result;		/* Result of last reload */
	char		last_error[256];	/* Description of last error */
} RELOAD_STATS;

/* Global reload statistics */
extern RELOAD_STATS reload_stats;

/* Volatile flag set by signal handler */
extern volatile int reload_pending;

/*
 * API Functions
 */

/**
 * Initialize the reload subsystem.
 * Called once at startup from main().
 */
extern void reload_init(void);

/**
 * Trigger a configuration reload.
 * Safe to call from signal handler (sets flag only).
 * Actual reload happens in reload_check().
 */
extern void reload_trigger(void);

/**
 * Check if a reload is pending and perform it.
 * Called from main loop or after signal handling.
 * Returns reload_result_t indicating success or failure.
 */
extern reload_result_t reload_check(void);

/**
 * Perform the actual configuration reload.
 * Not safe to call from signal handler.
 * Returns reload_result_t indicating success or failure.
 */
extern reload_result_t reload_config(void);

/**
 * Get current reload statistics.
 * Thread-safe read of reload stats.
 */
extern void reload_get_stats(RELOAD_STATS *stats);

/**
 * Format reload metrics in Prometheus format.
 * Appends metrics to the provided buffer.
 * Returns number of characters written.
 */
extern int reload_format_prometheus(char *buf, size_t bufsize);

#endif /* _MYDNS_RELOAD_H */

/* vi:set ts=3: */
