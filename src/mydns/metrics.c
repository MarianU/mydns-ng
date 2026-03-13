/**************************************************************************************************
	metrics.c: Prometheus metrics collection for MyDNS-NG

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
#include "metrics.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_METRICS	1

/* Global metrics instance */
MYDNS_METRICS *GlobalMetrics = NULL;

/* Histogram bucket boundaries (in microseconds) */
const double metrics_hist_buckets[METRICS_HIST_BUCKET_COUNT] = {
	100.0,		/* 0.1ms */
	500.0,		/* 0.5ms */
	1000.0,		/* 1ms */
	5000.0,		/* 5ms */
	10000.0,	/* 10ms */
	50000.0,	/* 50ms */
	100000.0,	/* 100ms */
	500000.0,	/* 500ms */
	1000000.0,	/* 1s */
	5000000.0	/* 5s */
};

/* Query type names for Prometheus labels */
static const char *qtype_labels[METRICS_QTYPE_COUNT] = {
	"A", "AAAA", "MX", "NS", "SOA", "TXT", "PTR", "CNAME", "SRV", "other"
};

/* Protocol names for Prometheus labels */
static const char *proto_labels[METRICS_PROTO_COUNT] = {
	"udp", "tcp"
};

/* Cache names for Prometheus labels */
static const char *cache_labels[METRICS_CACHE_COUNT] = {
	"zone", "reply", "negative"
};


/**************************************************************************************************
	METRICS_INIT
	Initialize metrics collection.
**************************************************************************************************/
void
metrics_init(void) {
	if (GlobalMetrics)
		return;
	
	GlobalMetrics = ALLOCATE(sizeof(MYDNS_METRICS), MYDNS_METRICS);
	memset(GlobalMetrics, 0, sizeof(MYDNS_METRICS));
	GlobalMetrics->server_up = 1;
	GlobalMetrics->start_time = time(NULL);
	
#if DEBUG_ENABLED && DEBUG_METRICS
	DebugX("metrics", 1, _("Metrics system initialized"));
#endif
}
/*--- metrics_init() ----------------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_CLEANUP
	Clean up metrics collection.
**************************************************************************************************/
void
metrics_cleanup(void) {
	if (GlobalMetrics) {
		RELEASE(GlobalMetrics);
		GlobalMetrics = NULL;
	}
}
/*--- metrics_cleanup() -------------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_QTYPE_TO_INDEX
	Convert a DNS query type to a metrics index.
**************************************************************************************************/
int
metrics_qtype_to_index(int qtype) {
	switch (qtype) {
	case DNS_QTYPE_A:	return METRICS_QTYPE_A;
	case DNS_QTYPE_AAAA:	return METRICS_QTYPE_AAAA;
	case DNS_QTYPE_MX:	return METRICS_QTYPE_MX;
	case DNS_QTYPE_NS:	return METRICS_QTYPE_NS;
	case DNS_QTYPE_SOA:	return METRICS_QTYPE_SOA;
	case DNS_QTYPE_TXT:	return METRICS_QTYPE_TXT;
	case DNS_QTYPE_PTR:	return METRICS_QTYPE_PTR;
	case DNS_QTYPE_CNAME:	return METRICS_QTYPE_CNAME;
	case DNS_QTYPE_SRV:	return METRICS_QTYPE_SRV;
	default:		return METRICS_QTYPE_OTHER;
	}
}
/*--- metrics_qtype_to_index() ------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_INC_QUERY
	Increment query counter for the given type and protocol.
**************************************************************************************************/
void
metrics_inc_query(int qtype_index, int proto_index) {
	if (!GlobalMetrics)
		return;
	if (qtype_index < 0 || qtype_index >= METRICS_QTYPE_COUNT)
		return;
	if (proto_index < 0 || proto_index >= METRICS_PROTO_COUNT)
		return;
	
	GlobalMetrics->queries_total[qtype_index][proto_index]++;
}
/*--- metrics_inc_query() -----------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_RECORD_QUERY_DURATION
	Record a query duration in the histogram.
**************************************************************************************************/
void
metrics_record_query_duration(double duration_us) {
	int i;
	
	if (!GlobalMetrics)
		return;
	
	/* Update histogram buckets (cumulative) */
	for (i = 0; i < METRICS_HIST_BUCKET_COUNT; i++) {
		if (duration_us <= metrics_hist_buckets[i]) {
			GlobalMetrics->query_duration_bucket[i]++;
		}
	}
	
	GlobalMetrics->query_duration_count++;
	GlobalMetrics->query_duration_sum += duration_us / 1000000.0; /* Convert to seconds */
}
/*--- metrics_record_query_duration() -----------------------------------------------------------*/


/**************************************************************************************************
	METRICS_INC_CACHE_HIT
	Increment cache hit counter.
**************************************************************************************************/
void
metrics_inc_cache_hit(int cache_index) {
	if (!GlobalMetrics)
		return;
	if (cache_index < 0 || cache_index >= METRICS_CACHE_COUNT)
		return;
	
	GlobalMetrics->cache_hits[cache_index]++;
}
/*--- metrics_inc_cache_hit() -------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_INC_CACHE_MISS
	Increment cache miss counter.
**************************************************************************************************/
void
metrics_inc_cache_miss(int cache_index) {
	if (!GlobalMetrics)
		return;
	if (cache_index < 0 || cache_index >= METRICS_CACHE_COUNT)
		return;
	
	GlobalMetrics->cache_misses[cache_index]++;
}
/*--- metrics_inc_cache_miss() ------------------------------------------------------------------*/


