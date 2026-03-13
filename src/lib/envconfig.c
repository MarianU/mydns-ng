/**************************************************************************************************
	envconfig.c - Environment variable configuration support for container deployments

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
#include "envconfig.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_ENVCONFIG	1

/**
 * Static flag indicating if we're running in a container
 */
static int	in_container = -1;	/* -1 = not checked yet */

/**
 * Mapping table from config file option names to environment variable names.
 * All environment variables use the MYDNS_ prefix for consistency.
 */
static const ENVCONFIG_MAPPING envconfig_mappings[] = {
	/* Database configuration */
	{ "db-host",			"MYDNS_DB_HOST" },
	{ "db-user",			"MYDNS_DB_USER" },
	{ "db-password",		"MYDNS_DB_PASSWORD" },
	{ "database",			"MYDNS_DATABASE" },

	/* General options */
	{ "user",			"MYDNS_USER" },
	{ "group",			"MYDNS_GROUP" },
	{ "listen",			"MYDNS_LISTEN" },
	{ "no-listen",			"MYDNS_NO_LISTEN" },

	/* Cache options */
	{ "cache-size",			"MYDNS_CACHE_SIZE" },
	{ "cache-expire",		"MYDNS_CACHE_EXPIRE" },
	{ "zone-cache-size",		"MYDNS_ZONE_CACHE_SIZE" },
	{ "zone-cache-expire",		"MYDNS_ZONE_CACHE_EXPIRE" },
	{ "reply-cache-size",		"MYDNS_REPLY_CACHE_SIZE" },
	{ "reply-cache-expire",		"MYDNS_REPLY_CACHE_EXPIRE" },

	/* Database resilience */
	{ "stale-cache-ttl",		"MYDNS_STALE_CACHE_TTL" },
	{ "db-reconnect-base-interval",	"MYDNS_DB_RECONNECT_BASE_INTERVAL" },
	{ "db-reconnect-max-interval",	"MYDNS_DB_RECONNECT_MAX_INTERVAL" },
	{ "cache-warm-on-startup",	"MYDNS_CACHE_WARM_ON_STARTUP" },
	{ "cache-warm-on-reconnect",	"MYDNS_CACHE_WARM_ON_RECONNECT" },

	/* Esoterica */
	{ "log",			"MYDNS_LOG" },
	{ "pidfile",			"MYDNS_PIDFILE" },
	{ "timeout",			"MYDNS_TIMEOUT" },
	{ "servers",			"MYDNS_SERVERS" },
	{ "recursive",			"MYDNS_RECURSIVE" },
	{ "recursive-timeout",		"MYDNS_RECURSIVE_TIMEOUT" },
	{ "recursive-retries",		"MYDNS_RECURSIVE_RETRIES" },
	{ "recursive-algorithm",	"MYDNS_RECURSIVE_ALGORITHM" },
	{ "allow-axfr",			"MYDNS_ALLOW_AXFR" },
	{ "allow-tcp",			"MYDNS_ALLOW_TCP" },
	{ "allow-update",		"MYDNS_ALLOW_UPDATE" },
	{ "ignore-minimum",		"MYDNS_IGNORE_MINIMUM" },
	{ "soa-table",			"MYDNS_SOA_TABLE" },
	{ "rr-table",			"MYDNS_RR_TABLE" },
	{ "use-soa-active",		"MYDNS_USE_SOA_ACTIVE" },
	{ "use-rr-active",		"MYDNS_USE_RR_ACTIVE" },
	{ "shutdown-timeout",		"MYDNS_SHUTDOWN_TIMEOUT" },

	/* Notify configuration */
	{ "notify-enabled",		"MYDNS_NOTIFY_ENABLED" },
	{ "notify-source",		"MYDNS_NOTIFY_SOURCE" },
	{ "notify-source6",		"MYDNS_NOTIFY_SOURCE6" },
	{ "notify-timeout",		"MYDNS_NOTIFY_TIMEOUT" },
	{ "notify-retries",		"MYDNS_NOTIFY_RETRIES" },
	{ "notify-algorithm",		"MYDNS_NOTIFY_ALGORITHM" },

	/* Response Rate Limiting (RRL) */
	{ "rrl-enabled",		"MYDNS_RRL_ENABLED" },
	{ "rrl-responses-per-second",	"MYDNS_RRL_RESPONSES_PER_SECOND" },
	{ "rrl-window",			"MYDNS_RRL_WINDOW" },
	{ "rrl-slip",			"MYDNS_RRL_SLIP" },
	{ "rrl-ipv4-prefix",		"MYDNS_RRL_IPV4_PREFIX" },
	{ "rrl-ipv6-prefix",		"MYDNS_RRL_IPV6_PREFIX" },

	/* HTTP management server */
	{ "http-enabled",		"MYDNS_HTTP_ENABLED" },
	{ "http-listen",		"MYDNS_HTTP_LISTEN" },
	{ "http-port",			"MYDNS_HTTP_PORT" },

	/* IXFR configuration */
	{ "ixfr-enabled",		"MYDNS_IXFR_ENABLED" },
	{ "ixfr-gc-enabled",		"MYDNS_IXFR_GC_ENABLED" },
	{ "ixfr-gc-interval",		"MYDNS_IXFR_GC_INTERVAL" },
	{ "ixfr-gc-delay",		"MYDNS_IXFR_GC_DELAY" },

	/* Misc */
	{ "extended-data-support",	"MYDNS_EXTENDED_DATA_SUPPORT" },
	{ "dbengine",			"MYDNS_DBENGINE" },
	{ "wildcard-recursion",		"MYDNS_WILDCARD_RECURSION" },
	{ "soa-where",			"MYDNS_SOA_WHERE" },
	{ "rr-where",			"MYDNS_RR_WHERE" },

#ifdef DN_COLUMN_NAMES
	{ "default-ns",			"MYDNS_DEFAULT_NS" },
#endif

	/* Sentinel */
	{ NULL,				NULL }
};


