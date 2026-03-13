# Requirements Specification: DNS-01 Webhook for mydns-ng

## 1. Introduction

### 1.1 Purpose

This document defines the formal requirements for implementing a DNS-01 ACME webhook provider that integrates cert-manager with mydns-ng DNS server.

### 1.2 Scope

**In Scope:**
- cert-manager webhook interface implementation
- TXT record management in mydns-ng database
- Kubernetes deployment and configuration

**Out of Scope:**
- mydns-ng DNS server modifications
- Other DNS record types

## 2. Functional Requirements

### FR-001: Solver Interface Implementation

| Attribute | Value |
|-----------|-------|
| **Description** | Implement cert-manager `Solver` interface |
| **Priority** | Must Have |

**Acceptance Criteria:**
- [ ] Implements `Name() string` method
- [ ] Implements `Present(ch *ChallengeRequest) error` method
- [ ] Implements `CleanUp(ch *ChallengeRequest) error` method
- [ ] Implements `Initialize(kubeClientConfig *rest.Config, stopCh <-chan struct{}) error` method

### FR-002: Solver Name

| Attribute | Value |
|-----------|-------|
| **Description** | Return `"mydns"` as solver name |
| **Priority** | Must Have |

### FR-003: Group Name

| Attribute | Value |
|-----------|-------|
| **Description** | Use `acme.example.com` as API group name |
| **Priority** | Must Have |

### FR-004: Present TXT Record

| Attribute | Value |
|-----------|-------|
| **Description** | Create TXT record in mydns-ng database |
| **Priority** | Must Have |

**Acceptance Criteria:**
- [ ] Creates TXT record with name `_acme-challenge.<subdomain>`
- [ ] Sets record data to challenge key
- [ ] Sets TTL to 60 seconds (configurable)
- [ ] Sets `active` flag to `'Y'`
- [ ] Increments SOA serial number

### FR-005: CleanUp TXT Record

| Attribute | Value |
|-----------|-------|
| **Description** | Delete specific TXT record from database |
| **Priority** | Must Have |

**Acceptance Criteria:**
- [ ] Deletes only record matching name AND data
- [ ] Increments SOA serial number
- [ ] Returns success if record doesn't exist (idempotent)

### FR-006: Zone Discovery

| Attribute | Value |
|-----------|-------|
| **Description** | Resolve zone ID from FQDN |
| **Priority** | Must Have |

**Acceptance Criteria:**
- [ ] Handles trailing dot in zone name
- [ ] Returns error if zone not found
- [ ] Returns error if zone is inactive

### FR-007: Record Name Extraction

| Attribute | Value |
|-----------|-------|
| **Description** | Extract relative record name from FQDN |
| **Priority** | Must Have |

**Examples:**
| ResolvedFQDN | ResolvedZone | Extracted Name |
|--------------|--------------|----------------|
| `_acme-challenge.www.example.com.` | `example.com.` | `_acme-challenge.www` |
| `_acme-challenge.example.com.` | `example.com.` | `_acme-challenge` |

### FR-008: Database Connection

| Attribute | Value |
|-----------|-------|
| **Description** | Establish and maintain connection pool |
| **Priority** | Must Have |

**Acceptance Criteria:**
- [ ] Uses connection pooling
- [ ] Handles connection failures with retry
- [ ] Closes connections on shutdown

### FR-009: Database Credentials from Secrets

| Attribute | Value |
|-----------|-------|
| **Description** | Read credentials from Kubernetes Secret |
| **Priority** | Must Have |

**Secret Structure:**
```yaml
stringData:
  host: "192.168.1.50"
  port: "3306"
  user: "mydns"
  password: "secret"
  database: "mydns"
```

### FR-010: Configuration Parsing

| Attribute | Value |
|-----------|-------|
| **Description** | Parse solver config from ChallengeRequest |
| **Priority** | Must Have |

### FR-011: Health Check Endpoint

| Attribute | Value |
|-----------|-------|
| **Description** | Expose `/healthz` endpoint |
| **Priority** | Must Have |

### FR-012: Structured Logging

| Attribute | Value |
|-----------|-------|
| **Description** | Log all operations with structured JSON |
| **Priority** | Should Have |

## 3. Non-Functional Requirements

### NFR-001: Response Time

| Attribute | Value |
|-----------|-------|
| **Description** | Operations complete within 5 seconds |
| **Target** | p95 < 5 seconds |
| **Priority** | Must Have |

### NFR-002: Availability

| Attribute | Value |
|-----------|-------|
| **Description** | 99.9% availability |
| **Priority** | Must Have |

### NFR-003: TLS Communication

| Attribute | Value |
|-----------|-------|
| **Description** | All endpoints served over TLS |
| **Priority** | Must Have |

### NFR-004: Secrets Protection

| Attribute | Value |
|-----------|-------|
| **Description** | Never log credentials or challenge keys |
| **Priority** | Must Have |

### NFR-005: RBAC Permissions

| Attribute | Value |
|-----------|-------|
| **Description** | Minimal required permissions |
| **Priority** | Must Have |

### NFR-006: Database Retry

| Attribute | Value |
|-----------|-------|
| **Description** | Retry transient failures |
| **Target** | 3 retries with exponential backoff |
| **Priority** | Must Have |

### NFR-007: Startup Time

| Attribute | Value |
|-----------|-------|
| **Description** | Ready within 30 seconds |
| **Priority** | Must Have |

### NFR-008: Idempotent Operations

| Attribute | Value |
|-----------|-------|
| **Description** | Handle repeated calls gracefully |
| **Priority** | Must Have |

### NFR-009: Conformance Tests

| Attribute | Value |
|-----------|-------|
| **Description** | Pass cert-manager test suite |
| **Priority** | Must Have |

### NFR-010: Unit Test Coverage

| Attribute | Value |
|-----------|-------|
| **Description** | 80%+ coverage on core logic |
| **Priority** | Should Have |

## 4. Constraints

| ID | Constraint | Impact |
|----|------------|--------|
| C-001 | Must implement cert-manager webhook interface | Fixed API |
| C-002 | Must work with existing mydns-ng schema | Cannot modify tables |
| C-003 | Must deploy as Kubernetes API service | Requires TLS, RBAC |
| C-004 | Must use Go programming language | cert-manager ecosystem |
| C-005 | Must be deployable via Helm | Standard deployment |

## 5. Database Operations

```sql
-- Find zone
SELECT id FROM soa WHERE origin = ? AND active = 'Y';

-- Create TXT record
INSERT INTO rr (zone, name, type, data, ttl, active)
VALUES (?, ?, 'TXT', ?, ?, 'Y');

-- Delete TXT record
DELETE FROM rr 
WHERE zone = ? AND name = ? AND type = 'TXT' AND data = ?;

-- Update SOA serial
UPDATE soa SET serial = serial + 1 WHERE id = ?;
```
