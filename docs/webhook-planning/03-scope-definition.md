# Scope Definition: DNS-01 ACME Webhook for MyDNS-NG

## Executive Summary

This project creates a **DNS-01 ACME challenge webhook** as a subdirectory (`cert-manager-webhook/`) within the existing `mydns-ng` repository. The webhook enables cert-manager to automatically obtain SSL/TLS certificates from Let's Encrypt by creating and deleting TXT records in the MyDNS database.

## Project Objectives

| Objective | Success Metric | Priority |
|-----------|----------------|----------|
| Enable DNS-01 challenges for cert-manager | Successfully issue wildcard certificate via Let's Encrypt | **Must Have** |
| Create/delete TXT records in MyDNS database | TXT records appear/disappear within 5 seconds | **Must Have** |
| Pass cert-manager conformance tests | `make test` passes | **Must Have** |
| Deployable via Helm chart | `helm install` works | **Must Have** |
| Secure credential handling | Database credentials from Kubernetes Secret | **Must Have** |

## Scope Boundaries

### In Scope

| Category | Included | Priority |
|----------|----------|----------|
| **Features** | Present() implementation - Create TXT record | Must Have |
| **Features** | CleanUp() implementation - Delete TXT record | Must Have |
| **Features** | Initialize() implementation - DB connection | Must Have |
| **Features** | Zone lookup from FQDN | Must Have |
| **Features** | Secret-based credentials | Must Have |
| **Features** | Health endpoint `/healthz` | Must Have |
| **Data** | TXT records only | Must Have |
| **Integrations** | cert-manager webhook API | Must Have |
| **Integrations** | MySQL/MariaDB | Must Have |
| **Platforms** | Kubernetes | Must Have |
| **Platforms** | linux/amd64, linux/arm64 | Must Have |

### Out of Scope

| Category | Excluded | Reason |
|----------|----------|--------|
| **Features** | DNSZone CRD | Part of mydns-controller, not webhook |
| **Features** | DNSRecord CRD | Part of mydns-controller |
| **Features** | Health monitoring | Separate component |
| **Features** | Dynamic IP updates | Part of mydns-controller |
| **Features** | PostgreSQL support | Focus on MariaDB first |
| **Features** | Web UI | Not needed for webhook |
| **Data** | A/AAAA/MX/etc records | Only TXT for ACME |
| **Data** | Zone creation | Zones must pre-exist |

## Constraints

### Technical Constraints

| Constraint | Details |
|------------|---------|
| cert-manager webhook API | Must implement `Solver` interface |
| MyDNS database schema | Must work with existing `soa` and `rr` tables |
| `active` column values | Must set `active='Y'` for records to be served |
| TLS required | Webhook runs as K8s API service |
| Go version | Go 1.21+ |

### Organizational Constraints

| Constraint | Details |
|------------|---------|
| Subdirectory location | Inside `mydns-ng` repository |
| Group name | `acme.example.com` |
| Solver name | `mydns` |

## Assumptions

| ID | Assumption | If False... |
|----|------------|-------------|
| A-001 | MyDNS database is accessible from K8s cluster | Need network configuration |
| A-002 | Zones already exist in MyDNS | Must create zones first |
| A-003 | cert-manager v1.13+ is installed | May need API version adjustments |
| A-004 | Database credentials in K8s Secret | Alternative needed |
| A-005 | TXT record TTL of 60s is acceptable | Make configurable |

## Dependencies

### Upstream Dependencies

| Dependency | Owner | Status |
|------------|-------|--------|
| MyDNS database running | Infrastructure | Assumed ready |
| DNS zones created | DNS admin | Manual step |
| cert-manager installed | Platform team | Assumed ready |
| Database credentials Secret | Operator | Manual step |

## File Structure (Proposed)

```
mydns-ng/
├── cert-manager-webhook/           # NEW SUBDIRECTORY
│   ├── main.go                     # Entry point
│   ├── main_test.go                # Conformance tests
│   ├── go.mod
│   ├── go.sum
│   ├── Dockerfile
│   ├── Makefile
│   ├── README.md
│   ├── deploy/
│   │   └── mydns-webhook/          # Helm chart
│   │       ├── Chart.yaml
│   │       ├── values.yaml
│   │       └── templates/
│   └── testdata/
│       └── my-custom-solver/
│           └── config.json
└── ... (existing mydns-ng files)
```

## Verification Criteria

### Definition of Done

- [ ] Webhook compiles without errors
- [ ] Docker image builds successfully
- [ ] Helm chart installs without errors
- [ ] cert-manager conformance tests pass
- [ ] Can issue certificate for test domain via Let's Encrypt staging
- [ ] TXT records created and cleaned up correctly
- [ ] README documents all configuration options