/**************************************************************************************************
	ENVCONFIG_IS_CONTAINER
	Check if running inside a container (Docker, Podman, Kubernetes, etc.)
**************************************************************************************************/
int
envconfig_is_container(void) {
	if (in_container < 0) {
		/* Check for Docker */
		if (access("/.dockerenv", F_OK) == 0) {
			in_container = 1;
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
			DebugX("envconfig", 1, _("Container detected: Docker (/.dockerenv exists)"));
#endif
			return in_container;
		}

		/* Check for Podman/CRI-O */
		if (access("/run/.containerenv", F_OK) == 0) {
			in_container = 1;
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
			DebugX("envconfig", 1, _("Container detected: Podman/CRI-O (/run/.containerenv exists)"));
#endif
			return in_container;
		}

		/* Check for Kubernetes */
		if (getenv("KUBERNETES_SERVICE_HOST") != NULL) {
			in_container = 1;
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
			DebugX("envconfig", 1, _("Container detected: Kubernetes (KUBERNETES_SERVICE_HOST set)"));
#endif
			return in_container;
		}

		/* Check for container environment variable (common in many runtimes) */
		if (getenv("container") != NULL) {
			in_container = 1;
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
			DebugX("envconfig", 1, _("Container detected: container env var set"));
#endif
			return in_container;
		}

		in_container = 0;
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
		DebugX("envconfig", 1, _("Not running in container"));
#endif
	}
	return in_container;
}
/*--- envconfig_is_container() ------------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_GET_ENV_VAR
	Get the environment variable name for a config key.
**************************************************************************************************/
const char *
envconfig_get_env_var(const char *config_key) {
	int i;

	if (!config_key)
		return NULL;

	for (i = 0; envconfig_mappings[i].config_key != NULL; i++) {
		if (strcasecmp(envconfig_mappings[i].config_key, config_key) == 0) {
			return envconfig_mappings[i].env_var;
		}
	}
	return NULL;
}
/*--- envconfig_get_env_var() -------------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_GET_CONFIG_KEY
	Get the config key for an environment variable name.
**************************************************************************************************/
const char *
envconfig_get_config_key(const char *env_var) {
	int i;

	if (!env_var)
		return NULL;

	for (i = 0; envconfig_mappings[i].env_var != NULL; i++) {
		if (strcasecmp(envconfig_mappings[i].env_var, env_var) == 0) {
			return envconfig_mappings[i].config_key;
		}
	}
	return NULL;
}
/*--- envconfig_get_config_key() ----------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_READ_PASSWORD_FILE
	Read password from a file (for Kubernetes secrets, Docker secrets, etc.)
	Returns allocated string that caller must free, or NULL on error.
**************************************************************************************************/
char *
envconfig_read_password_file(const char *filepath) {
	FILE *fp;
	char buf[1024];
	char *password = NULL;
	size_t len;

	if (!filepath || !filepath[0]) {
		return NULL;
	}

	fp = fopen(filepath, "r");
	if (!fp) {
		Warn("%s: %s", filepath, _("Cannot read password file"));
		return NULL;
	}

	if (fgets(buf, sizeof(buf), fp) != NULL) {
		/* Remove trailing newline/whitespace */
		len = strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || 
		                   buf[len-1] == ' ' || buf[len-1] == '\t')) {
			buf[--len] = '\0';
		}
		if (len > 0) {
			password = STRDUP(buf);
		}
	}

	/* Clear buffer before closing */
	memset(buf, 0, sizeof(buf));
	fclose(fp);

