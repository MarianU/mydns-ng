# Feature Plan: DNS-01 ACME Challenge Webhook for mydns-ng

## Summary

This feature implements a cert-manager DNS-01 ACME challenge webhook (`mydns-acme-webhook`) that integrates with the mydns-ng DNS server. The webhook enables automated TLS certificate issuance via Let's Encrypt by programmatically creating and cleaning up `_acme-challenge` TXT records in the MyDNS database.

## User Story

As a **Kubernetes cluster administrator**,
I want **automated DNS-01 ACME challenge resolution for mydns-ng managed domains**,
So that **I can obtain wildcard TLS certificates without manual DNS record management**.

## Acceptance Criteria

- [ ] `Present()` creates TXT record in MyDNS `rr` table with challenge token
- [ ] `CleanUp()` removes TXT record from MyDNS database
- [ ] Multiple concurrent certificate requests create separate TXT records
- [ ] Webhook correctly identifies zone and creates `_acme-challenge` TXT record
- [ ] Database credentials from Kubernetes Secret work
- [ ] Helm chart deploys all required Kubernetes resources
- [ ] Certificate successfully issued using `letsencrypt-prod` ClusterIssuer

## Technical Approach

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Kubernetes Cluster                               │
│                                                                          │
│  ┌──────────────────┐         ┌─────────────────────────────────────┐   │
│  │   cert-manager   │────────▶│      mydns-acme-webhook             │   │
│  │                  │         │                                     │   │
│  │  - Certificate   │  POST   │  - Solver.Present()                 │   │
│  │  - Issuer        │ /present│  - Solver.CleanUp()                 │   │
│  │  - Challenge     │ /cleanup│  - Solver.Initialize()              │   │
│  └──────────────────┘         └──────────────┬──────────────────────┘   │
│                                              │ SQL                       │
│  ┌──────────────────┐                        │                           │
│  │  Secret          │────────────────────────┘                           │
│  │  (db-credentials)│                        │                           │
│  └──────────────────┘                        ▼                           │
└──────────────────────────────────────────────┼───────────────────────────┘
                                               │
                                               ▼
                              ┌─────────────────────────────────────┐
                              │         MariaDB (MyDNS)             │
                              │   ┌─────┐  ┌────┐                   │
                              │   │ soa │  │ rr │                   │
                              │   └─────┘  └────┘                   │
                              └─────────────────────────────────────┘
```

### Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Database driver | `go-sql-driver/mysql` | MariaDB compatible |
| Zone discovery | Longest suffix match | Handles subdomains correctly |
| Record identification | zone+name+type+data | Allows multiple TXT records |
| Configuration source | Kubernetes Secret | Secure storage |
| Serial update | Increment existing | Matches MyDNS convention |

## Affected Components

| Component | Type of Change |
|-----------|---------------|
| `cert-manager-webhook/main.go` | New |
| `cert-manager-webhook/solver.go` | New |
| `cert-manager-webhook/database.go` | New |
| `cert-manager-webhook/config.go` | New |
| `cert-manager-webhook/deploy/` | New Helm chart |
| `cert-manager-webhook/Dockerfile` | New |
| `cert-manager-webhook/Makefile` | New |

## Data Flow

```
1. cert-manager → POST /present
   {
     "resolvedFQDN": "_acme-challenge.www.example.com.",
     "resolvedZone": "example.com.",
     "key": "abc123-challenge-token"
   }

2. Webhook → Query soa table
   SELECT id FROM soa WHERE origin = 'example.com.'

3. Webhook → Insert into rr table
   INSERT INTO rr (zone, name, type, data, ttl, active)
   VALUES (1, '_acme-challenge.www', 'TXT', 'abc123-challenge-token', 60, 'Y')

4. Webhook → Update SOA serial
   UPDATE soa SET serial = serial + 1 WHERE id = 1

5. ACME server verifies TXT record via DNS query

6. cert-manager → POST /cleanup
   DELETE FROM rr WHERE zone = 1 AND name = '_acme-challenge.www' 
                    AND type = 'TXT' AND data = 'abc123-challenge-token'
```

## Configuration

### Helm Values

```yaml
groupName: acme.example.com
solverName: mydns

database:
  secretRef:
    name: mydns-db-credentials
    namespace: dns

challengeTTL: 60

resources:
  requests:
    cpu: 50m
    memory: 64Mi
  limits:
    cpu: 200m
    memory: 128Mi
```

### Database Secret

```yaml
apiVersion: v1
kind: Secret
metadata:
  name: mydns-db-credentials
  namespace: dns
type: Opaque
stringData:
  host: "192.168.1.50"
  port: "3306"
  user: "mydns_webhook"
  password: "secure-password"
  database: "mydns"
```

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
```

## Testing Strategy

### BDD Scenarios

```gherkin
Feature: DNS-01 ACME Challenge Resolution

  Scenario: Successfully present a challenge
    Given a zone "example.com" exists in the MyDNS database
    When cert-manager requests a challenge for "_acme-challenge.www.example.com"
    Then a TXT record should be created in the rr table
    And the SOA serial should be incremented

  Scenario: Successfully cleanup a challenge
    Given a TXT record exists for "_acme-challenge.www.example.com"
    When cert-manager requests cleanup
    Then the TXT record should be deleted
    And the SOA serial should be incremented

  Scenario: Handle zone not found
    Given no zone exists for "unknown.com"
    When cert-manager requests a challenge
    Then the webhook should return an error
```

## Estimates

| Task | Estimate |
|------|----------|
| Project scaffolding | 2 hours |
| Solver implementation | 4 hours |
| Database client | 4 hours |
| Configuration loading | 2 hours |
| Helm chart | 4 hours |
| Unit tests | 4 hours |
| Conformance tests | 2 hours |
| Integration tests | 4 hours |
| Documentation | 2 hours |
| E2E testing | 4 hours |
| **Total** | **32 hours (~1 week)** |

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Database connection failures | High | Connection pooling, retry logic |
| Zone lookup failures | High | Clear error messages, validation |
| TXT record conflicts | Medium | Unique record identification |
| Helm chart complexity | Medium | Base on working template |
