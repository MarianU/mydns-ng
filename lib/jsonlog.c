/**************************************************************************************************
	jsonlog.c: Structured JSON logging for MyDNS-NG

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

#include "mydnsutil.h"
#include "jsonlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_JSONLOG	1

/* Maximum buffer size for JSON output */
#define JSONLOG_BUFSIZE		4096

/* Global state */
int			jsonlog_enabled = 0;
jsonlog_level_t		jsonlog_min_level = JSONLOG_NOTICE;
int			jsonlog_query_enabled = 0;

static FILE		*jsonlog_file = NULL;
static pthread_mutex_t	jsonlog_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Level name lookup table */
static const char *level_names[] = {
	"debug",
	"info",
	"notice",
	"warning",
	"error"
};


/**************************************************************************************************
	JSONLOG_ESCAPE_STRING
	Escape a string for JSON output. Special characters are escaped per RFC 8259.
	Returns number of characters written (excluding null terminator), or -1 on error.
**************************************************************************************************/
static int
jsonlog_escape_string(char *dest, size_t dest_size, const char *src) {
	size_t di = 0;
	const char *s;

	if (!dest || dest_size == 0)
		return -1;
	if (!src) {
		dest[0] = '\0';
		return 0;
	}

	for (s = src; *s && di < dest_size - 1; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '"':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = '"';
			break;
		case '\\':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = '\\';
			break;
		case '\b':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = 'b';
			break;
		case '\f':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = 'f';
			break;
		case '\n':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = 'n';
			break;
		case '\r':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = 'r';
			break;
		case '\t':
			if (di + 2 >= dest_size) goto truncate;
			dest[di++] = '\\';
			dest[di++] = 't';
			break;
		default:
			/* Escape control characters as \uXXXX */
			if (c < 0x20) {
				if (di + 6 >= dest_size) goto truncate;
				di += snprintf(dest + di, dest_size - di, "\\u%04x", c);
			} else {
				dest[di++] = c;
			}
			break;
		}
	}

