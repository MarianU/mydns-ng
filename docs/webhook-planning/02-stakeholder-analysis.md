# Stakeholder Analysis: MyDNS DNS-01 ACME Webhook Provider

## Executive Summary

This project creates a DNS-01 ACME challenge webhook provider for cert-manager that integrates with MyDNS-NG, a SQL-based DNS server.

## Primary Stakeholders

### 1. Platform/Infrastructure Engineers (Kubernetes Operators)

| Attribute | Details |
|-----------|---------|
| **Needs** | Automated TLS certificate management, reduced manual intervention, GitOps-compatible DNS management |
| **Success Criteria** | Zero manual certificate renewals, <5 min certificate issuance, 99.9% webhook availability |
| **Concerns** | Webhook reliability, database connectivity issues |
| **Influence Level** | **High** - Deploy, configure, and maintain the webhook |

### 2. Application Developers (Certificate Consumers)

| Attribute | Details |
|-----------|---------|
| **Needs** | Simple certificate requests via annotations/CRDs, wildcard certificate support |
| **Success Criteria** | Can request certificates without DNS knowledge, certificates auto-renew |
| **Concerns** | Certificate issuance failures blocking deployments |
| **Influence Level** | **Medium** - Primary consumers |

### 3. MyDNS-NG Maintainers/Administrators

| Attribute | Details |
|-----------|---------|
| **Needs** | Minimal changes to existing MyDNS setup, database schema compatibility |
| **Success Criteria** | Webhook integrates without MyDNS modifications, DNS query performance unchanged |
| **Concerns** | Database locking issues, schema changes |
| **Influence Level** | **High** - Control the DNS infrastructure |

### 4. Security Team

| Attribute | Details |
|-----------|---------|
| **Needs** | Secure credential management, audit logging, principle of least privilege |
| **Success Criteria** | Secrets stored in Kubernetes Secrets, RBAC properly configured |
| **Concerns** | Database credentials in webhook, TXT record manipulation as attack vector |
| **Influence Level** | **High** - Can block deployment if security requirements not met |

### 5. SRE/On-Call Team

| Attribute | Details |
|-----------|---------|
| **Needs** | Clear alerting, runbooks, metrics dashboards, easy troubleshooting |
| **Success Criteria** | Prometheus metrics exposed, alerts for webhook failures |
| **Concerns** | Silent failures, cascading certificate expiration |
| **Influence Level** | **Medium** - Will be paged when things break |

## Stakeholder Map (Power/Interest Grid)

```
HIGH POWER
    │
    │  Keep Satisfied         │   Manage Closely
    │                         │
    │  • Security Team        │   • Platform Engineers
    │  • DBAs                 │   • Project Owner
    │  • Network Team         │   • MyDNS Administrators
    │                         │   • Application Developers
    │                         │   • SRE/On-Call Team
────┼─────────────────────────┼────────────────────────────
    │                         │
    │  Monitor                │   Keep Informed
    │                         │
    │  • End Users            │   • cert-manager Community
    │  • Future Maintainers   │   • Let's Encrypt
    │                         │
LOW POWER                   LOW ──────────────────────── HIGH
                               INTEREST
```

## RACI Matrix

| Activity | Project Owner | Platform Eng | MyDNS Admin | Security | SRE |
|----------|---------------|--------------|-------------|----------|-----|
| Requirements Definition | A | C | C | C | I |
| Architecture Design | A | R | C | C | C |
| Database Schema Review | A | C | R | I | I |
| Security Review | A | C | I | R | I |
| Implementation | A | R | I | I | I |
| Testing/Validation | A | R | C | C | C |
| Deployment | A | R | C | C | C |
| Production Support | I | R | C | I | A |

**Legend:** R=Responsible, A=Accountable, C=Consulted, I=Informed

## Potential Conflicts

| Stakeholder 1 | Stakeholder 2 | Conflict Area | Resolution |
|---------------|---------------|---------------|------------|
| App Developers | Security Team | Simple UX vs strict RBAC | Implement both: simple UX with underlying security |
| Platform Engineers | DBAs | Aggressive caching vs minimal DB load | Efficient queries, connection pooling |
| SRE Team | Platform Engineers | Extensive alerting vs minimal noise | Tiered alerting: critical vs warning |
