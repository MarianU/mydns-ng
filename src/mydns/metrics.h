/**************************************************************************************************
	metrics.h: Prometheus metrics collection for MyDNS-NG

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

#ifndef _MYDNS_METRICS_H
#define _MYDNS_METRICS_H

#include "mydnsutil.h"

/* Query type labels for metrics */
#define METRICS_QTYPE_A		0
#define METRICS_QTYPE_AAAA	1
#define METRICS_QTYPE_MX	2
#define METRICS_QTYPE_NS	3
#define METRICS_QTYPE_SOA	4
#define METRICS_QTYPE_TXT	5
#define METRICS_QTYPE_PTR	6
#define METRICS_QTYPE_CNAME	7
#define METRICS_QTYPE_SRV	8
#define METRICS_QTYPE_OTHER	9
#define METRICS_QTYPE_COUNT	10

/* Protocol labels for metrics */
#define METRICS_PROTO_UDP	0
#define METRICS_PROTO_TCP	1
#define METRICS_PROTO_COUNT	2

/* Cache type labels for metrics */
#define METRICS_CACHE_ZONE	0
#define METRICS_CACHE_REPLY	1
#define METRICS_CACHE_NEGATIVE	2
#define METRICS_CACHE_COUNT	3

/* Histogram bucket boundaries for query duration (in microseconds) */
#define METRICS_HIST_BUCKET_COUNT 10
extern const double metrics_hist_buckets[METRICS_HIST_BUCKET_COUNT];

/* Metrics structure */
typedef struct _mydns_metrics {
	/* Query counters by type and protocol */
	uint64_t	queries_total[METRICS_QTYPE_COUNT][METRICS_PROTO_COUNT];
	
	/* Query duration histogram buckets (cumulative) */
	uint64_t	query_duration_bucket[METRICS_HIST_BUCKET_COUNT];
	uint64_t	query_duration_count;
	double		query_duration_sum;
	
	/* Cache metrics */
	uint64_t	cache_hits[METRICS_CACHE_COUNT];
	uint64_t	cache_misses[METRICS_CACHE_COUNT];
	
	/* Server status */
	int		server_up;
	time_t		start_time;
	
} MYDNS_METRICS;

/* Global metrics instance */
extern MYDNS_METRICS *GlobalMetrics;

/* Metrics functions */
extern void		metrics_init(void);
extern void		metrics_cleanup(void);
extern void		metrics_inc_query(int qtype_index, int proto_index);
extern void		metrics_record_query_duration(double duration_us);
extern void		metrics_inc_cache_hit(int cache_index);
extern void		metrics_inc_cache_miss(int cache_index);
extern int		metrics_qtype_to_index(int qtype);
extern char		*metrics_format_prometheus(size_t *len);

#endif /* _MYDNS_METRICS_H */

/* vi:set ts=3: */
