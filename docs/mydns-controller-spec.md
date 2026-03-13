# MyDNS Controller - Technical Specification

> **Project Repository**: This controller will be developed in a separate repository.
> This document serves as the technical specification and implementation guide.

> **Implementation Status**:
> - **DNS-01 ACME Webhook**: **IMPLEMENTED** - See [`cert-manager-webhook/`](../cert-manager-webhook/) directory
> - **DNSZone/DNSRecord CRDs**: Not yet implemented
> - **Health Monitoring**: Not yet implemented

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Custom Resource Definitions](#custom-resource-definitions)
4. [Zone Health Monitoring](#zone-health-monitoring)
5. [DNS-01 ACME Webhook](#dns-01-acme-webhook)
6. [Database Schema Reference](#database-schema-reference)
7. [Implementation Plan](#implementation-plan)
8. [Deployment Guide](#deployment-guide)
9. [Future Improvements for MyDNS](#future-improvements-for-mydns)

---

## Overview

### Purpose

The `mydns-controller` is a Kubernetes-native DNS management controller designed to:

1. **Manage DNS zones and records** via Kubernetes CRDs
2. **Provide zone health monitoring** with quorum-based failover across geographic zones
3. **Integrate with cert-manager** for automated DNS-01 ACME challenges
4. **Replace the existing cronjob** for dynamic IP updates with a more robust solution

### Problem Statement

Current limitations with the existing MyDNS setup:

- **No automatic failover**: When a zone goes down, DNS records pointing to that zone remain active
- **Cronjob limitations**: The `mydns-sync-dyn-ip` cronjob can only update IPs from within each zone; it cannot remove records for a down zone
- ~~**No DNS-01 support**: Cannot use Let's Encrypt DNS-01 challenges with MyDNS~~ **SOLVED** - See [`cert-manager-webhook/`](../cert-manager-webhook/)
- **Manual management**: DNS records are managed directly in the database, no GitOps workflow

### Target Cluster

- **Zones**: ro2 (Romania), ro3 (Romania), de4 (Germany)
- **Nodes**: 10 total (5 in ro2, 4 in ro3, 1 in de4)
- **CNI**: kube-router
- **DNS Backend**: MyDNS-NG with MariaDB

---

## Architecture

### High-Level Diagram

```
                                    ┌─────────────────────────────────────┐
                                    │         Kubernetes Cluster          │
                                    │                                     │
  ┌──────────────────┐              │  ┌─────────────────────────────┐   │
  │   cert-manager   │──────────────┼─▶│    mydns-acme-webhook       │   │
  │                  │              │  │    (DNS-01 solver)          │   │
  └──────────────────┘              │  └──────────────┬──────────────┘   │
                                    │                 │                   │
  ┌──────────────────┐              │  ┌──────────────▼──────────────┐   │
  │   DNSZone CRD    │──────────────┼─▶│                             │   │
  │   DNSRecord CRD  │              │  │    mydns-controller         │   │
  └──────────────────┘              │  │    (reconciliation loop)    │   │
                                    │  │                             │   │
                                    │  └──────────────┬──────────────┘   │
                                    │                 │                   │
                                    │  ┌──────────────▼──────────────┐   │
                                    │  │                             │   │
                                    │  │   mydns-health-monitor      │   │
                                    │  │   (1 pod per zone)          │   │
                                    │  │                             │   │
                                    │  │   ro2 ◄──► ro3 ◄──► de4    │   │
                                    │  │   (quorum consensus)        │   │
                                    │  │                             │   │
                                    │  └──────────────┬──────────────┘   │
                                    │                 │                   │
                                    └─────────────────┼───────────────────┘
                                                      │
                                                      ▼
                                    ┌─────────────────────────────────────┐
                                    │         MariaDB (MyDNS)             │
                                    │                                     │
                                    │   ┌─────┐  ┌────┐  ┌─────────┐     │
                                    │   │ soa │  │ rr │  │ rr_zone │     │
                                    │   └─────┘  └────┘  └─────────┘     │
                                    └─────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Status |
|-----------|---------------|--------|
| `mydns-controller` | Reconciles DNSZone and DNSRecord CRDs to the MyDNS database | Not implemented |
| `mydns-health-monitor` | Monitors zone health, implements quorum consensus, triggers failover | Not implemented |
| `mydns-acme-webhook` | Handles DNS-01 challenges for cert-manager | **Implemented** - [`cert-manager-webhook/`](../cert-manager-webhook/) |

---

## Custom Resource Definitions

### DNSZone

Represents a DNS zone (maps to the `soa` table in MyDNS).

```yaml
apiVersion: dns.example.com/v1alpha1
kind: DNSZone
metadata:
  name: example-com
  namespace: dns
spec:
  # Zone origin (domain name) - REQUIRED
  origin: example.com
  
  # Primary nameserver - REQUIRED
  ns: ns1.example.com
  
  # Admin email (@ replaced with .) - REQUIRED
  mbox: admin.example.com
  
  # SOA parameters (all optional with defaults)
  refresh: 28800      # Default: 28800 (8 hours)
  retry: 7200         # Default: 7200 (2 hours)
  expire: 604800      # Default: 604800 (1 week)
  minimum: 86400      # Default: 86400 (1 day)
  ttl: 86400          # Default: 86400 (1 day)
  
  # Zone transfer allowed IPs (optional)
  xfer: ""
  
  # Enable/disable zone
  active: true        # Default: true
  
  # Geo-failover configuration (optional)
  failover:
    enabled: true     # Default: false
    # Kubernetes topology zones that serve this DNS zone
    zones:
      - name: ro2
        weight: 100   # Traffic weight (for future weighted DNS)
        enabled: true
      - name: ro3
        weight: 100
        enabled: true
      - name: de4
        weight: 50
        enabled: true

status:
  # Database ID (assigned after sync)
  zoneId: 1
  
  # Current SOA serial (auto-incremented on changes)
  serial: 2024021501
  
  # Zone health per Kubernetes zone (populated by health monitor)
  zoneHealth:
    ro2:
      healthy: true
      lastCheck: "2024-02-15T10:30:00Z"
      activeRecords: 5
    ro3:
      healthy: true
      lastCheck: "2024-02-15T10:30:00Z"
      activeRecords: 5
    de4:
      healthy: false
      lastCheck: "2024-02-15T10:30:00Z"
      reason: "Zone unreachable from quorum"
      activeRecords: 0
  
  # Standard Kubernetes conditions
  conditions:
    - type: Ready
      status: "True"
      lastTransitionTime: "2024-02-15T10:30:00Z"
      reason: Synced
      message: "Zone synced to database"
    - type: Degraded
      status: "True"
      reason: ZoneUnhealthy
      message: "Zone de4 is unhealthy"
```

### DNSRecord

Represents DNS records (maps to `rr` + `rr_zone` tables).

```yaml
apiVersion: dns.example.com/v1alpha1
kind: DNSRecord
metadata:
  name: www-example-com
  namespace: dns
  labels:
    dns.example.com/zone: example-com
spec:
  # Reference to DNSZone - REQUIRED
  zoneRef:
    name: example-com
  
  # Record name (relative to zone, use "@" for apex) - REQUIRED
  name: www
  
  # Record type - REQUIRED
  # Supported: A, AAAA, CNAME, MX, TXT, NS, SRV, PTR, SPF, NAPTR, HINFO, RP, ALIAS
  type: A
  
  # Record targets (for non-geo records) - REQUIRED unless zoneTargets is set
  targets:
    - 192.168.1.100
    - 192.168.1.101
  
  # TTL in seconds (optional, defaults to zone TTL)
  ttl: 300
  
  # Priority for MX/SRV records (optional)
  priority: 0
  
  # Enable/disable record
  active: true        # Default: true
  
  # Per-zone targets for geo-failover (optional)
  # When set, these override "targets" for specific Kubernetes zones
  zoneTargets:
    ro2:
      targets:
        - 82.77.186.233
      enabled: true
    ro3:
      targets:
        - 5.2.225.29
      enabled: true
    de4:
      targets:
        - 157.173.110.230
      enabled: true
  
  # Dynamic IP source (optional, replaces static targets)
  # Mutually exclusive with targets/zoneTargets
  dynamicSource:
    # Type of dynamic source
    type: external-ip   # external-ip | node-ip | service
    
    # For node-ip: select nodes
    nodeSelector:
      topology.kubernetes.io/zone: ro2
    
    # For service: reference a service
    serviceRef:
      name: my-service
      namespace: default

status:
  # Database record IDs (for tracking)
  recordIds:
    - 152
    - 153
  
  # Currently active targets per zone
  activeTargets:
    ro2: ["82.77.186.233"]
    ro3: ["5.2.225.29"]
    de4: []  # Zone is down, records removed
  
  # Last resolved dynamic IPs (if using dynamicSource)
  resolvedTargets:
    - 82.77.186.233
  
  # Conditions
  conditions:
    - type: Ready
      status: "True"
      reason: Synced
      message: "Record synced to database"
```

### DNSHealthCheck

Internal resource for tracking zone health observations (one per monitor pod).

```yaml
apiVersion: dns.example.com/v1alpha1
kind: DNSHealthCheck
metadata:
  name: zone-health-ro2
  namespace: dns
  labels:
    dns.example.com/zone: ro2
    dns.example.com/component: health-monitor
spec:
  # The Kubernetes zone this monitor runs in
  zone: ro2
  
  # Check configuration
  checkInterval: 30s          # Default: 30s
  failureThreshold: 3         # Default: 3
  successThreshold: 2         # Default: 2
  timeout: 10s                # Default: 10s
  
  # Zones to monitor (other zones, not self)
  checkZones:
    - ro3
    - de4
  
  # Health check methods (in order of preference)
  checkMethods:
    - type: node-ready        # Check node Ready conditions via k8s API
      weight: 100
    - type: http              # HTTP health check to zone endpoint
      weight: 50
      endpoint: "http://health.{zone}.internal:8080/healthz"
    - type: tcp               # TCP connectivity check
      weight: 25
      port: 22

status:
  # This monitor's view of other zones
  observedHealth:
    ro3:
      healthy: true
      consecutiveFailures: 0
      consecutiveSuccesses: 5
      lastCheck: "2024-02-15T10:30:00Z"
      lastSuccess: "2024-02-15T10:30:00Z"
      checkResults:
        node-ready: true
        http: true
    de4:
      healthy: false
      consecutiveFailures: 3
      consecutiveSuccesses: 0
      lastCheck: "2024-02-15T10:30:00Z"
      lastFailure: "2024-02-15T10:30:00Z"
      reason: "All nodes NotReady (0/1 ready)"
      checkResults:
        node-ready: false
  
  # Quorum consensus calculated from all DNSHealthCheck resources
  quorumConsensus:
    de4:
      observersReportingUnhealthy: ["ro2", "ro3"]
      observersReportingHealthy: []
      consensusHealthy: false
      consensusTimestamp: "2024-02-15T10:30:00Z"
  
  # Self-health (can this monitor reach the k8s API?)
  selfHealthy: true
  lastSelfCheck: "2024-02-15T10:30:00Z"
```

---

## Zone Health Monitoring

### Quorum Algorithm

The health monitoring system uses a quorum-based consensus to prevent split-brain scenarios.

#### Rules

1. **Self-exclusion**: A monitor never checks its own zone's health
2. **Isolation detection**: If a monitor can't reach ANY other zone, it assumes it's isolated and takes no action
3. **Majority consensus**: Actions require >50% of active monitors to agree
4. **Database as source of truth**: Before taking action, check if another monitor already acted
5. **Hysteresis**: Require consecutive failures/successes before state change

#### Decision Flow

```
START
  │
  ▼
┌─────────────────────────────────────┐
│ Check health of OTHER zones         │
│ (not my own zone)                   │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ Can I reach ANY other zone?         │
└──────────────┬──────────────────────┘
               │
        ┌──────┴──────┐
        │ NO          │ YES
        ▼             ▼
┌───────────────┐ ┌─────────────────────────────────┐
│ I'm isolated  │ │ Update my DNSHealthCheck with   │
│ DO NOTHING    │ │ observed health states          │
└───────────────┘ └──────────────┬──────────────────┘
                                 │
                                 ▼
                  ┌─────────────────────────────────┐
                  │ Read other DNSHealthCheck       │
                  │ resources (from other monitors) │
                  └──────────────┬──────────────────┘
                                 │
                                 ▼
                  ┌─────────────────────────────────┐
                  │ Calculate quorum consensus      │
                  │ for each zone                   │
                  └──────────────┬──────────────────┘
                                 │
                                 ▼
                  ┌─────────────────────────────────┐
                  │ For each zone with consensus:   │
                  │                                 │
                  │ IF unhealthy AND majority agree │
                  │   → Check DB state              │
                  │   → If not already marked       │
                  │     → Remove zone's records     │
                  │                                 │
                  │ IF healthy AND was unhealthy    │
                  │   → Gradually restore records   │
                  └─────────────────────────────────┘
```

#### Quorum Examples (3 zones)

| Monitor | Sees ro3 | Sees de4 | Action for de4 |
|---------|----------|----------|----------------|
| ro2     | healthy  | unhealthy| Check consensus |
| ro3     | -        | unhealthy| Check consensus |
| de4     | healthy  | -        | N/A (self) |

**Consensus**: 2/2 monitors (excluding de4 self) report de4 as unhealthy → **Remove de4 records**

| Monitor | Sees ro3 | Sees de4 | Action for de4 |
|---------|----------|----------|----------------|
| ro2     | unhealthy| unhealthy| **NO ACTION** (can't reach any zone = isolated) |
| ro3     | -        | healthy  | No action needed |
| de4     | healthy  | -        | N/A |

**No consensus**: ro2 is isolated, only ro3's vote counts, no majority → **No action**

### Database Locking Strategy

To prevent race conditions when multiple monitors try to update:

```sql
-- Use SELECT ... FOR UPDATE with NOWAIT
START TRANSACTION;

-- Check current state
SELECT id, data, active 
FROM rr 
WHERE zone = ? AND name = ? AND type = 'A'
FOR UPDATE NOWAIT;

-- Only proceed if state matches expected
UPDATE rr SET active = 'N' WHERE id = ? AND active = 'Y';

-- Check affected rows
-- If 0 rows affected, another monitor already acted

COMMIT;
```

---

## DNS-01 ACME Webhook

> **STATUS: IMPLEMENTED**
>
> The DNS-01 ACME webhook has been fully implemented and is available in the
> [`cert-manager-webhook/`](../cert-manager-webhook/) directory.
>
> **Quick Start:**
> ```bash
> # Install from OCI registry (after release)
> helm install mydns-webhook oci://YOUR_REGISTRY/your-org/mydns-webhook \
>   --namespace cert-manager \
>   --version 1.0.0
>
> # Or install from source
> helm install mydns-webhook ./cert-manager-webhook/deploy/mydns-webhook \
>   --namespace cert-manager
> ```
>
> See [`cert-manager-webhook/README.md`](../cert-manager-webhook/README.md) for full documentation.

### Integration with cert-manager

The webhook implements the cert-manager DNS01 webhook interface.

#### API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/apis/acme.example.com/v1alpha1/present` | Create TXT record for challenge |
| POST | `/apis/acme.example.com/v1alpha1/cleanup` | Remove TXT record after validation |
| GET | `/healthz` | Health check endpoint |

#### Challenge Flow

```
1. cert-manager requests certificate for *.example.com

2. ACME server issues DNS-01 challenge:
   - Create TXT record: _acme-challenge.example.com
   - Value: <random-token>

3. cert-manager calls webhook Present():
   POST /present
   {
     "resourceNamespace": "cert-manager",
     "resolvedFQDN": "_acme-challenge.example.com.",
     "resolvedZone": "example.com.",
     "key": "abc123-challenge-token"
   }

4. Webhook creates TXT record in MyDNS:
   INSERT INTO rr (zone, name, type, data, ttl, active)
   VALUES (1, '_acme-challenge', 'TXT', 'abc123-challenge-token', 60, 'Y');

5. ACME server verifies TXT record

6. cert-manager calls webhook CleanUp():
   POST /cleanup
   { ... same payload ... }

7. Webhook removes TXT record from MyDNS
```

#### ClusterIssuer Configuration

```yaml
apiVersion: cert-manager.io/v1
kind: ClusterIssuer
metadata:
  name: letsencrypt-prod
spec:
  acme:
    server: https://acme-v02.api.letsencrypt.org/directory
    email: admin@example.com
    privateKeySecretRef:
      name: letsencrypt-prod-account-key
    solvers:
      - dns01:
          webhook:
            groupName: acme.example.com
            solverName: mydns
            config:
              # Database connection (references secret)
              dbSecretRef:
                name: mydns-db-credentials
                namespace: cert-manager
              # Optional: TTL for challenge TXT records (default: 60)
              ttl: 60
```

#### Database Credentials Secret

```yaml
apiVersion: v1
kind: Secret
metadata:
  name: mydns-db-credentials
  namespace: cert-manager
type: Opaque
stringData:
  host: "192.168.1.50"
  port: "3306"
  user: "mydns_webhook"
  password: "your-secure-password"
  database: "mydns"
```

#### Required Database Permissions

```sql
-- Minimal permissions for the webhook user
GRANT SELECT ON mydns.soa TO 'mydns_webhook'@'%';
GRANT SELECT, INSERT, DELETE ON mydns.rr TO 'mydns_webhook'@'%';
GRANT UPDATE (serial) ON mydns.soa TO 'mydns_webhook'@'%';
```

---

## Database Schema Reference

### Current MyDNS Schema

```sql
-- soa table (zones)
CREATE TABLE soa (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  sys_userid INT NOT NULL,
  sys_groupid INT NOT NULL,
  sys_perm_user VARCHAR(5) NOT NULL,
  sys_perm_group VARCHAR(5) NOT NULL,
  sys_perm_other VARCHAR(5) NOT NULL,
  server_id INT NOT NULL DEFAULT 1,
  origin CHAR(255) NOT NULL UNIQUE,      -- Zone name (e.g., "example.com.")
  ns CHAR(255) NOT NULL,                  -- Primary NS
  mbox CHAR(255) NOT NULL,                -- Admin email
  serial INT UNSIGNED NOT NULL DEFAULT 1, -- SOA serial
  refresh INT UNSIGNED NOT NULL DEFAULT 28800,
  retry INT UNSIGNED NOT NULL DEFAULT 7200,
  expire INT UNSIGNED NOT NULL DEFAULT 604800,
  minimum INT UNSIGNED NOT NULL DEFAULT 86400,
  ttl INT UNSIGNED NOT NULL DEFAULT 86400,
  active ENUM('Y','N') NOT NULL,
  xfer CHAR(255) NOT NULL
);

-- rr table (resource records)
CREATE TABLE rr (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  sys_userid INT NOT NULL,
  sys_groupid INT NOT NULL,
  sys_perm_user VARCHAR(5) NOT NULL,
  sys_perm_group VARCHAR(5) NOT NULL,
  sys_perm_other VARCHAR(5) NOT NULL,
  server_id INT NOT NULL DEFAULT 1,
  zone INT UNSIGNED NOT NULL,             -- FK to soa.id
  name CHAR(64) NOT NULL,                 -- Record name
  type ENUM('A','AAAA','ALIAS','CNAME','HINFO','MX','NAPTR','NS','PTR','RP','SRV','TXT','SPF'),
  data TEXT NOT NULL,                     -- Record data
  aux INT UNSIGNED NOT NULL DEFAULT 0,    -- Priority (for MX/SRV)
  ttl INT UNSIGNED NOT NULL DEFAULT 86400,
  active ENUM('N','Y') NOT NULL DEFAULT 'Y'
);

-- rr_zone table (record-to-zone mapping for geo distribution)
-- NOTE: This table links records to specific "geo zones" (not DNS zones)
CREATE TABLE rr_zone (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  rr_id INT UNSIGNED NOT NULL UNIQUE,     -- FK to rr.id
  zone_id INT UNSIGNED NOT NULL           -- Geo zone ID (2=ro2, 3=ro3, 4=de4)
);
```

### Usage Notes

1. **SOA serial**: Must be incremented on any zone change (controller handles this automatically)
2. **rr_zone mapping**: The `zone_id` in `rr_zone` represents geographic zones (2, 3, 4), not DNS zones
3. **Active flag**: Records with `active='N'` are not served by MyDNS

---

## Implementation Plan

### Phase 1: Core CRDs and Controller (Week 1-2)

**Deliverables:**
- [ ] Go project scaffolding with kubebuilder
- [ ] DNSZone CRD definition and controller
- [ ] DNSRecord CRD definition and controller
- [ ] Database client for MyDNS operations
- [ ] Basic reconciliation loop
- [ ] Unit tests for database operations

**Acceptance Criteria:**
- Creating a DNSZone CRD creates a zone in MyDNS
- Creating a DNSRecord CRD creates records in MyDNS
- Deleting CRDs removes corresponding database entries
- SOA serial is incremented on changes

### Phase 2: Health Monitoring (Week 3-4)

**Deliverables:**
- [ ] DNSHealthCheck CRD definition
- [ ] Health monitor deployment (one pod per zone)
- [ ] Node health checking via Kubernetes API
- [ ] Quorum consensus algorithm
- [ ] Database locking for safe updates
- [ ] Integration tests for failover scenarios

**Acceptance Criteria:**
- Health monitors detect when a zone's nodes are NotReady
- Quorum prevents action when monitor is isolated
- Records are removed/restored based on consensus
- No split-brain scenarios in testing

### Phase 3: ACME DNS-01 Webhook (Week 5-6)

> **STATUS: COMPLETED**
>
> Implementation available at [`cert-manager-webhook/`](../cert-manager-webhook/)

**Deliverables:**
- [x] cert-manager webhook implementation
- [x] TXT record creation/deletion
- [x] Zone discovery from FQDN
- [x] Helm chart for deployment
- [x] Forgejo CI/CD pipelines (build, test, release)
- [ ] Integration tests with cert-manager (manual testing done)

**Acceptance Criteria:**
- [x] Certificates can be issued using DNS-01 challenges
- [x] TXT records are cleaned up after validation
- [x] Works with both staging and production Let's Encrypt

**Implementation Details:**
- **Location**: `cert-manager-webhook/`
- **API Group**: `acme.example.com`
- **Solver Name**: `mydns`
- **Container Registry**: `YOUR_REGISTRY/your-org/mydns-acme-webhook`
- **Helm OCI**: `oci://YOUR_REGISTRY/your-org/mydns-webhook`

### Phase 4: Dynamic IP Support (Week 7)

**Deliverables:**
- [ ] Dynamic IP source implementation
- [ ] External IP detection
- [ ] Node IP selection
- [ ] Service IP resolution
- [ ] Migration path from existing cronjob

**Acceptance Criteria:**
- Records can be configured to auto-update from detected IPs
- Existing cronjob functionality is replaced
- IP changes are detected within configured interval

### Phase 5: Observability (Week 8)

**Deliverables:**
- [ ] Prometheus metrics
- [ ] Grafana dashboard
- [ ] Alerting rules
- [ ] Audit logging
- [ ] Documentation

**Metrics to expose:**
```
mydns_zone_health{zone, target_zone}
mydns_records_total{zone, type}
mydns_failover_events_total{zone, action}
mydns_acme_challenges_total{result}
mydns_db_operations_total{operation, result}
mydns_db_latency_seconds{operation}
mydns_reconcile_duration_seconds{controller}
mydns_quorum_consensus{zone, healthy}
```

### Phase 6: External-DNS Compatibility (Optional, Week 9+)

**Deliverables:**
- [ ] Webhook provider interface for external-dns
- [ ] Documentation for external-dns integration

---

## Deployment Guide

### Prerequisites

- Kubernetes cluster with cert-manager installed
- MyDNS with MariaDB backend
- Network connectivity between controller and database

### Helm Installation

```bash
# Add helm repository
helm repo add mydns-controller https://your-org.github.io/mydns-controller

# Install the controller
helm install mydns-controller mydns-controller/mydns-controller \
  --namespace dns \
  --set database.secretRef.name=mydns-secret \
  --set database.secretRef.namespace=dns \
  --set healthMonitor.zones="{ro2,ro3,de4}"
```

### Configuration Values

```yaml
# values.yaml
replicaCount: 1

database:
  secretRef:
    name: mydns-secret
    namespace: dns
  # Keys expected in secret: host, user, pass, database

healthMonitor:
  enabled: true
  zones:
    - ro2
    - ro3
    - de4
  checkInterval: 30s
  failureThreshold: 3
  successThreshold: 2

acmeWebhook:
  enabled: true
  groupName: acme.example.com
  solverName: mydns

metrics:
  enabled: true
  serviceMonitor:
    enabled: true

resources:
  controller:
    requests:
      cpu: 100m
      memory: 128Mi
    limits:
      cpu: 500m
      memory: 256Mi
  healthMonitor:
    requests:
      cpu: 50m
      memory: 64Mi
    limits:
      cpu: 200m
      memory: 128Mi
```

---

## Future Improvements for MyDNS

> **Note**: These are improvements for the MyDNS application itself (separate repository).
> Review and prioritize these when working on the mydns-ng repository.

### High Priority

- [ ] **Native Kubernetes integration**: Add sidecar or init container support for reading config from ConfigMaps/Secrets
- [ ] **Health endpoint improvements**: Expose more detailed health status (database connectivity, query latency)
- [ ] **Metrics endpoint**: Native Prometheus metrics for query rates, cache hits, response times
- [ ] **DNSSEC support**: Investigate adding DNSSEC signing capabilities
- [ ] **Connection pooling**: Improve database connection handling for high-load scenarios

### Medium Priority

- [ ] **API endpoint**: REST API for record management (would simplify controller integration)
- [ ] **Zone transfer improvements**: Better AXFR/IXFR support for secondary DNS
- [ ] **Query logging**: Structured logging for DNS queries (for debugging and analytics)
- [ ] **Cache statistics**: Expose cache hit/miss rates
- [ ] **Graceful reload**: Support config reload without restart

### Low Priority

- [ ] **Web UI**: Simple admin interface for zone/record management
- [ ] **DNS-over-HTTPS (DoH)**: Support for encrypted DNS queries
- [ ] **DNS-over-TLS (DoT)**: Alternative encrypted transport
- [ ] **EDNS Client Subnet**: For CDN-style geo-routing
- [ ] **Response Rate Limiting**: Protection against DNS amplification attacks

### Database Schema Improvements

- [ ] **Add metadata columns**: `created_at`, `updated_at`, `created_by` for audit trail
- [ ] **Add zone_id to rr table directly**: Simplify queries by denormalizing (keep rr_zone for geo)
- [ ] **Add labels/tags**: For organizing records (e.g., "managed-by: mydns-controller")
- [ ] **Add version column**: For optimistic locking

### Operational Improvements

- [ ] **Helm chart for MyDNS**: Package MyDNS itself as a Helm chart
- [ ] **High availability**: Document/implement active-passive or active-active setup
- [ ] **Backup/restore**: Scripts for database backup and zone export/import
- [ ] **Migration tools**: Import from BIND zone files, other DNS servers

---

## References

- [cert-manager webhook documentation](https://cert-manager.io/docs/configuration/acme/dns01/webhook/)
- [cert-manager webhook-example](https://github.com/cert-manager/webhook-example)
- [MyDNS-NG documentation](http://mydns-ng.com/)
- [Kubernetes API conventions](https://github.com/kubernetes/community/blob/master/contributors/devel/sig-architecture/api-conventions.md)
- [kubebuilder book](https://book.kubebuilder.io/)

### Implementation References

- **DNS-01 Webhook Implementation**: [`cert-manager-webhook/`](../cert-manager-webhook/)
- **Webhook README**: [`cert-manager-webhook/README.md`](../cert-manager-webhook/README.md)
- **Webhook Planning Documents**: [`docs/webhook-planning/`](./webhook-planning/)

---

## Appendix: Example Manifests

### Complete DNSZone Example

```yaml
apiVersion: dns.example.com/v1alpha1
kind: DNSZone
metadata:
  name: example-com
  namespace: dns
spec:
  origin: example.com
  ns: ns1.example.com
  mbox: admin.example.com
  refresh: 28800
  retry: 7200
  expire: 604800
  minimum: 86400
  ttl: 86400
  active: true
  failover:
    enabled: true
    zones:
      - name: ro2
        weight: 100
        enabled: true
      - name: ro3
        weight: 100
        enabled: true
      - name: de4
        weight: 50
        enabled: true
```

### Complete DNSRecord Example with Geo-Failover

```yaml
apiVersion: dns.example.com/v1alpha1
kind: DNSRecord
metadata:
  name: example-com-apex
  namespace: dns
spec:
  zoneRef:
    name: example-com
  name: "@"
  type: A
  ttl: 300
  active: true
  zoneTargets:
    ro2:
      targets:
        - 82.77.186.233
      enabled: true
    ro3:
      targets:
        - 5.2.225.29
      enabled: true
    de4:
      targets:
        - 157.173.110.230
      enabled: true
```

### Certificate with DNS-01 Challenge

```yaml
apiVersion: cert-manager.io/v1
kind: Certificate
metadata:
  name: wildcard-example-com
  namespace: default
spec:
  secretName: wildcard-example-com-tls
  issuerRef:
    name: letsencrypt-prod
    kind: ClusterIssuer
  dnsNames:
    - "*.example.com"
    - "example.com"
```