#if DEBUG_ENABLED && DEBUG_ENVCONFIG
	if (password) {
		DebugX("envconfig", 1, _("Read password from file: %s"), filepath);
	}
#endif

	return password;
}
/*--- envconfig_read_password_file() ------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_APPLY_CONTAINER_DEFAULTS
	Apply container-friendly defaults when running in a container.
**************************************************************************************************/
void
envconfig_apply_container_defaults(CONF **confp) {
	int defaulted;

	if (!envconfig_is_container()) {
		return;
	}

	Verbose(_("Running in container - applying container-friendly defaults"));

	/* Log to stdout for container log aggregation (unless explicitly set) */
	if (conf_get(confp, "log", &defaulted) && defaulted) {
		conf_set(confp, "log", "stdout", 1);
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
		DebugX("envconfig", 1, _("Container default: log=stdout"));
#endif
	}

	/* Disable PID file (not needed in containers, and /var/run may not be writable) */
	if (conf_get(confp, "pidfile", &defaulted) && defaulted) {
		conf_set(confp, "pidfile", "", 1);
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
		DebugX("envconfig", 1, _("Container default: pidfile disabled"));
#endif
	}

	/* Enable HTTP health endpoint by default for readiness/liveness probes */
	if (conf_get(confp, "http-enabled", &defaulted) && defaulted) {
		conf_set(confp, "http-enabled", "yes", 1);
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
		DebugX("envconfig", 1, _("Container default: http-enabled=yes"));
#endif
	}

	/* Listen on all interfaces for HTTP (for Kubernetes probes) */
	if (conf_get(confp, "http-listen", &defaulted) && defaulted) {
		conf_set(confp, "http-listen", "0.0.0.0", 1);
#if DEBUG_ENABLED && DEBUG_ENVCONFIG
		DebugX("envconfig", 1, _("Container default: http-listen=0.0.0.0"));
#endif
	}
}
/*--- envconfig_apply_container_defaults() ------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_OVERRIDE_CONF
	Override config values with environment variable values.
	Returns the number of overrides applied.
**************************************************************************************************/
int
envconfig_override_conf(CONF **confp) {
	int i;
	int overrides = 0;
	int defaulted;
	const char *env_value;
	const char *current_value;
	char *password;

	/* First, check for password file (takes precedence over MYDNS_DB_PASSWORD) */
	env_value = getenv(ENVCONFIG_PASSWORD_FILE_VAR);
	if (env_value && env_value[0]) {
		password = envconfig_read_password_file(env_value);
		if (password) {
			conf_clobber(confp, "db-password", password);
			/* Clear and free the password string */
			memset(password, 0, strlen(password));
			RELEASE(password);
			overrides++;
			Verbose(_("Database password loaded from file: %s"), env_value);
		}
	}

	/* Process all environment variable mappings */
	for (i = 0; envconfig_mappings[i].config_key != NULL; i++) {
		env_value = getenv(envconfig_mappings[i].env_var);
		if (env_value && env_value[0]) {
			/* Environment variable is set and non-empty */
			/* Check if current value is a default - if so, clobber it */
			current_value = conf_get(confp, envconfig_mappings[i].config_key, &defaulted);
			if (defaulted || !current_value) {
				conf_clobber(confp, envconfig_mappings[i].config_key, env_value);
			}
			/* Also add as a new entry with defaulted=0 to ensure it takes precedence */
			/* conf_set adds to front of list, conf_get searches from front */
			conf_set(confp, envconfig_mappings[i].config_key, env_value, 0);
			overrides++;

#if DEBUG_ENABLED && DEBUG_ENVCONFIG
			/* Don't log password values */
			if (strcasecmp(envconfig_mappings[i].config_key, "db-password") == 0) {
				DebugX("envconfig", 1, _("Env override: %s=*** (from %s)"),
				       envconfig_mappings[i].config_key,
				       envconfig_mappings[i].env_var);
			} else {
				DebugX("envconfig", 1, _("Env override: %s=%s (from %s)"),
				       envconfig_mappings[i].config_key, env_value,
				       envconfig_mappings[i].env_var);
			}
#endif
		}
	}

	if (overrides > 0) {
		Verbose(_("Applied %d configuration override%s from environment variables"),
		        overrides, S(overrides));
	}

	return overrides;
}
/*--- envconfig_override_conf() -----------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_INIT
	Initialize the environment configuration system.
**************************************************************************************************/
void
envconfig_init(void) {
	/* Check for container environment (caches result) */
	envconfig_is_container();

#if DEBUG_ENABLED && DEBUG_ENVCONFIG
	DebugX("envconfig", 1, _("Environment configuration initialized (container=%s)"),
	       in_container ? "yes" : "no");
#endif
}
/*--- envconfig_init() --------------------------------------------------------------------------*/