/**************************************************************************************************
	METRICS_FORMAT_PROMETHEUS
	Format all metrics in Prometheus text exposition format.
	Returns allocated string that must be freed with RELEASE().
**************************************************************************************************/
char *
metrics_format_prometheus(size_t *out_len) {
	char *buf = NULL;
	size_t bufsize = 8192;
	size_t len = 0;
	int i, j;
	uint64_t cumulative;
	
	if (!GlobalMetrics) {
		*out_len = 0;
		return NULL;
	}
	
	buf = ALLOCATE(bufsize, char[]);
	
	/* mydns_up gauge */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_up Whether the MyDNS server is up\n"
		"# TYPE mydns_up gauge\n"
		"mydns_up %d\n\n",
		GlobalMetrics->server_up);
	
	/* mydns_start_time_seconds gauge */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_start_time_seconds Unix timestamp when the server started\n"
		"# TYPE mydns_start_time_seconds gauge\n"
		"mydns_start_time_seconds %lu\n\n",
		(unsigned long)GlobalMetrics->start_time);
	
	/* mydns_queries_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_queries_total Total number of DNS queries received\n"
		"# TYPE mydns_queries_total counter\n");
	
	for (i = 0; i < METRICS_QTYPE_COUNT; i++) {
		for (j = 0; j < METRICS_PROTO_COUNT; j++) {
			if (GlobalMetrics->queries_total[i][j] > 0) {
				len += snprintf(buf + len, bufsize - len,
					"mydns_queries_total{type=\"%s\",protocol=\"%s\"} %lu\n",
					qtype_labels[i], proto_labels[j],
					(unsigned long)GlobalMetrics->queries_total[i][j]);
			}
		}
	}
	len += snprintf(buf + len, bufsize - len, "\n");
	
	/* mydns_query_duration_seconds histogram */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_query_duration_seconds Query processing duration in seconds\n"
		"# TYPE mydns_query_duration_seconds histogram\n");
	
	cumulative = 0;
	for (i = 0; i < METRICS_HIST_BUCKET_COUNT; i++) {
		cumulative += GlobalMetrics->query_duration_bucket[i];
		len += snprintf(buf + len, bufsize - len,
			"mydns_query_duration_seconds_bucket{le=\"%.6f\"} %lu\n",
			metrics_hist_buckets[i] / 1000000.0, /* Convert to seconds */
			(unsigned long)cumulative);
	}
	len += snprintf(buf + len, bufsize - len,
		"mydns_query_duration_seconds_bucket{le=\"+Inf\"} %lu\n",
		(unsigned long)GlobalMetrics->query_duration_count);
	len += snprintf(buf + len, bufsize - len,
		"mydns_query_duration_seconds_sum %.6f\n",
		GlobalMetrics->query_duration_sum);
	len += snprintf(buf + len, bufsize - len,
		"mydns_query_duration_seconds_count %lu\n\n",
		(unsigned long)GlobalMetrics->query_duration_count);
	
	/* mydns_cache_hits_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_cache_hits_total Total number of cache hits\n"
		"# TYPE mydns_cache_hits_total counter\n");
	
	for (i = 0; i < METRICS_CACHE_COUNT; i++) {
		len += snprintf(buf + len, bufsize - len,
			"mydns_cache_hits_total{cache=\"%s\"} %lu\n",
			cache_labels[i],
			(unsigned long)GlobalMetrics->cache_hits[i]);
	}
	len += snprintf(buf + len, bufsize - len, "\n");
	
	/* mydns_cache_misses_total counter */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_cache_misses_total Total number of cache misses\n"
		"# TYPE mydns_cache_misses_total counter\n");
	
	for (i = 0; i < METRICS_CACHE_COUNT; i++) {
		len += snprintf(buf + len, bufsize - len,
			"mydns_cache_misses_total{cache=\"%s\"} %lu\n",
			cache_labels[i],
			(unsigned long)GlobalMetrics->cache_misses[i]);
	}
	len += snprintf(buf + len, bufsize - len, "\n");
	
	/* mydns_cache_size gauge - get from actual cache structures */
	len += snprintf(buf + len, bufsize - len,
		"# HELP mydns_cache_size Current number of entries in each cache\n"
		"# TYPE mydns_cache_size gauge\n");
	
	if (ZoneCache) {
		len += snprintf(buf + len, bufsize - len,
			"mydns_cache_size{cache=\"zone\"} %lu\n",
			(unsigned long)ZoneCache->count);
	}
	if (ReplyCache) {
		len += snprintf(buf + len, bufsize - len,
			"mydns_cache_size{cache=\"reply\"} %lu\n",
			(unsigned long)ReplyCache->count);
	}
#if USE_NEGATIVE_CACHE
	if (NegativeCache) {
		len += snprintf(buf + len, bufsize - len,
			"mydns_cache_size{cache=\"negative\"} %lu\n",
			(unsigned long)NegativeCache->count);
	}
#endif
	
	*out_len = len;
	return buf;
}
/*--- metrics_format_prometheus() ---------------------------------------------------------------*/

/* vi:set ts=3: */
