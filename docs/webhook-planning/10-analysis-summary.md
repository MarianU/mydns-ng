# Analysis Summary: DNS-01 Webhook Provider for mydns-ng

## Executive Summary

This project aims to create a **cert-manager DNS-01 webhook** that enables automated TLS certificate issuance via Let's Encrypt for domains managed by **MyDNS-NG**, a SQL-backed DNS server. The webhook will directly manipulate TXT records in the MyDNS MariaDB database to complete ACME DNS-01 challenges.

## Key Findings

### 1. Project Scope
- Create cert-manager DNS-01 webhook as subdirectory in mydns-ng
- Enable automated TLS certificate issuance via Let's Encrypt
- Direct database integration with MyDNS-NG's MariaDB backend

### 2. Technical Approach
- **Language**: Go 1.21+ (matching cert-manager ecosystem)
- **Interface**: Implement `webhook.Solver` with `Present()`, `CleanUp()`, `Initialize()`
- **Database**: Direct SQL to `soa` (zones) and `rr` (records) tables
- **Deployment**: Helm chart, Kubernetes APIService
- **API Group**: `acme.example.com`
- **Solver Name**: `mydns`

### 3. Existing Resources
- Webhook template provides ~80% of scaffolding
- Complete technical specification exists in `docs/mydns-controller-spec.md`

### 4. Estimated Effort
- **Total**: 89 story points (~6-8 weeks)
- Phase 1 (Foundation): 10 points
- Phase 2 (Core Implementation): 24 points
- Phase 3 (Package & Deploy): 26 points
- Phase 4 (Testing & Docs): 29 points

## Technical Direction

```
┌─────────────────────────────────────────────────────────────┐
│                    cert-manager-webhook                      │
├─────────────────────────────────────────────────────────────┤
│  main.go                                                     │
│    └── cmd.RunWebhookServer()                               │
│         └── mydnsSolver{}                                   │
│              ├── Initialize() → DB connection + K8s client  │
│              ├── Present()    → INSERT TXT + UPDATE serial  │
│              └── CleanUp()    → DELETE TXT + UPDATE serial  │
├─────────────────────────────────────────────────────────────┤
│  pkg/mydns/                                                  │
│    ├── client.go   → Database connection management         │
│    ├── zone.go     → Zone discovery from FQDN               │
│    └── record.go   → TXT record CRUD operations             │
├─────────────────────────────────────────────────────────────┤
│  deploy/mydns-webhook/                                       │
│    ├── Chart.yaml                                           │
│    ├── values.yaml → groupName: acme.example.com          │
│    └── templates/  → Deployment, RBAC, Service, PKI         │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Sequence

```
Week 1: Foundation
├── Create repository structure
├── Implement database client package
├── Implement zone discovery
└── Basic Present()/CleanUp() implementation

Week 2: Integration
├── Kubernetes Secret reading for DB credentials
├── SOA serial increment logic
├── Idempotency handling
└── Error handling and logging

Week 3: Deployment & Testing
├── Customize Helm chart
├── Run conformance test suite
├── Integration testing with real MyDNS
└── Documentation
```

## Risk Summary

| Risk | Severity | Mitigation |
|------|----------|------------|
| Database connection failures | High | Connection pooling, retry logic |
| SOA serial race conditions | Medium | Use transactions |
| Zone discovery edge cases | Medium | Handle subdomains, wildcards |
| cert-manager API changes | Low | Pin version, monitor releases |

## SQL Operations Reference

```sql
-- Find zone ID
SELECT id FROM soa WHERE origin = 'example.com.' AND active = 'Y';

-- Insert TXT record
INSERT INTO rr (zone, name, type, data, ttl, active)
VALUES (1, '_acme-challenge', 'TXT', 'challenge-token', 60, 'Y');

-- Delete TXT record
DELETE FROM rr 
WHERE zone = 1 
  AND name = '_acme-challenge' 
  AND type = 'TXT' 
  AND data = 'challenge-token';

-- Increment SOA serial
UPDATE soa SET serial = serial + 1 WHERE id = 1;
```

## Example Configuration

### ClusterIssuer
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
              dbSecretRef:
                name: mydns-db-secret
                namespace: dns
```

### Database Secret
```yaml
apiVersion: v1
kind: Secret
metadata:
  name: mydns-db-secret
  namespace: dns
type: Opaque
stringData:
  host: "192.168.1.50"
  user: "mydns_webhook"
  password: "secure-password"
  database: "mydns"
```

## Next Steps

1. Create webhook subdirectory structure
2. Initialize Go module with dependencies
3. Implement database client package
4. Implement Solver interface
5. Create Helm chart
6. Write conformance tests
7. Test with Let's Encrypt staging
8. Document installation and configuration
