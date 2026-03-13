# Idea Analysis: MyDNS-NG Cert-Manager Webhook

## Executive Summary

This project aims to create a Kubernetes cert-manager webhook that enables automated TLS certificate provisioning via DNS-01 ACME challenges, using MyDNS-NG (a SQL-backed DNS server) as the DNS provider. The webhook will bridge cert-manager's certificate automation with MyDNS-NG's database-driven DNS record management, enabling seamless Let's Encrypt certificate issuance for domains hosted on MyDNS-NG.

## Problem Statement

**Core Problem:** Organizations using MyDNS-NG as their DNS server cannot automatically obtain TLS certificates via cert-manager's DNS-01 challenge mechanism because no webhook integration exists for this custom DNS server.

**Pain Points:**
1. **Manual certificate management** - Without automation, TLS certificates must be manually requested, validated, and renewed
2. **DNS-01 is required for wildcards** - Wildcard certificates (`*.example.com`) can only be obtained via DNS-01 challenges, not HTTP-01
3. **No existing integration** - MyDNS-NG is not a mainstream DNS provider, so no cert-manager solver exists
4. **Operational overhead** - Manual DNS TXT record creation/deletion for ACME challenges is error-prone and time-consuming

## Value Proposition

| Aspect | Value Delivered |
|--------|-----------------|
| **Primary Value** | Fully automated TLS certificate lifecycle management for MyDNS-NG users |
| **Wildcard Support** | Enables wildcard certificates that HTTP-01 cannot provide |
| **Kubernetes Native** | Integrates seamlessly with cert-manager's declarative model |
| **Zero Downtime** | DNS-01 doesn't require service interruption (unlike HTTP-01) |
| **Self-Hosted** | Maintains control over DNS infrastructure without third-party dependencies |

## Core Concepts

### 1. DNS-01 ACME Challenge
The ACME protocol's DNS-01 challenge proves domain ownership by requiring the creation of a specific TXT record (`_acme-challenge.<domain>`) with a token value. The CA verifies this record exists before issuing the certificate.

### 2. Cert-Manager Webhook Interface
A Kubernetes API extension that implements the `Solver` interface with three key methods:
- `Present()` - Create the DNS TXT record
- `CleanUp()` - Delete the DNS TXT record  
- `Initialize()` - Set up database connections

### 3. MyDNS-NG Database Schema
DNS records stored in SQL tables:
- **`soa` table** - Zone definitions (domains)
- **`rr` table** - Resource records (A, AAAA, TXT, MX, etc.)

### 4. Challenge Flow
```
cert-manager → Webhook.Present() → INSERT TXT record → MyDNS serves record
    ↓
ACME CA queries DNS → Verifies TXT record → Issues certificate
    ↓
cert-manager → Webhook.CleanUp() → DELETE TXT record
```

## Scope Definition

### In Scope
- Go-based webhook implementing cert-manager's Solver interface
- Direct MySQL/PostgreSQL database connectivity
- TXT record creation and deletion for `_acme-challenge` records
- Helm chart for Kubernetes deployment
- Configuration via Kubernetes Secrets (database credentials)
- Conformance test suite integration
- Dockerfile for containerized deployment

### Out of Scope
- Modifications to MyDNS-NG core DNS server code
- Support for other DNS record types (only TXT needed)
- Web UI for webhook management
- Multi-database support in single deployment
- DNS server functionality (webhook is separate from DNS serving)

## Open Questions

1. **Database Selection:** Should the webhook support both MySQL and PostgreSQL, or focus on one?
2. **Zone Resolution:** How should the webhook find the correct zone ID for a given FQDN?
3. **SOA Serial:** Should inserting/deleting TXT records increment the zone's SOA serial number?
4. **Credential Storage:** Should database credentials come from Kubernetes Secret reference?
5. **Record Cleanup:** What happens if CleanUp fails? Should there be a garbage collection mechanism?

## Architecture Sketch

```
┌─────────────────────────────────────────────────────────────────┐
│                        Kubernetes Cluster                        │
│  ┌─────────────────┐         ┌─────────────────────────────┐   │
│  │   cert-manager  │         │    mydns-ng-webhook         │   │
│  │                 │ ──────► │  (Deployment + Service)     │   │
│  │  - Issuer       │ Present │                             │   │
│  │  - Certificate  │ CleanUp │  ┌─────────────────────┐   │   │
│  └─────────────────┘         │  │  Solver Interface   │   │   │
│                              │  │  - Present()        │   │   │
│                              │  │  - CleanUp()        │   │   │
│                              │  │  - Initialize()     │   │   │
│                              │  └──────────┬──────────┘   │   │
│                              └─────────────┼───────────────┘   │
│                                            │ SQL                │
└────────────────────────────────────────────┼────────────────────┘
                                             │
                                             ▼
                              ┌──────────────────────────┐
                              │   MySQL / PostgreSQL     │
                              │   ┌──────────────────┐  │
                              │   │ soa table        │  │
                              │   │ (zones)          │  │
                              │   └──────────────────┘  │
                              │   ┌──────────────────┐  │
                              │   │ rr table         │  │
                              │   │ (TXT records)    │  │
                              │   └──────────────────┘  │
                              └──────────────────────────┘
```

## Success Criteria (Draft)

- [ ] Webhook passes cert-manager conformance test suite
- [ ] Successfully issues a certificate for a test domain via DNS-01
- [ ] Successfully issues a wildcard certificate (`*.example.com`)
- [ ] CleanUp removes TXT records after certificate issuance
- [ ] Helm chart deploys successfully to Kubernetes
- [ ] Documentation covers installation and configuration
