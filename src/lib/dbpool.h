/**************************************************************************************************
	dbpool.h: Database connection pooling for MyDNS-NG

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

#ifndef _DBPOOL_H
#define _DBPOOL_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

/* Forward declaration for SQL type (defined in mydns.h) */
#ifndef _MYDNS_H
#if USE_PGSQL
typedef PGconn SQL;
#else
typedef MYSQL SQL;
#endif
#endif

/**
 * Connection states
 */
typedef enum {
	CONN_AVAILABLE,		/* Connection is available for use */
	CONN_IN_USE,		/* Connection is currently being used */
	CONN_DEAD		/* Connection is dead and needs reconnection */
} dbpool_conn_state_t;

/**
 * Individual connection entry in the pool
 */
typedef struct {
	SQL			*conn;		/* The actual database connection */
	dbpool_conn_state_t	state;		/* Current state of this connection */
	time_t			last_used;	/* Time when last used (for idle timeout) */
	time_t			last_validated;	/* Time when last validated */
	uint64_t		query_count;	/* Number of queries executed on this connection */
	int			slot;		/* Slot index in the pool array */
} DBPOOL_CONN;

/**
 * Pool statistics for metrics integration
 */
typedef struct {
	uint32_t	pool_size;		/* Total pool size (configured) */
	uint32_t	available;		/* Currently available connections */
	uint32_t	in_use;			/* Currently in-use connections */
	uint32_t	dead;			/* Currently dead connections */
	uint64_t	wait_count;		/* Times a thread had to wait */
	uint64_t	timeout_count;		/* Number of acquire timeouts */
	uint64_t	total_acquires;		/* Total successful acquires */
	uint64_t	total_releases;		/* Total releases */
	uint64_t	reconnect_count;	/* Number of reconnections */
	uint64_t	validation_failures;	/* Number of validation failures */
} DBPOOL_STATS;

/**
 * Database connection pool structure
 */
typedef struct {
	DBPOOL_CONN		*connections;	/* Array of pool connections */
	uint32_t		size;		/* Total pool size */
	uint32_t		min_idle;	/* Minimum idle connections to maintain */
	uint32_t		acquire_timeout;/* Seconds to wait for connection */
	uint32_t		validate_interval; /* Seconds between health checks */

	pthread_mutex_t		lock;		/* Mutex for thread-safe operations */
	pthread_cond_t		available_cond;	/* Condition for waiting on available conn */

	DBPOOL_STATS		stats;		/* Pool statistics */

	/* Connection parameters (for reconnection) */
	char			*db_user;
	char			*db_password;
	char			*db_host;
	char			*db_database;

	int			initialized;	/* Whether pool has been initialized */
	int			shutting_down;	/* Whether pool is being destroyed */

	/* Background health check thread */
	pthread_t		health_thread;
	int			health_thread_running;
} DBPOOL;

/* Global pool instance */
extern DBPOOL *db_pool;

/* Configuration variables */
extern int		db_pool_enabled;
extern uint32_t		db_pool_size;
extern uint32_t		db_pool_min;
extern uint32_t		db_pool_acquire_timeout;
extern uint32_t		db_pool_validate_interval;

/**
 * Initialize the connection pool.
 * Creates connections based on configured pool size.
 *
 * @param user      Database username
 * @param password  Database password
 * @param host      Database host
 * @param database  Database name
 * @return 0 on success, -1 on failure
 */
int dbpool_init(const char *user, const char *password,
		const char *host, const char *database);

/**
 * Acquire a connection from the pool.
 * Blocks if no connection is available, up to acquire_timeout seconds.
 * Validates the connection before returning.
 *
 * @return Pointer to SQL connection, or NULL on timeout/error
 */
SQL *dbpool_acquire(void);

/**
 * Release a connection back to the pool.
 * The connection is validated before being marked available.
 *
 * @param conn  The connection to release
 */
void dbpool_release(SQL *conn);

/**
 * Destroy the connection pool.
 * Closes all connections and frees resources.
 */
void dbpool_destroy(void);

/**
 * Get a copy of current pool statistics.
 *
 * @param stats  Pointer to structure to fill with stats
 */
void dbpool_get_stats(DBPOOL_STATS *stats);

/**
 * Force validation of all idle connections.
 * Called during health check intervals.
 */
void dbpool_validate_idle(void);

/**
 * Report pool status (for logging/status commands).
 */
void dbpool_status(void);

/**
 * Check if the pool is enabled and initialized.
 *
 * @return 1 if pool is ready, 0 otherwise
 */
int dbpool_is_ready(void);

#endif /* _DBPOOL_H */

/* vi:set ts=3: */
