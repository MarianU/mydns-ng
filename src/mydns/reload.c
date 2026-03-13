/**************************************************************************************************
	reload.c: Configuration hot reload implementation for MyDNS-NG

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
#include "reload.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_RELOAD	1

/* Global reload statistics */
RELOAD_STATS reload_stats;

/* Volatile flag for signal handler communication */
volatile int reload_pending = 0;

/* Saved configuration values for rollback */
typedef struct _saved_config {
	/* RRL settings */
	int		rrl_enabled;
	uint32_t	rrl_responses_per_second;
	uint32_t	rrl_window;
	uint32_t	rrl_slip;
	int		rrl_ipv4_prefix;
	int		rrl_ipv6_prefix;

	/* Cache settings */
	uint32_t	zone_cache_size;
	uint32_t	zone_cache_expire;
	uint32_t	reply_cache_size;
	uint32_t	reply_cache_expire;
} SAVED_CONFIG;

static SAVED_CONFIG saved_config;

/* External variables from conf.c */
extern int		rrl_enabled;
extern uint32_t		rrl_responses_per_second;
extern uint32_t		rrl_window;
extern uint32_t		rrl_slip;
extern int		rrl_ipv4_prefix;
extern int		rrl_ipv6_prefix;

/* External cache pointers */
extern CACHE *ZoneCache;
extern CACHE *ReplyCache;
#if USE_NEGATIVE_CACHE
extern CACHE *NegativeCache;
#endif


/**************************************************************************************************
	SAVE_CURRENT_CONFIG
	Save current configuration values for potential rollback.
**************************************************************************************************/
static void
save_current_config(void) {
	saved_config.rrl_enabled = rrl_enabled;
	saved_config.rrl_responses_per_second = rrl_responses_per_second;
	saved_config.rrl_window = rrl_window;
	saved_config.rrl_slip = rrl_slip;
	saved_config.rrl_ipv4_prefix = rrl_ipv4_prefix;
	saved_config.rrl_ipv6_prefix = rrl_ipv6_prefix;

	if (ZoneCache) {
		saved_config.zone_cache_size = ZoneCache->limit;
		saved_config.zone_cache_expire = ZoneCache->expire;
	}
	if (ReplyCache) {
		saved_config.reply_cache_size = ReplyCache->limit;
		saved_config.reply_cache_expire = ReplyCache->expire;
	}

#if DEBUG_ENABLED && DEBUG_RELOAD
	DebugX("reload", 1, _("Saved current configuration for rollback"));
#endif
}
/*--- save_current_config() ---------------------------------------------------------------------*/


/**************************************************************************************************
	ROLLBACK_CONFIG
	Restore saved configuration values after a failed reload.
**************************************************************************************************/
static void
rollback_config(void) {
	rrl_enabled = saved_config.rrl_enabled;
	rrl_responses_per_second = saved_config.rrl_responses_per_second;
	rrl_window = saved_config.rrl_window;
	rrl_slip = saved_config.rrl_slip;
	rrl_ipv4_prefix = saved_config.rrl_ipv4_prefix;
	rrl_ipv6_prefix = saved_config.rrl_ipv6_prefix;

	Warn(_("Configuration rolled back to previous values"));

#if DEBUG_ENABLED && DEBUG_RELOAD
	DebugX("reload", 1, _("Configuration rolled back"));
#endif
}
/*--- rollback_config() -------------------------------------------------------------------------*/


/**************************************************************************************************
	VALIDATE_RRL_CONFIG
	Validate RRL configuration values.
	Returns 0 on success, -1 on validation failure.
**************************************************************************************************/
static int
validate_rrl_config(int enabled, uint32_t rate, uint32_t window, uint32_t slip,
		    int ipv4_prefix, int ipv6_prefix, char *errbuf, size_t errsize) {
	if (rate == 0 || rate > 10000) {
		snprintf(errbuf, errsize,
			 "rrl-responses-per-second must be between 1 and 10000 (got %u)", rate);
		return -1;
	}

	if (window == 0 || window > 3600) {
		snprintf(errbuf, errsize,
			 "rrl-window must be between 1 and 3600 seconds (got %u)", window);
		return -1;
	}

	if (slip > 100) {
		snprintf(errbuf, errsize,
			 "rrl-slip must be between 0 and 100 (got %u)", slip);
		return -1;
	}

	if (ipv4_prefix < 0 || ipv4_prefix > 32) {
		snprintf(errbuf, errsize,
			 "rrl-ipv4-prefix must be between 0 and 32 (got %d)", ipv4_prefix);
		return -1;
	}

	if (ipv6_prefix < 0 || ipv6_prefix > 128) {
		snprintf(errbuf, errsize,
			 "rrl-ipv6-prefix must be between 0 and 128 (got %d)", ipv6_prefix);
		return -1;
	}

	return 0;
}
/*--- validate_rrl_config() ---------------------------------------------------------------------*/


