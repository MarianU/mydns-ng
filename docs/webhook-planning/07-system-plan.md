# System Plan: DNS-01 Webhook Provider for mydns-ng

## Executive Summary

This document describes the architecture and implementation plan for a DNS-01 ACME webhook provider that integrates cert-manager with the mydns-ng DNS server. The webhook enables automated TLS certificate issuance via Let's Encrypt by creating and managing DNS TXT records for domain validation challenges directly in the MyDNS MariaDB database.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Kubernetes Cluster                                 │
│                                                                             │
│  ┌──────────────────┐         ┌─────────────────────────────────────────┐  │
│  │                  │         │         mydns-acme-webhook              │  │
│  │   cert-manager   │         │                                         │  │
│  │                  │  HTTPS  │  ┌─────────────────────────────────┐   │  │
│  │  ┌────────────┐  │ ──────► │  │     Webhook API Server          │   │  │
│  │  │ ACME       │  │         │  │     (Kubernetes APIService)     │   │  │
│  │  │ Controller │  │         │  └──────────────┬──────────────────┘   │  │
│  │  └────────────┘  │         │                 │                       │  │
│  │                  │         │  ┌──────────────▼──────────────────┐   │  │
│  │  ┌────────────┐  │         │  │     DNS01 Solver                │   │  │
│  │  │ Challenge  │  │         │  │     - Present()                 │   │  │
│  │  │ Controller │  │         │  │     - CleanUp()                 │   │  │
│  │  └────────────┘  │         │  │     - Initialize()              │   │  │
│  └──────────────────┘         │  └──────────────┬──────────────────┘   │  │
│                               │                 │                       │  │
│  ┌──────────────────┐         │  ┌──────────────▼──────────────────┐   │  │
│  │  Secret          │ ◄───────┤  │     MyDNS Database Client       │   │  │
│  │  (DB credentials)│         │  │     - Zone lookup               │   │  │
│  └──────────────────┘         │  │     - TXT record CRUD           │   │  │
│                               │  └──────────────┬──────────────────┘   │  │
│                               └─────────────────┼───────────────────────┘  │
│                                                 │                          │
└─────────────────────────────────────────────────┼──────────────────────────┘
                                                  │ TCP/3306
                                                  ▼
                               ┌─────────────────────────────────────────┐
                               │            MariaDB                      │
                               │   ┌─────────┐  ┌─────────┐             │
                               │   │   soa   │  │   rr    │             │
                               │   │ (zones) │  │(records)│             │
                               │   └─────────┘  └─────────┘             │
                               └─────────────────────────────────────────┘
```

## Major Components

| Component | Purpose | Key Responsibilities |
|-----------|---------|---------------------|
| `main.go` | Entry point | Register solver, start webhook server |
| `solver.go` | DNS01 Solver | Implement Present/CleanUp/Initialize |
| `database.go` | DB operations | Zone lookup, TXT record CRUD |
| `config.go` | Configuration | Parse webhook config, load secrets |
| Helm Chart | Deployment | RBAC, Service, Deployment, APIService |

## Data Flow for DNS-01 Challenge

```
User              cert-manager         ACME Server        Webhook           MariaDB
  │                    │                    │                │                 │
  │ Create Certificate │                    │                │                 │
  │───────────────────►│                    │                │                 │
  │                    │ Request Challenge  │                │                 │
  │                    │───────────────────►│                │                 │
  │                    │◄───────────────────│                │                 │
  │                    │ DNS-01 Challenge   │                │                 │
  │                    │                    │                │                 │
  │                    │ POST /present      │                │                 │
  │                    │────────────────────────────────────►│                 │
  │                    │                    │                │ SELECT zone     │
  │                    │                    │                │────────────────►│
  │                    │                    │                │ INSERT TXT      │
  │                    │                    │                │────────────────►│
  │                    │◄────────────────────────────────────│                 │
  │                    │                    │                │                 │
  │                    │                    │ Verify DNS     │                 │
  │                    │                    │────────────────────────────────►│
  │                    │◄───────────────────│                │                 │
  │                    │ Challenge valid    │                │                 │
  │                    │                    │                │                 │
  │                    │ POST /cleanup      │                │                 │
  │                    │────────────────────────────────────►│                 │
  │                    │                    │                │ DELETE TXT      │
  │                    │                    │                │────────────────►│
  │                    │◄────────────────────────────────────│                 │
  │◄───────────────────│                    │                │                 │
  │ Certificate Ready  │                    │                │                 │
```

## Security Considerations

### RBAC Configuration

```yaml
# Webhook ServiceAccount permissions
rules:
  - apiGroups: [""]
    resources: ["secrets"]
    verbs: ["get"]
    resourceNames: ["mydns-db-credentials"]

# Grant cert-manager access to webhook API
rules:
  - apiGroups: ["acme.example.com"]
    resources: ["*"]
    verbs: ["create"]
```

### Threat Mitigations

| Threat | Mitigation |
|--------|------------|
| Unauthorized certificate issuance | RBAC restricts webhook access |
| Database credential exposure | Secrets mounted read-only |
| SQL injection | Parameterized queries only |
| Zone hijacking | Optional `allowedZones` config |

## Deployment Architecture

### Kubernetes Resources

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        cert-manager namespace                           │
│                                                                         │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐ │
│  │   Deployment    │  │    Service      │  │      APIService         │ │
│  │ mydns-webhook   │◄─┤ mydns-webhook   │◄─┤ v1alpha1.acme.mydns.io  │ │
│  │ replicas: 1     │  │ ClusterIP:443   │  │                         │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────┘ │
│                                                                         │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐ │
│  │ Secret (TLS)    │  │ Secret (DB)     │  │    ServiceAccount       │ │
│  │ serving-cert    │  │ db-credentials  │  │    mydns-webhook        │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

## Failure Modes and Recovery

| Failure | Impact | Detection | Recovery |
|---------|--------|-----------|----------|
| Database unreachable | All challenges fail | Health probe fails | Auto-reconnect |
| Zone not found | Specific challenge fails | Error in logs | Check zone exists |
| Webhook pod crash | Challenges queue | K8s restarts pod | Automatic |
| TLS cert expired | All challenges fail | Health probe fails | cert-manager renews |

## Phases & Milestones

| Phase | Deliverables | Duration |
|-------|--------------|----------|
| Phase 1 | Solver interface, DB client, Present/CleanUp | 1 week |
| Phase 2 | Secret loading, per-Issuer config, RBAC | 3 days |
| Phase 3 | Conformance tests, integration tests | 1 week |
| Phase 4 | Complete Helm chart, documentation | 3 days |
| Phase 5 | Metrics, logging, alerting rules | 3 days |
