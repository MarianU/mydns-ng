/**************************************************************************************************
	jsonlog.h: Structured JSON logging for MyDNS-NG

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

#ifndef JSONLOG_H
#define JSONLOG_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

/* Log levels */
typedef enum {
	JSONLOG_DEBUG   = 0,
	JSONLOG_INFO    = 1,
	JSONLOG_NOTICE  = 2,
	JSONLOG_WARNING = 3,
	JSONLOG_ERROR   = 4
} jsonlog_level_t;

/* Log entry structure for general messages */
typedef struct {
	char		timestamp[32];		/* ISO 8601 timestamp */
	jsonlog_level_t	level;			/* Log level */
	char		message[1024];		/* Log message */
	char		component[64];		/* Component name (mydns, query, etc.) */
	char		request_id[64];		/* Optional request ID */
	char		client_ip[64];		/* Optional client IP */
	int		pid;			/* Process ID */
} jsonlog_entry_t;

/* Query log entry structure */
typedef struct {
	char		timestamp[32];		/* ISO 8601 timestamp */
	char		qname[256];		/* Query name */
	char		qtype[16];		/* Query type (A, AAAA, MX, etc.) */
	char		client_ip[64];		/* Client IP address */
	char		rcode[16];		/* Response code (NOERROR, NXDOMAIN, etc.) */
	uint32_t	latency_ms;		/* Response latency in milliseconds */
	uint32_t	response_size;		/* Response size in bytes */
} jsonlog_query_t;

/* Configuration */
extern int		jsonlog_enabled;	/* Is JSON logging enabled? */
extern jsonlog_level_t	jsonlog_min_level;	/* Minimum log level to output */
extern int		jsonlog_query_enabled;	/* Is query logging enabled? */

/**************************************************************************************************
	FUNCTION DECLARATIONS
**************************************************************************************************/

/*
 * Initialize JSON logging subsystem
 * @param log_file  Path to log file, or NULL/empty for stdout
 * @return 0 on success, -1 on error
 */
extern int	jsonlog_init(const char *log_file);

/*
 * Clean up JSON logging subsystem
 */
extern void	jsonlog_cleanup(void);

/*
 * Write a general log entry
 * @param level      Log level
 * @param component  Component name (e.g., "mydns", "cache", "db")
 * @param fmt        Printf-style format string
 * @param ...        Format arguments
 */
extern void	jsonlog_write(jsonlog_level_t level, const char *component, 
			      const char *fmt, ...) __attribute__((__format__ (__printf__, 3, 4)));

/*
 * Write a query log entry
 * @param qname         Query name
 * @param qtype         Query type string
 * @param client_ip     Client IP address
 * @param rcode         Response code string
 * @param latency_ms    Response latency in milliseconds
 * @param response_size Response size in bytes
 */
extern void	jsonlog_query(const char *qname, const char *qtype, const char *client_ip,
			      const char *rcode, uint32_t latency_ms, uint32_t response_size);

/*
 * Convert syslog priority to jsonlog level
 * @param priority  Syslog priority (LOG_DEBUG, LOG_INFO, etc.)
 * @return Corresponding jsonlog level
 */
extern jsonlog_level_t jsonlog_level_from_syslog(int priority);

/*
 * Parse log level from string
 * @param level_str  Level string ("debug", "info", "notice", "warning", "error")
 * @return Log level, defaults to JSONLOG_NOTICE if invalid
 */
extern jsonlog_level_t jsonlog_parse_level(const char *level_str);

/*
 * Get log level name
 * @param level  Log level
 * @return Level name string
 */
extern const char *jsonlog_level_name(jsonlog_level_t level);

#endif /* JSONLOG_H */

/* vi:set ts=3: */