/**************************************************************************************************
	VALIDATE_CACHE_CONFIG
	Validate cache configuration values.
	Returns 0 on success, -1 on validation failure.
**************************************************************************************************/
static int
validate_cache_config(uint32_t size, uint32_t expire, const char *cache_name,
		      char *errbuf, size_t errsize) {
	/* Size of 0 is valid (disables cache), max reasonable size is 1M entries */
	if (size > 1000000) {
		snprintf(errbuf, errsize,
			 "%s cache-size must be at most 1000000 (got %u)", cache_name, size);
		return -1;
	}

	/* Expire of 0 means no expiration, max is 24 hours */
	if (expire > 86400) {
		snprintf(errbuf, errsize,
			 "%s cache-expire must be at most 86400 seconds (got %u)", cache_name, expire);
		return -1;
	}

	return 0;
}
/*--- validate_cache_config() -------------------------------------------------------------------*/


/**************************************************************************************************
	DETECT_CONFIG_CHANGES
	Compare new config values against current values and determine what changed.
**************************************************************************************************/
static void
detect_config_changes(RELOAD_CHANGES *changes) {
	int new_rrl_enabled;
	uint32_t new_rrl_rate, new_rrl_window, new_rrl_slip;
	int new_ipv4_prefix, new_ipv6_prefix;
	uint32_t new_zone_cache_size, new_zone_cache_expire;
	uint32_t new_reply_cache_size, new_reply_cache_expire;

	memset(changes, 0, sizeof(RELOAD_CHANGES));

	/* Check RRL changes */
	new_rrl_enabled = GETBOOL(conf_get(&Conf, "rrl-enabled", NULL));
	new_rrl_rate = atou(conf_get(&Conf, "rrl-responses-per-second", NULL));
	new_rrl_window = atou(conf_get(&Conf, "rrl-window", NULL));
	new_rrl_slip = atou(conf_get(&Conf, "rrl-slip", NULL));
	new_ipv4_prefix = atoi(conf_get(&Conf, "rrl-ipv4-prefix", NULL));
	new_ipv6_prefix = atoi(conf_get(&Conf, "rrl-ipv6-prefix", NULL));

	if (new_rrl_enabled != rrl_enabled ||
	    new_rrl_rate != rrl_responses_per_second ||
	    new_rrl_window != rrl_window ||
	    new_rrl_slip != rrl_slip ||
	    new_ipv4_prefix != rrl_ipv4_prefix ||
	    new_ipv6_prefix != rrl_ipv6_prefix) {
		changes->rrl_changed = 1;
	}

	/* Check zone cache changes */
	new_zone_cache_size = atou(conf_get(&Conf, "zone-cache-size", NULL));
	new_zone_cache_expire = atou(conf_get(&Conf, "zone-cache-expire", NULL));

	if (ZoneCache && (new_zone_cache_size != ZoneCache->limit ||
			  new_zone_cache_expire != (uint32_t)ZoneCache->expire)) {
		changes->zone_cache_changed = 1;
		changes->cache_changed = 1;
	}

	/* Check reply cache changes */
	new_reply_cache_size = atou(conf_get(&Conf, "reply-cache-size", NULL));
	new_reply_cache_expire = atou(conf_get(&Conf, "reply-cache-expire", NULL));

	if (ReplyCache && (new_reply_cache_size != ReplyCache->limit ||
			   new_reply_cache_expire != (uint32_t)ReplyCache->expire)) {
		changes->reply_cache_changed = 1;
		changes->cache_changed = 1;
	}

	/* Check log changes */
	/* Log changes are always considered "changed" for simplicity,
	   as comparing the complex log configuration is not straightforward */
	changes->log_changed = 1;
}
/*--- detect_config_changes() -------------------------------------------------------------------*/


