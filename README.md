# MyDNS-NG - SQL-based DNS Server

MyDNS-NG is a simple, non-recursive authoritative DNS server that serves records directly from an SQL database (MySQL/MariaDB or PostgreSQL).

This is a modernized fork of the original MyDNS by Don Moore, enhanced for cloud-native Kubernetes deployments.

## Features

### Core DNS Features
- Authoritative DNS server for your zones
- SQL backend (MySQL/MariaDB, PostgreSQL)
- Zone and reply caching
- AXFR/IXFR zone transfers
- Dynamic DNS updates (RFC 2136)
- CAA record support (RFC 8659)
- EDNS0 support (RFC 6891)

### Cloud-Native Features
- **Prometheus metrics** endpoint (`/metrics`)
- **Health probes** (`/healthz`, `/readyz`) for Kubernetes
- **Response Rate Limiting (RRL)** for DDoS mitigation
- **Graceful shutdown** with in-flight query draining
- **Database resilience** with stale cache serving
- **Structured JSON logging** for log aggregators
- **Environment variable configuration** for containers
- **Configuration hot reload** via SIGHUP

## Quick Start

### Docker

```bash
docker run -d \
  -e MYDNS_DB_HOST=mysql.example.com \
  -e MYDNS_DB_USER=mydns \
  -e MYDNS_DB_PASSWORD=secret \
  -e MYDNS_DB_DATABASE=mydns \
  -p 53:53/udp \
  -p 53:53/tcp \
  -p 9153:9153 \
  ghcr.io/marianu/mydns-ng:latest
```

### Kubernetes

See the [Helm chart](deploy/helm/mydns-ng/) for production Kubernetes deployment.

```bash
helm install mydns ./deploy/helm/mydns-ng \
  --set database.host=mysql.example.com \
  --set database.existingSecret=mydns-db-credentials
```

## Configuration

MyDNS-NG can be configured via:
1. Configuration file (`/etc/mydns.conf`)
2. Environment variables (`MYDNS_*` prefix)
3. Command line arguments

Environment variables override config file values. This is ideal for container deployments.

### Database Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `db-host` | `MYDNS_DB_HOST` | `localhost` | Database server hostname |
| `db-user` | `MYDNS_DB_USER` | `mydns` | Database username |
| `db-password` | `MYDNS_DB_PASSWORD` | - | Database password |
| `database` | `MYDNS_DATABASE` | `mydns` | Database name |
| `db-pool-enabled` | `MYDNS_DB_POOL_ENABLED` | `no` | Enable connection pooling |
| `db-pool-size` | `MYDNS_DB_POOL_SIZE` | `10` | Number of connections in pool |
| `db-pool-min` | `MYDNS_DB_POOL_MIN` | `2` | Minimum idle connections |
| `db-pool-acquire-timeout` | `MYDNS_DB_POOL_ACQUIRE_TIMEOUT` | `5` | Seconds to wait for connection |

**Password file support** (for Kubernetes secrets):
```bash
MYDNS_DB_PASSWORD_FILE=/run/secrets/db-password
```

### DNS Server Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `listen` | `MYDNS_LISTEN` | `*` | Listen address(es) |
| `no-listen` | `MYDNS_NO_LISTEN` | - | Addresses to exclude |
| `user` | `MYDNS_USER` | `nobody` | Run as user |
| `group` | `MYDNS_GROUP` | `nogroup` | Run as group |

### EDNS0 Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `edns-enabled` | `MYDNS_EDNS_ENABLED` | `yes` | Enable EDNS0 support |
| `edns-max-udp-size` | `MYDNS_EDNS_MAX_UDP_SIZE` | `4096` | Maximum UDP response size |

### Cache Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `cache-size` | `MYDNS_CACHE_SIZE` | `1024` | Zone cache size |
| `cache-expire` | `MYDNS_CACHE_EXPIRE` | `60` | Zone cache TTL (seconds) |
| `reply-cache-size` | `MYDNS_REPLY_CACHE_SIZE` | `1024` | Reply cache size |
| `reply-cache-expire` | `MYDNS_REPLY_CACHE_EXPIRE` | `30` | Reply cache TTL (seconds) |
| `stale-cache-ttl` | `MYDNS_STALE_CACHE_TTL` | `300` | Serve stale cache when DB is down |

### Metrics & Health Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `metrics-enabled` | `MYDNS_METRICS_ENABLED` | `yes` | Enable HTTP metrics server |
| `metrics-port` | `MYDNS_METRICS_PORT` | `9153` | HTTP server port |

**Endpoints:**
- `GET /metrics` - Prometheus metrics
- `GET /healthz` - Liveness probe (returns 200 if alive)
- `GET /readyz` - Readiness probe (checks DB connectivity)

