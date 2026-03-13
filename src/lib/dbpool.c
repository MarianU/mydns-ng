/**************************************************************************************************
	dbpool.c: Database connection pooling for MyDNS-NG

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
#include "dbpool.h"

#include <errno.h>
#include <string.h>

/* Make this nonzero to enable debugging for this source file */
#define DEBUG_DBPOOL 1

/* Global pool instance */
DBPOOL *db_pool = NULL;

/* Configuration variables with defaults */
int		db_pool_enabled = 0;
uint32_t	db_pool_size = 10;
uint32_t	db_pool_min = 2;
uint32_t	db_pool_acquire_timeout = 5;
uint32_t	db_pool_validate_interval = 30;

/* Forward declarations for internal functions */
static SQL *dbpool_create_connection(void);
static int dbpool_validate_connection(SQL *conn);
static void dbpool_close_connection(SQL *conn);
static void *dbpool_health_thread(void *arg);
static DBPOOL_CONN *dbpool_find_conn_by_sql(SQL *conn);


/**************************************************************************************************
	DBPOOL_CREATE_CONNECTION
	Create a new database connection using stored credentials.
**************************************************************************************************/
static SQL *
dbpool_create_connection(void) {
	SQL *conn = NULL;
	char *portp = NULL;
	unsigned int port = 0;
	char *host2 = NULL;

	if (!db_pool || !db_pool->db_host)
		return NULL;

	host2 = STRDUP(db_pool->db_host);

	if ((portp = strchr(host2, ':')) != NULL) {
		port = atoi(portp + 1);
		*portp = '\0';
	}

#if USE_PGSQL
	conn = PQsetdbLogin(host2, portp, NULL, NULL,
			    db_pool->db_database,
			    db_pool->db_user,
			    db_pool->db_password);
	if (PQstatus(conn) == CONNECTION_BAD) {
		if (conn)
			PQfinish(conn);
		/* Try via UNIX socket */
		conn = PQsetdbLogin(NULL, NULL, NULL, NULL,
				    db_pool->db_database,
				    db_pool->db_user,
				    db_pool->db_password);
		if (PQstatus(conn) == CONNECTION_BAD) {
			if (conn)
				PQfinish(conn);
			conn = NULL;
		}
	}
#else
	conn = ALLOCATE(sizeof(*conn), MYSQL);
	if (!mysql_init(conn)) {
		RELEASE(conn);
		RELEASE(host2);
		return NULL;
	}
#if MYSQL_VERSION_ID > 32349
	mysql_options(conn, MYSQL_READ_DEFAULT_GROUP, "client");
#endif
#if MYSQL_VERSION_ID > 50012
	mysql_options(conn, MYSQL_OPT_RECONNECT, "1");
#endif
	if (!mysql_real_connect(conn, host2,
				db_pool->db_user,
				db_pool->db_password,
				db_pool->db_database,
				port, NULL, 0)) {
		mysql_close(conn);
		RELEASE(conn);
		conn = NULL;
	}
#endif

	RELEASE(host2);
	return conn;
}
/*--- dbpool_create_connection() ----------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_VALIDATE_CONNECTION
	Validate that a connection is still alive using ping/SELECT 1.
	Returns 1 if valid, 0 if dead.
**************************************************************************************************/
static int
dbpool_validate_connection(SQL *conn) {
	if (!conn)
		return 0;

#if USE_PGSQL
	{
		PGresult *res = PQexec(conn, "SELECT 1");
		ExecStatusType status = PQresultStatus(res);
		PQclear(res);
		if (status != PGRES_TUPLES_OK)
			return 0;
	}
#else
	if (mysql_ping(conn) != 0)
		return 0;
#endif

	return 1;
}
/*--- dbpool_validate_connection() --------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_CLOSE_CONNECTION
	Close a database connection.
**************************************************************************************************/
static void
dbpool_close_connection(SQL *conn) {
	if (!conn)
		return;

#if USE_PGSQL
	PQfinish(conn);
#else
	mysql_close(conn);
	RELEASE(conn);
#endif
}
/*--- dbpool_close_connection() -----------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_FIND_CONN_BY_SQL
	Find a pool connection entry by its SQL pointer.
**************************************************************************************************/
static DBPOOL_CONN *
dbpool_find_conn_by_sql(SQL *conn) {
	uint32_t i;

	if (!db_pool || !conn)
		return NULL;

	for (i = 0; i < db_pool->size; i++) {
		if (db_pool->connections[i].conn == conn) {
			return &db_pool->connections[i];
		}
	}

	return NULL;
}
/*--- dbpool_find_conn_by_sql() -----------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_HEALTH_THREAD
	Background thread that periodically validates idle connections.
**************************************************************************************************/
static void *
dbpool_health_thread(void *arg) {
	(void)arg;  /* Unused */

#if DEBUG_ENABLED && DEBUG_DBPOOL
	DebugX("dbpool", 1, _("Health check thread started"));
#endif

	while (db_pool && !db_pool->shutting_down) {
		sleep(db_pool->validate_interval);

		if (db_pool->shutting_down)
			break;

		dbpool_validate_idle();
	}

#if DEBUG_ENABLED && DEBUG_DBPOOL
	DebugX("dbpool", 1, _("Health check thread exiting"));
#endif

	return NULL;
}
/*--- dbpool_health_thread() --------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_INIT
	Initialize the connection pool.
**************************************************************************************************/
int
dbpool_init(const char *user, const char *password,
	    const char *host, const char *database) {
	uint32_t i;
	uint32_t created = 0;

	if (!db_pool_enabled) {
#if DEBUG_ENABLED && DEBUG_DBPOOL
		DebugX("dbpool", 1, _("Connection pooling is disabled"));
#endif
		return 0;
	}

	if (db_pool) {
		Warn(_("Connection pool already initialized"));
		return -1;
	}

	/* Allocate pool structure */
	db_pool = ALLOCATE(sizeof(DBPOOL), DBPOOL);
	memset(db_pool, 0, sizeof(DBPOOL));

	/* Store configuration */
	db_pool->size = db_pool_size;
	db_pool->min_idle = db_pool_min;
	db_pool->acquire_timeout = db_pool_acquire_timeout;
	db_pool->validate_interval = db_pool_validate_interval;

	/* Store connection parameters */
	db_pool->db_user = user ? STRDUP(user) : NULL;
	db_pool->db_password = password ? STRDUP(password) : NULL;
	db_pool->db_host = host ? STRDUP(host) : NULL;
	db_pool->db_database = database ? STRDUP(database) : NULL;

	/* Initialize synchronization primitives */
	if (pthread_mutex_init(&db_pool->lock, NULL) != 0) {
		Warn(_("Failed to initialize pool mutex: %s"), strerror(errno));
		goto init_failed;
	}

	if (pthread_cond_init(&db_pool->available_cond, NULL) != 0) {
		Warn(_("Failed to initialize pool condition: %s"), strerror(errno));
		pthread_mutex_destroy(&db_pool->lock);
		goto init_failed;
	}

	/* Allocate connection array */
	db_pool->connections = ALLOCATE_N(db_pool->size, sizeof(DBPOOL_CONN), DBPOOL_CONN);
	memset(db_pool->connections, 0, db_pool->size * sizeof(DBPOOL_CONN));

	/* Initialize each connection slot */
	for (i = 0; i < db_pool->size; i++) {
		db_pool->connections[i].slot = i;
		db_pool->connections[i].state = CONN_DEAD;
		db_pool->connections[i].conn = NULL;
	}

	/* Create minimum number of connections */
	for (i = 0; i < db_pool->min_idle && i < db_pool->size; i++) {
		SQL *conn = dbpool_create_connection();
		if (conn) {
			db_pool->connections[i].conn = conn;
			db_pool->connections[i].state = CONN_AVAILABLE;
			db_pool->connections[i].last_validated = time(NULL);
			created++;
		} else {
			Warn(_("Failed to create initial pool connection %u"), i);
		}
	}

	/* Update stats */
	db_pool->stats.pool_size = db_pool->size;
	db_pool->stats.available = created;

	/* Start health check thread */
	if (pthread_create(&db_pool->health_thread, NULL,
			   dbpool_health_thread, NULL) == 0) {
		db_pool->health_thread_running = 1;
	} else {
		Warn(_("Failed to start health check thread: %s"), strerror(errno));
	}

	db_pool->initialized = 1;

	Verbose(_("Connection pool initialized: size=%u, min=%u, created=%u"),
		db_pool->size, db_pool->min_idle, created);

#if DEBUG_ENABLED && DEBUG_DBPOOL
	DebugX("dbpool", 1, _("Pool initialized: size=%u, timeout=%u, validate=%u"),
	       db_pool->size, db_pool->acquire_timeout, db_pool->validate_interval);
#endif

	return 0;

init_failed:
	if (db_pool->db_user) RELEASE(db_pool->db_user);
	if (db_pool->db_password) RELEASE(db_pool->db_password);
	if (db_pool->db_host) RELEASE(db_pool->db_host);
	if (db_pool->db_database) RELEASE(db_pool->db_database);
	RELEASE(db_pool);
	db_pool = NULL;
	return -1;
}
/*--- dbpool_init() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_ACQUIRE
	Acquire a connection from the pool.
**************************************************************************************************/
SQL *
dbpool_acquire(void) {
	uint32_t i;
	SQL *conn = NULL;
	struct timespec timeout;
	int wait_result = 0;

	if (!db_pool || !db_pool->initialized) {
		return NULL;
	}

	pthread_mutex_lock(&db_pool->lock);

	while (!conn && !db_pool->shutting_down) {
		/* First, try to find an available connection */
		for (i = 0; i < db_pool->size; i++) {
			if (db_pool->connections[i].state == CONN_AVAILABLE) {
				DBPOOL_CONN *pc = &db_pool->connections[i];

				/* Check if validation is needed */
				time_t now = time(NULL);
				if ((now - pc->last_validated) > (time_t)db_pool->validate_interval) {
					if (!dbpool_validate_connection(pc->conn)) {
#if DEBUG_ENABLED && DEBUG_DBPOOL
						DebugX("dbpool", 1, _("Slot %d: connection failed validation, reconnecting"), i);
#endif
						dbpool_close_connection(pc->conn);
						pc->conn = dbpool_create_connection();
						if (!pc->conn) {
							pc->state = CONN_DEAD;
							db_pool->stats.validation_failures++;
							continue;
						}
						db_pool->stats.reconnect_count++;
					}
					pc->last_validated = now;
				}

				/* Mark as in use and return */
				pc->state = CONN_IN_USE;
				pc->last_used = now;
				pc->query_count++;
				conn = pc->conn;

				db_pool->stats.available--;
				db_pool->stats.in_use++;
				db_pool->stats.total_acquires++;

#if DEBUG_ENABLED && DEBUG_DBPOOL
				DebugX("dbpool", 2, _("Acquired connection from slot %d"), i);
#endif
				break;
			}
		}

		if (conn)
			break;

		/* Try to create a new connection in a dead slot */
		for (i = 0; i < db_pool->size; i++) {
			if (db_pool->connections[i].state == CONN_DEAD) {
				SQL *new_conn = dbpool_create_connection();
				if (new_conn) {
					DBPOOL_CONN *pc = &db_pool->connections[i];
					pc->conn = new_conn;
					pc->state = CONN_IN_USE;
					pc->last_used = time(NULL);
					pc->last_validated = time(NULL);
					pc->query_count = 1;
					conn = new_conn;

					db_pool->stats.in_use++;
					db_pool->stats.dead--;
					db_pool->stats.total_acquires++;
					db_pool->stats.reconnect_count++;

#if DEBUG_ENABLED && DEBUG_DBPOOL
					DebugX("dbpool", 1, _("Created new connection in slot %d"), i);
#endif
					break;
				}
			}
		}

		if (conn)
			break;

		/* No connection available, wait for one to be released */
		db_pool->stats.wait_count++;

		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += db_pool->acquire_timeout;

#if DEBUG_ENABLED && DEBUG_DBPOOL
		DebugX("dbpool", 1, _("No connections available, waiting (timeout=%u)"),
		       db_pool->acquire_timeout);
#endif

		wait_result = pthread_cond_timedwait(&db_pool->available_cond,
						     &db_pool->lock,
						     &timeout);

		if (wait_result == ETIMEDOUT) {
			db_pool->stats.timeout_count++;
			Warn(_("Connection pool acquire timeout after %u seconds"),
			     db_pool->acquire_timeout);
			break;
		}
	}

	pthread_mutex_unlock(&db_pool->lock);

	return conn;
}
/*--- dbpool_acquire() --------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_RELEASE
	Release a connection back to the pool.
**************************************************************************************************/
void
dbpool_release(SQL *conn) {
	DBPOOL_CONN *pc;

	if (!db_pool || !conn)
		return;

	pthread_mutex_lock(&db_pool->lock);

	pc = dbpool_find_conn_by_sql(conn);
	if (!pc) {
		Warn(_("Attempted to release unknown connection to pool"));
		pthread_mutex_unlock(&db_pool->lock);
		return;
	}

	if (pc->state != CONN_IN_USE) {
		Warn(_("Attempted to release connection not in use (slot %d, state %d)"),
		     pc->slot, pc->state);
		pthread_mutex_unlock(&db_pool->lock);
		return;
	}

	/* Mark as available */
	pc->state = CONN_AVAILABLE;
	pc->last_used = time(NULL);

	db_pool->stats.in_use--;
	db_pool->stats.available++;
	db_pool->stats.total_releases++;

#if DEBUG_ENABLED && DEBUG_DBPOOL
	DebugX("dbpool", 2, _("Released connection to slot %d"), pc->slot);
#endif

	/* Signal any waiting threads */
	pthread_cond_signal(&db_pool->available_cond);

	pthread_mutex_unlock(&db_pool->lock);
}
/*--- dbpool_release() --------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_DESTROY
	Destroy the connection pool.
**************************************************************************************************/
void
dbpool_destroy(void) {
	uint32_t i;

	if (!db_pool)
		return;

	Verbose(_("Destroying connection pool..."));

	/* Signal shutdown */
	pthread_mutex_lock(&db_pool->lock);
	db_pool->shutting_down = 1;
	pthread_cond_broadcast(&db_pool->available_cond);
	pthread_mutex_unlock(&db_pool->lock);

	/* Wait for health thread to exit */
	if (db_pool->health_thread_running) {
		pthread_join(db_pool->health_thread, NULL);
		db_pool->health_thread_running = 0;
	}

	/* Close all connections */
	pthread_mutex_lock(&db_pool->lock);
	for (i = 0; i < db_pool->size; i++) {
		if (db_pool->connections[i].conn) {
			dbpool_close_connection(db_pool->connections[i].conn);
			db_pool->connections[i].conn = NULL;
		}
	}
	pthread_mutex_unlock(&db_pool->lock);

	/* Clean up */
	pthread_mutex_destroy(&db_pool->lock);
	pthread_cond_destroy(&db_pool->available_cond);

	if (db_pool->connections)
		RELEASE(db_pool->connections);
	if (db_pool->db_user)
		RELEASE(db_pool->db_user);
	if (db_pool->db_password)
		RELEASE(db_pool->db_password);
	if (db_pool->db_host)
		RELEASE(db_pool->db_host);
	if (db_pool->db_database)
		RELEASE(db_pool->db_database);

	RELEASE(db_pool);
	db_pool = NULL;

	Verbose(_("Connection pool destroyed"));
}
/*--- dbpool_destroy() --------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_GET_STATS
	Get a copy of current pool statistics.
**************************************************************************************************/
void
dbpool_get_stats(DBPOOL_STATS *stats) {
	if (!stats)
		return;

	if (!db_pool) {
		memset(stats, 0, sizeof(DBPOOL_STATS));
		return;
	}

	pthread_mutex_lock(&db_pool->lock);
	memcpy(stats, &db_pool->stats, sizeof(DBPOOL_STATS));
	pthread_mutex_unlock(&db_pool->lock);
}
/*--- dbpool_get_stats() ------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_VALIDATE_IDLE
	Validate all idle connections.
**************************************************************************************************/
void
dbpool_validate_idle(void) {
	uint32_t i;
	uint32_t validated = 0;
	uint32_t reconnected = 0;
	time_t now;

	if (!db_pool || !db_pool->initialized || db_pool->shutting_down)
		return;

	now = time(NULL);

	pthread_mutex_lock(&db_pool->lock);

	for (i = 0; i < db_pool->size; i++) {
		DBPOOL_CONN *pc = &db_pool->connections[i];

		if (pc->state != CONN_AVAILABLE)
			continue;

		/* Skip if recently validated */
		if ((now - pc->last_validated) < (time_t)db_pool->validate_interval)
			continue;

		if (!dbpool_validate_connection(pc->conn)) {
#if DEBUG_ENABLED && DEBUG_DBPOOL
			DebugX("dbpool", 1, _("Slot %d: idle connection failed validation, reconnecting"), i);
#endif
			dbpool_close_connection(pc->conn);
			pc->conn = dbpool_create_connection();
			if (pc->conn) {
				reconnected++;
				db_pool->stats.reconnect_count++;
			} else {
				pc->state = CONN_DEAD;
				db_pool->stats.available--;
				db_pool->stats.dead++;
				db_pool->stats.validation_failures++;
			}
		}

		pc->last_validated = now;
		validated++;
	}

	/* Ensure minimum idle connections are maintained */
	if (db_pool->stats.available < db_pool->min_idle) {
		for (i = 0; i < db_pool->size && db_pool->stats.available < db_pool->min_idle; i++) {
			DBPOOL_CONN *pc = &db_pool->connections[i];
			if (pc->state == CONN_DEAD) {
				SQL *new_conn = dbpool_create_connection();
				if (new_conn) {
					pc->conn = new_conn;
					pc->state = CONN_AVAILABLE;
					pc->last_validated = now;
					db_pool->stats.available++;
					db_pool->stats.dead--;
					db_pool->stats.reconnect_count++;
					reconnected++;
#if DEBUG_ENABLED && DEBUG_DBPOOL
					DebugX("dbpool", 1, _("Slot %d: created connection to maintain minimum"), i);
#endif
				}
			}
		}
	}

	pthread_mutex_unlock(&db_pool->lock);

#if DEBUG_ENABLED && DEBUG_DBPOOL
	if (validated > 0 || reconnected > 0) {
		DebugX("dbpool", 1, _("Health check: validated=%u, reconnected=%u"),
		       validated, reconnected);
	}
#endif
}
/*--- dbpool_validate_idle() --------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_STATUS
	Report pool status.
**************************************************************************************************/
void
dbpool_status(void) {
	DBPOOL_STATS stats;

	if (!db_pool || !db_pool_enabled) {
		Notice(_("Connection pooling: disabled"));
		return;
	}

	dbpool_get_stats(&stats);

	Notice(_("Connection pool: size=%u, available=%u, in_use=%u, dead=%u"),
	       stats.pool_size, stats.available, stats.in_use, stats.dead);
	Notice(_("  acquires=%llu, releases=%llu, waits=%llu, timeouts=%llu"),
	       (unsigned long long)stats.total_acquires,
	       (unsigned long long)stats.total_releases,
	       (unsigned long long)stats.wait_count,
	       (unsigned long long)stats.timeout_count);
	Notice(_("  reconnects=%llu, validation_failures=%llu"),
	       (unsigned long long)stats.reconnect_count,
	       (unsigned long long)stats.validation_failures);
}
/*--- dbpool_status() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	DBPOOL_IS_READY
	Check if the pool is enabled and initialized.
**************************************************************************************************/
int
dbpool_is_ready(void) {
	return (db_pool_enabled && db_pool && db_pool->initialized && !db_pool->shutting_down);
}
/*--- dbpool_is_ready() -------------------------------------------------------------------------*/

/* vi:set ts=3: */