/**************************************************************************************************
	APPLY_RRL_CONFIG
	Apply new RRL configuration values.
	Returns 0 on success, -1 on failure.
**************************************************************************************************/
static int
apply_rrl_config(char *errbuf, size_t errsize) {
	int new_enabled;
	uint32_t new_rate, new_window, new_slip;
	int new_ipv4_prefix, new_ipv6_prefix;
	int was_enabled = rrl_enabled;

	/* Read new values from config */
	new_enabled = GETBOOL(conf_get(&Conf, "rrl-enabled", NULL));
	new_rate = atou(conf_get(&Conf, "rrl-responses-per-second", NULL));
	new_window = atou(conf_get(&Conf, "rrl-window", NULL));
	new_slip = atou(conf_get(&Conf, "rrl-slip", NULL));
	new_ipv4_prefix = atoi(conf_get(&Conf, "rrl-ipv4-prefix", NULL));
	new_ipv6_prefix = atoi(conf_get(&Conf, "rrl-ipv6-prefix", NULL));

	/* Validate new values */
	if (new_enabled) {
		if (validate_rrl_config(new_enabled, new_rate, new_window, new_slip,
					new_ipv4_prefix, new_ipv6_prefix,
					errbuf, errsize) != 0) {
			return -1;
		}
	}

	/* Apply new values */
	rrl_enabled = new_enabled;
	rrl_responses_per_second = new_rate;
	rrl_window = new_window;
	rrl_slip = new_slip;
	rrl_ipv4_prefix = new_ipv4_prefix;
	rrl_ipv6_prefix = new_ipv6_prefix;

	/* Log the change */
	if (new_enabled != was_enabled) {
		Notice(_("RRL %s via hot reload"),
		       new_enabled ? _("enabled") : _("disabled"));
	} else if (new_enabled) {
		Notice(_("RRL settings updated: %u responses/sec, %u sec window, slip=%u, IPv4/%d, IPv6/%d"),
		       new_rate, new_window, new_slip, new_ipv4_prefix, new_ipv6_prefix);
	}

	/* If RRL was just enabled, initialize it */
	if (new_enabled && !was_enabled) {
		rrl_cleanup();  /* Clean up any existing state */
		rrl_init();
	}
	/* If RRL was just disabled, clean it up */
	else if (!new_enabled && was_enabled) {
		rrl_cleanup();
	}

	return 0;
}
/*--- apply_rrl_config() ------------------------------------------------------------------------*/


/**************************************************************************************************
	APPLY_CACHE_CONFIG
	Apply new cache configuration values.
	Note: Changing cache size requires clearing and reinitializing the cache.
	Returns 0 on success, -1 on failure.
**************************************************************************************************/
static int
apply_cache_config(RELOAD_CHANGES *changes, char *errbuf, size_t errsize) {
	uint32_t new_zone_cache_size, new_zone_cache_expire;
	uint32_t new_reply_cache_size, new_reply_cache_expire;

	/* Read new values */
	new_zone_cache_size = atou(conf_get(&Conf, "zone-cache-size", NULL));
	new_zone_cache_expire = atou(conf_get(&Conf, "zone-cache-expire", NULL));
	new_reply_cache_size = atou(conf_get(&Conf, "reply-cache-size", NULL));
	new_reply_cache_expire = atou(conf_get(&Conf, "reply-cache-expire", NULL));

	/* Validate */
	if (validate_cache_config(new_zone_cache_size, new_zone_cache_expire, "zone",
				  errbuf, errsize) != 0) {
		return -1;
	}
	if (validate_cache_config(new_reply_cache_size, new_reply_cache_expire, "reply",
				  errbuf, errsize) != 0) {
		return -1;
	}

	/* Apply zone cache changes */
	if (changes->zone_cache_changed && ZoneCache) {
		Notice(_("Zone cache configuration changed: size %lu -> %u, expire %lu -> %u"),
		       (unsigned long)ZoneCache->limit, new_zone_cache_size,
		       (unsigned long)ZoneCache->expire, new_zone_cache_expire);

		/* Update expire time (simple update) */
		ZoneCache->expire = new_zone_cache_expire;
#if USE_NEGATIVE_CACHE
		if (NegativeCache) {
			NegativeCache->expire = new_zone_cache_expire;
		}
#endif

		/* If size changed significantly, clear cache to force rebuild with new settings */
		if (new_zone_cache_size != ZoneCache->limit) {
			Notice(_("Zone cache size changed, clearing cache"));
			cache_empty(ZoneCache);
#if USE_NEGATIVE_CACHE
			cache_empty(NegativeCache);
#endif
			/* Note: We can't easily resize the hash table without reinitializing
			   the entire cache structure. For now, just update the limit to
			   control when eviction happens. Full resize would require restart. */
			ZoneCache->limit = new_zone_cache_size;
#if USE_NEGATIVE_CACHE
			if (NegativeCache) {
				NegativeCache->limit = new_zone_cache_size;
			}
#endif
		}
	}

	/* Apply reply cache changes */
	if (changes->reply_cache_changed && ReplyCache) {
		Notice(_("Reply cache configuration changed: size %lu -> %u, expire %lu -> %u"),
		       (unsigned long)ReplyCache->limit, new_reply_cache_size,
		       (unsigned long)ReplyCache->expire, new_reply_cache_expire);

		/* Update expire time */
		ReplyCache->expire = new_reply_cache_expire;

		/* If size changed, clear cache */
		if (new_reply_cache_size != ReplyCache->limit) {
			Notice(_("Reply cache size changed, clearing cache"));
			cache_empty(ReplyCache);
			ReplyCache->limit = new_reply_cache_size;
		}
	}

	return 0;
}
/*--- apply_cache_config() ----------------------------------------------------------------------*/