truncate:
	dest[di] = '\0';
	return (int)di;
}
/*--- jsonlog_escape_string() -------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_FORMAT_TIMESTAMP
	Format current time as ISO 8601 timestamp with milliseconds.
	Example: 2024-01-15T10:30:45.123Z
**************************************************************************************************/
static void
jsonlog_format_timestamp(char *buf, size_t bufsize) {
	struct timeval tv;
	struct tm *tm_info;
	char time_str[32];

	gettimeofday(&tv, NULL);
	tm_info = gmtime(&tv.tv_sec);

	strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
	snprintf(buf, bufsize, "%s.%03ldZ", time_str, tv.tv_usec / 1000);
}
/*--- jsonlog_format_timestamp() ----------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_INIT
	Initialize JSON logging subsystem.
	@param log_file  Path to log file, or NULL/empty for stdout
	@return 0 on success, -1 on error
**************************************************************************************************/
int
jsonlog_init(const char *log_file) {
	pthread_mutex_lock(&jsonlog_mutex);

	/* Close existing log file if open */
	if (jsonlog_file && jsonlog_file != stdout && jsonlog_file != stderr) {
		fclose(jsonlog_file);
		jsonlog_file = NULL;
	}

	/* Open new log file or use stdout */
	if (!log_file || log_file[0] == '\0') {
		jsonlog_file = stdout;
	} else {
		jsonlog_file = fopen(log_file, "a");
		if (!jsonlog_file) {
			pthread_mutex_unlock(&jsonlog_mutex);
			return -1;
		}
		/* Set line buffering for log file */
		setvbuf(jsonlog_file, NULL, _IOLBF, 0);
	}

	jsonlog_enabled = 1;

	pthread_mutex_unlock(&jsonlog_mutex);
	return 0;
}
/*--- jsonlog_init() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_CLEANUP
	Clean up JSON logging subsystem.
**************************************************************************************************/
void
jsonlog_cleanup(void) {
	pthread_mutex_lock(&jsonlog_mutex);

	if (jsonlog_file && jsonlog_file != stdout && jsonlog_file != stderr) {
		fclose(jsonlog_file);
	}
	jsonlog_file = NULL;
	jsonlog_enabled = 0;

	pthread_mutex_unlock(&jsonlog_mutex);
}
/*--- jsonlog_cleanup() -------------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_WRITE
	Write a general log entry in JSON format.
**************************************************************************************************/
void
jsonlog_write(jsonlog_level_t level, const char *component, const char *fmt, ...) {
	char buf[JSONLOG_BUFSIZE];
	char timestamp[32];
	char message[1024];
	char escaped_message[2048];
	char escaped_component[128];
	va_list ap;
	int len;

	/* Check if logging is enabled and level is sufficient */
	if (!jsonlog_enabled || !jsonlog_file || level < jsonlog_min_level)
		return;

	/* Format the message */
	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	/* Escape strings for JSON */
	jsonlog_escape_string(escaped_message, sizeof(escaped_message), message);
	jsonlog_escape_string(escaped_component, sizeof(escaped_component), 
			      component ? component : "mydns");

	/* Format timestamp */
	jsonlog_format_timestamp(timestamp, sizeof(timestamp));

	/* Build JSON object */
	len = snprintf(buf, sizeof(buf),
		       "{\"timestamp\":\"%s\",\"level\":\"%s\",\"component\":\"%s\","
		       "\"message\":\"%s\",\"pid\":%d}\n",
		       timestamp,
		       level_names[level],
		       escaped_component,
		       escaped_message,
		       (int)getpid());

	/* Write to log file (thread-safe) */
	pthread_mutex_lock(&jsonlog_mutex);
	if (jsonlog_file && len > 0 && (size_t)len < sizeof(buf)) {
		fputs(buf, jsonlog_file);
		fflush(jsonlog_file);
	}
	pthread_mutex_unlock(&jsonlog_mutex);
}
/*--- jsonlog_write() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_QUERY
	Write a query log entry in JSON format.
**************************************************************************************************/
void
jsonlog_query(const char *qname, const char *qtype, const char *client_ip,
	      const char *rcode, uint32_t latency_ms, uint32_t response_size) {
	char buf[JSONLOG_BUFSIZE];
	char timestamp[32];
	char escaped_qname[512];
	char escaped_qtype[32];
	char escaped_client[128];
	char escaped_rcode[32];
	int len;

	/* Check if query logging is enabled */
	if (!jsonlog_enabled || !jsonlog_file || !jsonlog_query_enabled)
		return;

	/* Escape strings for JSON */
	jsonlog_escape_string(escaped_qname, sizeof(escaped_qname), qname ? qname : "");
	jsonlog_escape_string(escaped_qtype, sizeof(escaped_qtype), qtype ? qtype : "");
	jsonlog_escape_string(escaped_client, sizeof(escaped_client), client_ip ? client_ip : "");
	jsonlog_escape_string(escaped_rcode, sizeof(escaped_rcode), rcode ? rcode : "");

	/* Format timestamp */
	jsonlog_format_timestamp(timestamp, sizeof(timestamp));

	/* Build JSON object for query log */
	len = snprintf(buf, sizeof(buf),
		       "{\"timestamp\":\"%s\",\"level\":\"info\",\"component\":\"query\","
		       "\"qname\":\"%s\",\"qtype\":\"%s\",\"client\":\"%s\","
		       "\"rcode\":\"%s\",\"latency_ms\":%u,\"size\":%u}\n",
		       timestamp,
		       escaped_qname,
		       escaped_qtype,
		       escaped_client,
		       escaped_rcode,
		       latency_ms,
		       response_size);

	/* Write to log file (thread-safe) */
	pthread_mutex_lock(&jsonlog_mutex);
	if (jsonlog_file && len > 0 && (size_t)len < sizeof(buf)) {
		fputs(buf, jsonlog_file);
		fflush(jsonlog_file);
	}
	pthread_mutex_unlock(&jsonlog_mutex);
}
/*--- jsonlog_query() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_LEVEL_FROM_SYSLOG
	Convert syslog priority to jsonlog level.
**************************************************************************************************/
jsonlog_level_t
jsonlog_level_from_syslog(int priority) {
	switch (priority) {
	case LOG_DEBUG:
		return JSONLOG_DEBUG;
	case LOG_INFO:
		return JSONLOG_INFO;
	case LOG_NOTICE:
		return JSONLOG_NOTICE;
	case LOG_WARNING:
		return JSONLOG_WARNING;
	case LOG_ERR:
	case LOG_CRIT:
	case LOG_ALERT:
	case LOG_EMERG:
		return JSONLOG_ERROR;
	default:
		return JSONLOG_NOTICE;
	}
}
/*--- jsonlog_level_from_syslog() ---------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_PARSE_LEVEL
	Parse log level from string.
**************************************************************************************************/
jsonlog_level_t
jsonlog_parse_level(const char *level_str) {
	if (!level_str)
		return JSONLOG_NOTICE;

	if (!strcasecmp(level_str, "debug"))
		return JSONLOG_DEBUG;
	if (!strcasecmp(level_str, "info"))
		return JSONLOG_INFO;
	if (!strcasecmp(level_str, "notice"))
		return JSONLOG_NOTICE;
	if (!strcasecmp(level_str, "warning") || !strcasecmp(level_str, "warn"))
		return JSONLOG_WARNING;
	if (!strcasecmp(level_str, "error") || !strcasecmp(level_str, "err"))
		return JSONLOG_ERROR;

	return JSONLOG_NOTICE;
}
/*--- jsonlog_parse_level() ---------------------------------------------------------------------*/


/**************************************************************************************************
	JSONLOG_LEVEL_NAME
	Get log level name string.
**************************************************************************************************/
const char *
jsonlog_level_name(jsonlog_level_t level) {
	if (level >= JSONLOG_DEBUG && level <= JSONLOG_ERROR)
		return level_names[level];
	return "unknown";
}
/*--- jsonlog_level_name() ----------------------------------------------------------------------*/

/* vi:set ts=3: */
