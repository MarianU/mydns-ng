/**************************************************************************************************
	http_server.c: Embedded HTTP server for management endpoints

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
#include "http_server.h"
#include "metrics.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_HTTP	1

/* Global configuration variables */
int		http_enabled = 0;
char		*http_listen = NULL;
int		http_port = HTTP_DEFAULT_PORT;

/* HTTP listening socket */
static int	http_fd = -1;


/**************************************************************************************************
	HTTP_CONN_FREE
	Free HTTP connection extension data.
**************************************************************************************************/
static void
http_conn_free(TASK *t, void *data) {
	HTTP_CONN *conn = (HTTP_CONN *)data;
	
	if (conn) {
		if (conn->read_buf)
			RELEASE(conn->read_buf);
		if (conn->write_buf)
			RELEASE(conn->write_buf);
		RELEASE(conn);
	}
}
/*--- http_conn_free() --------------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_BUILD_RESPONSE
	Build an HTTP response with the given status, content type, and body.
**************************************************************************************************/
static char *
http_build_response(int status, const char *content_type, const char *body, size_t body_len, size_t *out_len) {
	char *response = NULL;
	const char *status_text = NULL;
	size_t header_len;
	char header[1024];
	
	switch (status) {
	case HTTP_STATUS_OK:			status_text = "OK"; break;
	case HTTP_STATUS_BAD_REQUEST:		status_text = "Bad Request"; break;
	case HTTP_STATUS_NOT_FOUND:		status_text = "Not Found"; break;
	case HTTP_STATUS_METHOD_NOT_ALLOWED:	status_text = "Method Not Allowed"; break;
	case HTTP_STATUS_INTERNAL_ERROR:	status_text = "Internal Server Error"; break;
	case HTTP_STATUS_SERVICE_UNAVAIL:	status_text = "Service Unavailable"; break;
	default:				status_text = "Unknown"; break;
	}
	
	header_len = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lu\r\n"
		"Connection: close\r\n"
		"Server: MyDNS-NG\r\n"
		"\r\n",
		status, status_text, content_type, (unsigned long)body_len);
	
	*out_len = header_len + body_len;
	response = ALLOCATE(*out_len + 1, char[]);
	memcpy(response, header, header_len);
	if (body && body_len > 0) {
		memcpy(response + header_len, body, body_len);
	}
	response[*out_len] = '\0';
	
	return response;
}
/*--- http_build_response() ---------------------------------------------------------------------*/


/**************************************************************************************************
	HEALTH_CHECK_LIVENESS
	Check if the server process is alive. Returns 1 if alive, 0 if not.
**************************************************************************************************/
int
health_check_liveness(void) {
	/* If we can execute this code, the process is alive */
	return 1;
}
/*--- health_check_liveness() -------------------------------------------------------------------*/