/**************************************************************************************************
	APPLY_LOG_CONFIG
	Apply new logging configuration.
	Returns 0 on success, -1 on failure.
**************************************************************************************************/
static int
apply_log_config(char *errbuf, size_t errsize) {
	/* conf_set_logging() handles all the work */
	conf_set_logging();

	Notice(_("Logging configuration reloaded"));
	return 0;
}
/*--- apply_log_config() ------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_INIT
	Initialize the reload subsystem.
**************************************************************************************************/
void
reload_init(void) {
	memset(&reload_stats, 0, sizeof(RELOAD_STATS));
	reload_stats.state = RELOAD_STATE_IDLE;
	reload_pending = 0;

#if DEBUG_ENABLED && DEBUG_RELOAD
	DebugX("reload", 1, _("Reload subsystem initialized"));
#endif
}
/*--- reload_init() -----------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_TRIGGER
	Trigger a configuration reload.
	Safe to call from signal handler.
**************************************************************************************************/
void
reload_trigger(void) {
	reload_pending = 1;
}
/*--- reload_trigger() --------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_CHECK
	Check if a reload is pending and perform it.
	Called from main loop.
**************************************************************************************************/
reload_result_t
reload_check(void) {
	reload_result_t result;

	if (!reload_pending) {
		return RELOAD_OK;
	}

	/* Clear the pending flag */
	reload_pending = 0;

	/* Perform the reload */
	result = reload_config();

	return result;
}
/*--- reload_check() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_CONFIG
	Perform the actual configuration reload.
	Not safe to call from signal handler.
**************************************************************************************************/
reload_result_t
reload_config(void) {
	RELOAD_CHANGES changes;
	char errbuf[256];
	reload_result_t result = RELOAD_OK;

	reload_stats.state = RELOAD_STATE_LOADING;
	reload_stats.reload_total++;
	reload_stats.last_reload_time = time(NULL);

	Notice(_("Configuration reload triggered"));

	/* Save current config for potential rollback */
	save_current_config();

	/* Reload configuration file */
	reload_stats.state = RELOAD_STATE_LOADING;

#if DEBUG_ENABLED && DEBUG_RELOAD
	DebugX("reload", 1, _("Reloading configuration from %s"), opt_conf);
#endif

	/* Re-read the configuration file
	   Note: conf_load() will update Conf with new values from file */
	conf_load(&Conf, opt_conf);

	/* Detect what changed */
	detect_config_changes(&changes);

#if DEBUG_ENABLED && DEBUG_RELOAD
	DebugX("reload", 1, _("Changes detected: log=%d, rrl=%d, cache=%d"),
	       changes.log_changed, changes.rrl_changed, changes.cache_changed);
#endif

	/* Validate and apply changes */
	reload_stats.state = RELOAD_STATE_APPLYING;
	errbuf[0] = '\0';

	/* Apply RRL changes */
	if (changes.rrl_changed) {
		if (apply_rrl_config(errbuf, sizeof(errbuf)) != 0) {
			Warn(_("Reload failed (RRL): %s"), errbuf);
			strncpy(reload_stats.last_error, errbuf, sizeof(reload_stats.last_error) - 1);
			rollback_config();
			reload_stats.state = RELOAD_STATE_FAILED;
			reload_stats.last_result = RELOAD_ERR_VALIDATE;
			reload_stats.reload_failed++;
			return RELOAD_ERR_VALIDATE;
		}
	}

	/* Apply cache changes */
	if (changes.cache_changed) {
		if (apply_cache_config(&changes, errbuf, sizeof(errbuf)) != 0) {
			Warn(_("Reload failed (cache): %s"), errbuf);
			strncpy(reload_stats.last_error, errbuf, sizeof(reload_stats.last_error) - 1);
			rollback_config();
			reload_stats.state = RELOAD_STATE_FAILED;
			reload_stats.last_result = RELOAD_ERR_VALIDATE;
			reload_stats.reload_failed++;
			return RELOAD_ERR_VALIDATE;
		}
	}

	/* Apply log changes (always) */
	if (apply_log_config(errbuf, sizeof(errbuf)) != 0) {
		Warn(_("Reload failed (logging): %s"), errbuf);
		strncpy(reload_stats.last_error, errbuf, sizeof(reload_stats.last_error) - 1);
		reload_stats.state = RELOAD_STATE_FAILED;
		reload_stats.last_result = RELOAD_ERR_APPLY;
		reload_stats.reload_failed++;
		return RELOAD_ERR_APPLY;
	}

	/* Success */
	reload_stats.state = RELOAD_STATE_SUCCESS;
	reload_stats.last_result = RELOAD_OK;
	reload_stats.reload_success++;
	reload_stats.last_success_time = time(NULL);
	reload_stats.last_error[0] = '\0';

	Notice(_("Configuration reloaded successfully"));

	return RELOAD_OK;
}
/*--- reload_config() ---------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_GET_STATS
	Get current reload statistics.
**************************************************************************************************/
void
reload_get_stats(RELOAD_STATS *stats) {
	if (stats) {
		memcpy(stats, &reload_stats, sizeof(RELOAD_STATS));
	}
}
/*--- reload_get_stats() ------------------------------------------------------------------------*/


/**************************************************************************************************
	RELOAD_FORMAT_PROMETHEUS
	Format reload metrics in Prometheus format.
**************************************************************************************************/
int
reload_format_prometheus(char *buf, size_t bufsize) {
	int len = 0;

	/* mydns_config_reload_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_config_reload_total Total number of configuration reload attempts\n"
		"# TYPE mydns_config_reload_total counter\n"
		"mydns_config_reload_total %lu\n\n",
		(unsigned long)reload_stats.reload_total);

	/* mydns_config_reload_success_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_config_reload_success_total Total number of successful configuration reloads\n"
		"# TYPE mydns_config_reload_success_total counter\n"
		"mydns_config_reload_success_total %lu\n\n",
		(unsigned long)reload_stats.reload_success);

	/* mydns_config_reload_failed_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_config_reload_failed_total Total number of failed configuration reloads\n"
		"# TYPE mydns_config_reload_failed_total counter\n"
		"mydns_config_reload_failed_total %lu\n\n",
		(unsigned long)reload_stats.reload_failed);

	/* mydns_config_last_reload_timestamp gauge */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_config_last_reload_timestamp Unix timestamp of last reload attempt\n"
		"# TYPE mydns_config_last_reload_timestamp gauge\n"
		"mydns_config_last_reload_timestamp %lu\n\n",
		(unsigned long)reload_stats.last_reload_time);

	/* mydns_config_last_reload_success_timestamp gauge */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_config_last_reload_success_timestamp Unix timestamp of last successful reload\n"
		"# TYPE mydns_config_last_reload_success_timestamp gauge\n"
		"mydns_config_last_reload_success_timestamp %lu\n\n",
		(unsigned long)reload_stats.last_success_time);

	return len;
}
/*--- reload_format_prometheus() ----------------------------------------------------------------*/

/* vi:set ts=3: */
/* NEED_PO */