/**************************************************************************************************
	ENVCONFIG_DUMP_CONFIG
	Dump effective configuration showing value sources.
**************************************************************************************************/
void
envconfig_dump_config(CONF **confp) {
	int i;
	int defaulted;
	const char *value;
	const char *env_var;
	const char *env_value;
	const char *source;
	time_t time_now = time(NULL);

	printf("##\n");
	printf("##  Effective Configuration\n");
	printf("##  %.24s\n", ctime(&time_now));
	printf("##  Container: %s\n", envconfig_is_container() ? "yes" : "no");
	printf("##\n\n");

	printf("%-30s %-40s %s\n", "# Option", "Value", "Source");
	printf("%-30s %-40s %s\n", "# ------", "-----", "------");

	for (i = 0; envconfig_mappings[i].config_key != NULL; i++) {
		env_var = envconfig_mappings[i].env_var;
		env_value = getenv(env_var);
		value = conf_get(confp, envconfig_mappings[i].config_key, &defaulted);

		/* Determine source */
		if (env_value && env_value[0]) {
			source = env_var;
		} else if (defaulted) {
			source = "default";
		} else {
			source = "config file";
		}

		/* Mask password values */
		if (strcasecmp(envconfig_mappings[i].config_key, "db-password") == 0) {
			printf("%-30s %-40s %s\n", envconfig_mappings[i].config_key,
			       value ? "***" : "(not set)", source);
		} else {
			printf("%-30s %-40s %s\n", envconfig_mappings[i].config_key,
			       value ? value : "(not set)", source);
		}
	}

	printf("\n");
}
/*--- envconfig_dump_config() -------------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */
