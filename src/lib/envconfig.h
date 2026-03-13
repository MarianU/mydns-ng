/**************************************************************************************************
	envconfig.h - Environment variable configuration support for container deployments

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

#ifndef _ENVCONFIG_H
#define _ENVCONFIG_H

#include "mydnsutil.h"

/**
 * Mapping from config file option name to environment variable name
 */
typedef struct _envconfig_mapping {
	const char	*config_key;	/* Config file option name (e.g., "db-host") */
	const char	*env_var;	/* Environment variable name (e.g., "MYDNS_DB_HOST") */
} ENVCONFIG_MAPPING;

/**
 * Source of a configuration value
 */
typedef enum _config_source {
	CONFIG_SOURCE_DEFAULT = 0,	/* Default value */
	CONFIG_SOURCE_FILE,		/* From config file */
	CONFIG_SOURCE_ENV,		/* From environment variable */
	CONFIG_SOURCE_CMDLINE		/* From command line */
} config_source_t;

/**
 * Initialize environment configuration system.
 * Detects container environment and sets up container-friendly defaults.
 */
extern void		envconfig_init(void);

/**
 * Get the environment variable name for a config key.
 * Returns NULL if no mapping exists.
 */
extern const char	*envconfig_get_env_var(const char *config_key);

/**
 * Get the config key for an environment variable name.
 * Returns NULL if no mapping exists.
 */
extern const char	*envconfig_get_config_key(const char *env_var);

/**
 * Override config values with environment variable values.
 * Should be called after loading the config file.
 * Returns the number of overrides applied.
 */
extern int		envconfig_override_conf(CONF **confp);

/**
 * Check if running in a container environment.
 * Detects Docker, Podman, Kubernetes, etc.
 * Returns 1 if in container, 0 otherwise.
 */
extern int		envconfig_is_container(void);

/**
 * Apply container-friendly defaults.
 * Called automatically by envconfig_init() when container is detected.
 */
extern void		envconfig_apply_container_defaults(CONF **confp);

/**
 * Read password from a file (for secret management).
 * Used when MYDNS_DB_PASSWORD_FILE is set.
 * Returns allocated string with password (caller must free), or NULL on error.
 */
extern char		*envconfig_read_password_file(const char *filepath);

/**
 * Dump effective configuration showing value sources.
 * Useful for debugging in container environments.
 */
extern void		envconfig_dump_config(CONF **confp);

/**
 * Special environment variable for password file
 */
#define ENVCONFIG_PASSWORD_FILE_VAR	"MYDNS_DB_PASSWORD_FILE"

#endif /* !_ENVCONFIG_H */

/* vi:set ts=3: */