/**************************************************************************************************
	HEALTH_CHECK_READINESS
	Check if the server is ready to serve requests.
	Returns 1 if ready, 0 if not. Sets reason if not ready.
**************************************************************************************************/
int
health_check_readiness(char **reason) {
	/* Check database connection */
	if (!sql) {
		if (reason)
			*reason = STRDUP("database not connected");
		return 0;
	}
	
	/* Try a simple query to verify connection is alive */
#if USE_PGSQL
	{
		PGresult *res = PQexec(sql, "SELECT 1");
		if (PQresultStatus(res) != PGRES_TUPLES_OK) {
			if (reason)
				*reason = STRDUP("database connection failed");
			PQclear(res);
			return 0;
		}
		PQclear(res);
	}
#else
	{
		if (mysql_ping(sql) != 0) {
			if (reason)
				*reason = STRDUP("database connection failed");
			return 0;
		}
	}
#endif
	
	return 1;
}
/*--- health_check_readiness() ------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_HANDLE_REQUEST
	Process an HTTP request and build the response.
**************************************************************************************************/
static void
http_handle_request(HTTP_CONN *conn) {
	char *body = NULL;
	size_t body_len = 0;
	int status = HTTP_STATUS_OK;
	const char *content_type = HTTP_CONTENT_TEXT_PLAIN;
	
	/* Only support GET method */
	if (strcmp(conn->request.method, "GET") != 0) {
		status = HTTP_STATUS_METHOD_NOT_ALLOWED;
		body = STRDUP("Method Not Allowed\n");
		body_len = strlen(body);
		goto respond;
	}
	
	/* Route requests to handlers */
	if (strcmp(conn->request.path, "/metrics") == 0) {
		/* Prometheus metrics endpoint */
		body = metrics_format_prometheus(&body_len);
		if (!body) {
			status = HTTP_STATUS_INTERNAL_ERROR;
			body = STRDUP("Metrics not available\n");
			body_len = strlen(body);
		}
	} else if (strcmp(conn->request.path, "/healthz") == 0) {
		/* Liveness probe */
		content_type = HTTP_CONTENT_APP_JSON;
		if (health_check_liveness()) {
			status = HTTP_STATUS_OK;
			body = STRDUP("{\"status\":\"ok\"}\n");
		} else {
			status = HTTP_STATUS_SERVICE_UNAVAIL;
			body = STRDUP("{\"status\":\"unhealthy\",\"reason\":\"process not responding\"}\n");
		}
		body_len = strlen(body);
	} else if (strcmp(conn->request.path, "/readyz") == 0) {
		/* Readiness probe */
		char *reason = NULL;
		content_type = HTTP_CONTENT_APP_JSON;
		if (health_check_readiness(&reason)) {
			status = HTTP_STATUS_OK;
			body = STRDUP("{\"status\":\"ok\"}\n");
			body_len = strlen(body);
		} else {
			char buf[512];
			status = HTTP_STATUS_SERVICE_UNAVAIL;
			snprintf(buf, sizeof(buf), "{\"status\":\"unhealthy\",\"reason\":\"%s\"}\n",
				reason ? reason : "unknown");
			body = STRDUP(buf);
			body_len = strlen(body);
			if (reason)
				RELEASE(reason);
		}
	} else {
		/* Not found */
		status = HTTP_STATUS_NOT_FOUND;
		body = STRDUP("Not Found\n");
		body_len = strlen(body);
	}

respond:
	conn->write_buf = http_build_response(status, content_type, body, body_len, &conn->write_len);
	conn->write_pos = 0;
	conn->state = HTTP_STATE_WRITING;
	
	if (body)
		RELEASE(body);
}
/*--- http_handle_request() ---------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_PARSE_REQUEST
	Parse an HTTP request from the read buffer.
	Returns 1 if complete request parsed, 0 if more data needed, -1 on error.
**************************************************************************************************/
static int
http_parse_request(HTTP_CONN *conn) {
	char *line_end = NULL;
	char *method = NULL, *path = NULL, *version = NULL;
	char *saveptr = NULL;
	char line[1024];
	
	/* Look for end of headers (double CRLF) */
	if (!strstr(conn->read_buf, "\r\n\r\n"))
		return 0; /* Need more data */
	
	/* Parse request line */
	line_end = strstr(conn->read_buf, "\r\n");
	if (!line_end || (size_t)(line_end - conn->read_buf) >= sizeof(line) - 1)
		return -1;
	
	memcpy(line, conn->read_buf, line_end - conn->read_buf);
	line[line_end - conn->read_buf] = '\0';
	
	/* Parse method, path, version */
	method = strtok_r(line, " ", &saveptr);
	path = strtok_r(NULL, " ", &saveptr);
	version = strtok_r(NULL, " ", &saveptr);
	
	if (!method || !path || !version)
		return -1;
	
	strncpy(conn->request.method, method, sizeof(conn->request.method) - 1);
	strncpy(conn->request.path, path, sizeof(conn->request.path) - 1);
	strncpy(conn->request.version, version, sizeof(conn->request.version) - 1);
	
#if DEBUG_ENABLED && DEBUG_HTTP
	DebugX("http", 1, _("HTTP request: %s %s %s"), 
		conn->request.method, conn->request.path, conn->request.version);
#endif
	
	return 1;
}
/*--- http_parse_request() ----------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_READ
	Read data from HTTP connection.
**************************************************************************************************/
static taskexec_t
http_read(TASK *t, void *data) {
	HTTP_CONN *conn = (HTTP_CONN *)data;
	ssize_t n;
	int parse_result;
	
	if (!conn || conn->state != HTTP_STATE_READING)
		return TASK_FAILED;
	
	/* Ensure we have buffer space */
	if (conn->read_len >= conn->read_size - 1) {
		if (conn->read_size >= HTTP_MAX_REQUEST_SIZE)
			return TASK_FAILED; /* Request too large */
		conn->read_size *= 2;
		if (conn->read_size > HTTP_MAX_REQUEST_SIZE)
			conn->read_size = HTTP_MAX_REQUEST_SIZE;
		conn->read_buf = REALLOCATE(conn->read_buf, conn->read_size, char[]);
	}
	
	/* Read data */
	n = read(t->fd, conn->read_buf + conn->read_len, conn->read_size - conn->read_len - 1);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return TASK_CONTINUE;
		return TASK_FAILED;
	}
	if (n == 0)
		return TASK_FAILED; /* Connection closed */
	
	conn->read_len += n;
	conn->read_buf[conn->read_len] = '\0';
	
	/* Try to parse request */
	parse_result = http_parse_request(conn);
	if (parse_result < 0)
		return TASK_FAILED;
	if (parse_result == 0)
		return TASK_CONTINUE; /* Need more data */
	
	/* Request complete, handle it */
	http_handle_request(conn);
	
	/* Change task to write mode */
	t->status = NEED_WRITE;
	return TASK_CONTINUE;
}
/*--- http_read() -------------------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_WRITE
	Write data to HTTP connection.
**************************************************************************************************/
static taskexec_t
http_write(TASK *t, void *data) {
	HTTP_CONN *conn = (HTTP_CONN *)data;
	ssize_t n;
	
	if (!conn || conn->state != HTTP_STATE_WRITING)
		return TASK_FAILED;
	
	if (!conn->write_buf || conn->write_pos >= conn->write_len) {
		conn->state = HTTP_STATE_DONE;
		return TASK_COMPLETED;
	}
	
	n = write(t->fd, conn->write_buf + conn->write_pos, conn->write_len - conn->write_pos);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return TASK_CONTINUE;
		return TASK_FAILED;
	}
	
	conn->write_pos += n;
	
	if (conn->write_pos >= conn->write_len) {
		conn->state = HTTP_STATE_DONE;
		return TASK_COMPLETED;
	}
	
	return TASK_CONTINUE;
}
/*--- http_write() ------------------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_RUN
	Main run function for HTTP connection task.
**************************************************************************************************/
static taskexec_t
http_run(TASK *t, void *data) {
	HTTP_CONN *conn = (HTTP_CONN *)data;
	
	if (!conn)
		return TASK_FAILED;
	
	switch (conn->state) {
	case HTTP_STATE_READING:
		return http_read(t, data);
	case HTTP_STATE_WRITING:
		return http_write(t, data);
	case HTTP_STATE_DONE:
		return TASK_COMPLETED;
	default:
		return TASK_FAILED;
	}
}
/*--- http_run() --------------------------------------------------------------------------------*/


/**************************************************************************************************
	ACCEPT_HTTP_CONNECTION
	Accept a new HTTP connection and create a task for it.
**************************************************************************************************/
static void
accept_http_connection(int listen_fd) {
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	int client_fd;
	TASK *t;
	HTTP_CONN *conn;
	
	memset(&addr, 0, sizeof(addr));
	
	client_fd = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
	if (client_fd < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			Warn(_("accept_http_connection: accept failed: %s"), strerror(errno));
		return;
	}
	
	/* Set non-blocking */
	fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
	
#if DEBUG_ENABLED && DEBUG_HTTP
	DebugX("http", 1, _("HTTP connection from %s:%d"), 
		inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
#endif
	
	/* Create task for this connection */
	t = IOtask_init(LOW_PRIORITY_TASK, NEED_READ, client_fd, SOCK_STREAM, AF_INET, &addr);
	if (!t) {
		close(client_fd);
		return;
	}
	
	/* Create connection extension */
	conn = ALLOCATE(sizeof(HTTP_CONN), HTTP_CONN);
	memset(conn, 0, sizeof(HTTP_CONN));
	conn->state = HTTP_STATE_READING;
	conn->read_size = 1024;
	conn->read_buf = ALLOCATE(conn->read_size, char[]);
	conn->read_buf[0] = '\0';
	
	task_add_extension(t, conn, http_conn_free, http_run, NULL);
	
	/* Set timeout */
	t->timeout = current_time + 30; /* 30 second timeout */
	
	enqueue(&TaskArray[IO_TASK][LOW_PRIORITY_TASK], t);
}
/*--- accept_http_connection() ------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_LISTENER_RUN
	Run function for HTTP listener task.
**************************************************************************************************/
static taskexec_t
http_listener_run(TASK *t, void *data) {
	(void)data; /* Unused */
	
	accept_http_connection(t->fd);
	return TASK_CONTINUE;
}
/*--- http_listener_run() -----------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_CREATE_LISTENER
	Create the HTTP listening socket.
**************************************************************************************************/
static int
http_create_listener(void) {
	struct sockaddr_in sa;
	int opt = 1;
	
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(http_port);
	
	if (http_listen && strcmp(http_listen, "*") != 0) {
		if (inet_pton(AF_INET, http_listen, &sa.sin_addr) <= 0) {
			Warnx(_("http_create_listener: invalid listen address: %s"), http_listen);
			return -1;
		}
	} else {
		sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	
	http_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (http_fd < 0) {
		Warn(_("http_create_listener: socket failed"));
		return -1;
	}
	
	if (setsockopt(http_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		Warn(_("http_create_listener: setsockopt SO_REUSEADDR failed"));
	}
	
	fcntl(http_fd, F_SETFL, fcntl(http_fd, F_GETFL, 0) | O_NONBLOCK);
	
	if (bind(http_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		Warn(_("http_create_listener: bind failed on %s:%d"), 
			http_listen ? http_listen : "127.0.0.1", http_port);
		close(http_fd);
		http_fd = -1;
		return -1;
	}
	
	if (listen(http_fd, SOMAXCONN) < 0) {
		Warn(_("http_create_listener: listen failed"));
		close(http_fd);
		http_fd = -1;
		return -1;
	}
	
	Notice(_("HTTP server listening on %s:%d"), 
		http_listen ? http_listen : "127.0.0.1", http_port);
	
	return 0;
}
/*--- http_create_listener() --------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_SERVER_INIT
	Initialize the HTTP server (load configuration).
**************************************************************************************************/
void
http_server_init(void) {
	const char *val;
	
	/* Check if HTTP server is enabled */
	val = conf_get(&Conf, "http-enabled", NULL);
	http_enabled = val ? GETBOOL(val) : 0;
	
	if (!http_enabled)
		return;
	
	/* Get listen address */
	val = conf_get(&Conf, "http-listen", NULL);
	if (val && val[0]) {
		if (http_listen)
			RELEASE(http_listen);
		http_listen = STRDUP(val);
	} else {
		http_listen = STRDUP(HTTP_DEFAULT_LISTEN);
	}
	
	/* Get port */
	val = conf_get(&Conf, "http-port", NULL);
	if (val && atoi(val) > 0)
		http_port = atoi(val);
	else
		http_port = HTTP_DEFAULT_PORT;
	
	/* Initialize metrics */
	metrics_init();
	
#if DEBUG_ENABLED && DEBUG_HTTP
	DebugX("http", 1, _("HTTP server configured: listen=%s port=%d"), 
		http_listen, http_port);
#endif
}
/*--- http_server_init() ------------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_SERVER_CLEANUP
	Clean up HTTP server resources.
**************************************************************************************************/
void
http_server_cleanup(void) {
	if (http_fd >= 0) {
		close(http_fd);
		http_fd = -1;
	}
	
	if (http_listen) {
		RELEASE(http_listen);
		http_listen = NULL;
	}
	
	metrics_cleanup();
}
/*--- http_server_cleanup() ---------------------------------------------------------------------*/


/**************************************************************************************************
	HTTP_START
	Start the HTTP server (create listener and initial task).
**************************************************************************************************/
void
http_start(void) {
	TASK *t;
	
	if (!http_enabled)
		return;
	
	if (http_create_listener() < 0) {
		Warnx(_("HTTP server failed to start"));
		return;
	}
	
	/* Create listener task */
	t = IOtask_init(LOW_PRIORITY_TASK, NEED_READ, http_fd, SOCK_STREAM, AF_INET, NULL);
	if (!t) {
		close(http_fd);
		http_fd = -1;
		Warnx(_("HTTP server: failed to create listener task"));
		return;
	}
	
	task_add_extension(t, NULL, NULL, http_listener_run, NULL);
	
	enqueue(&TaskArray[IO_TASK][LOW_PRIORITY_TASK], t);
	
#if DEBUG_ENABLED && DEBUG_HTTP
	DebugX("http", 1, _("HTTP server started"));
#endif
}
/*--- http_start() ------------------------------------------------------------------------------*/

/* vi:set ts=3: */