### Response Rate Limiting (RRL)

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `rrl-enabled` | `MYDNS_RRL_ENABLED` | `no` | Enable rate limiting |
| `rrl-rate` | `MYDNS_RRL_RATE` | `20` | Responses per second per client |
| `rrl-window` | `MYDNS_RRL_WINDOW` | `15` | Time window (seconds) |
| `rrl-slip-ratio` | `MYDNS_RRL_SLIP_RATIO` | `2` | Slip ratio for TC responses |
| `rrl-ipv4-prefix` | `MYDNS_RRL_IPV4_PREFIX` | `24` | IPv4 prefix length for grouping |
| `rrl-ipv6-prefix` | `MYDNS_RRL_IPV6_PREFIX` | `48` | IPv6 prefix length for grouping |

### Logging Configuration

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `log-format` | `MYDNS_LOG_FORMAT` | `text` | Log format: `text` or `json` |
| `log-level` | `MYDNS_LOG_LEVEL` | `notice` | Log level: debug, info, notice, warning, error |
| `query-log` | `MYDNS_QUERY_LOG` | `no` | Enable query logging |
| `log-file` | `MYDNS_LOG_FILE` | - | Log file path (empty = stdout) |

### Graceful Shutdown

| Config Option | Environment Variable | Default | Description |
|--------------|---------------------|---------|-------------|
| `shutdown-timeout` | `MYDNS_SHUTDOWN_TIMEOUT` | `30` | Seconds to wait for in-flight queries |

## Prometheus Metrics

When metrics are enabled, the following metrics are exposed:

```prometheus
# DNS query metrics
mydns_queries_total{type="A"} 12345
mydns_queries_total{type="AAAA"} 6789
mydns_responses_total{rcode="NOERROR"} 12000
mydns_responses_total{rcode="NXDOMAIN"} 500
mydns_query_latency_seconds_bucket{le="0.001"} 10000

# Cache metrics
mydns_cache_hits_total{cache="zone"} 50000
mydns_cache_misses_total{cache="zone"} 1000
mydns_cache_size{cache="zone"} 512
mydns_cache_size{cache="reply"} 256

# Database metrics
mydns_db_queries_total 15000
mydns_db_errors_total 5
mydns_db_connection_state 1
mydns_db_pool_size 10
mydns_db_pool_available 8
mydns_db_pool_in_use 2

# RRL metrics
mydns_rrl_responses_total 1000
mydns_rrl_slipped_total 50
mydns_rrl_dropped_total 25

# Process metrics
mydns_uptime_seconds 86400
mydns_process_memory_bytes 52428800

# Config reload metrics
mydns_config_reload_total 3
mydns_config_reload_success_total 3
mydns_config_last_reload_timestamp 1705312245
```

## Configuration Hot Reload

Send SIGHUP to reload configuration without restart:

```bash
kill -HUP $(cat /var/run/mydns.pid)
```

**Hot-reloadable options:**
- Log level and format
- RRL settings (rate, window, slip ratio)
- Cache sizes and expiry times
- Query logging

**Requires restart:**
- Listen addresses
- Database connection settings
- User/group settings

## Container Detection

MyDNS-NG automatically detects container environments (Docker, Podman, Kubernetes) and applies sensible defaults:

- Logs to stdout (not syslog)
- JSON log format for log aggregators
- Metrics endpoint enabled

## Building

### Prerequisites

- GCC or Clang
- MySQL/MariaDB or PostgreSQL development libraries
- Autotools (autoconf, automake, libtool)

### Build from Source

```bash
./configure --with-mysql
make
sudo make install
```

### Docker Build

```bash
./build.sh
```

## Database Schema

MyDNS-NG uses two main tables:

```sql
-- Zones table
CREATE TABLE soa (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  origin VARCHAR(255) NOT NULL,
  ns VARCHAR(255) NOT NULL,
  mbox VARCHAR(255) NOT NULL,
  serial INT UNSIGNED NOT NULL DEFAULT 1,
  refresh INT UNSIGNED NOT NULL DEFAULT 28800,
  retry INT UNSIGNED NOT NULL DEFAULT 7200,
  expire INT UNSIGNED NOT NULL DEFAULT 604800,
  minimum INT UNSIGNED NOT NULL DEFAULT 86400,
  ttl INT UNSIGNED NOT NULL DEFAULT 86400,
  UNIQUE KEY (origin)
);

-- Resource records table  
CREATE TABLE rr (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  zone INT UNSIGNED NOT NULL,
  name VARCHAR(200) NOT NULL,
  type ENUM('A','AAAA','CNAME','MX','NS','PTR','SRV','TXT','CAA') NOT NULL,
  data VARCHAR(512) NOT NULL,
  aux INT UNSIGNED NOT NULL DEFAULT 0,
  ttl INT UNSIGNED NOT NULL DEFAULT 86400,
  KEY (zone),
  KEY (name),
  KEY (type)
);
```

## License

MyDNS-NG is released under the GNU General Public License (GPL) v2.

## Links

- Original MyDNS: http://mydns.bboy.net/
- MyDNS-NG: http://www.mydns-ng.com
- GitHub: https://github.com/deliank/mydns-ng

## Authors

- Don Moore (original MyDNS)
- Howard Wilkinson (MyDNS-NG maintainer)
- Contributors
