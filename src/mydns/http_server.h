/**************************************************************************************************
	http_server.h: Embedded HTTP server for management endpoints

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

#ifndef _MYDNS_HTTP_SERVER_H
#define _MYDNS_HTTP_SERVER_H

#include "mydnsutil.h"

/* Default HTTP server configuration */
#define HTTP_DEFAULT_PORT	9100
#define HTTP_DEFAULT_LISTEN	"127.0.0.1"
#define HTTP_MAX_REQUEST_SIZE	4096
#define HTTP_MAX_RESPONSE_SIZE	65536

/* HTTP response status codes */
#define HTTP_STATUS_OK			200
#define HTTP_STATUS_BAD_REQUEST		400
#define HTTP_STATUS_NOT_FOUND		404
#define HTTP_STATUS_METHOD_NOT_ALLOWED	405
#define HTTP_STATUS_INTERNAL_ERROR	500
#define HTTP_STATUS_SERVICE_UNAVAIL	503

/* HTTP content types */
#define HTTP_CONTENT_TEXT_PLAIN		"text/plain; charset=utf-8"
#define HTTP_CONTENT_APP_JSON		"application/json"

/* HTTP connection state */
typedef enum _http_state_t {
	HTTP_STATE_READING,		/* Reading request */
	HTTP_STATE_WRITING,		/* Writing response */
	HTTP_STATE_DONE			/* Request complete */
} http_state_t;

/* HTTP request structure */
typedef struct _http_request {
	char		method[16];	/* HTTP method (GET, POST, etc.) */
	char		path[256];	/* Request path */
	char		version[16];	/* HTTP version */
	size_t		content_length;	/* Content-Length header value */
} HTTP_REQUEST;

/* HTTP connection structure (used as task extension) */
typedef struct _http_conn {
	http_state_t	state;			/* Connection state */
	char		*read_buf;		/* Read buffer */
	size_t		read_len;		/* Bytes read */
	size_t		read_size;		/* Buffer size */
	char		*write_buf;		/* Write buffer */
	size_t		write_len;		/* Total bytes to write */
	size_t		write_pos;		/* Bytes written so far */
	HTTP_REQUEST	request;		/* Parsed request */
} HTTP_CONN;

/* Configuration variables */
extern int		http_enabled;		/* Is HTTP server enabled? */
extern char		*http_listen;		/* Address to listen on */
extern int		http_port;		/* Port to listen on */

/* HTTP server functions */
extern void		http_server_init(void);
extern void		http_server_cleanup(void);
extern void		http_start(void);

/* Health check functions */
extern int		health_check_liveness(void);
extern int		health_check_readiness(char **reason);

#endif /* _MYDNS_HTTP_SERVER_H */

/* vi:set ts=3: */
