# Work Breakdown Structure: MyDNS-NG DNS-01 Cert-Manager Webhook

## Summary

| Metric | Value |
|--------|-------|
| Total Epics | 3 |
| Total Stories | 24 |
| Total Story Points | 89 |
| Estimated Duration | 3-4 Sprints (6-8 weeks) |

## Epic 1: Project Foundation & Infrastructure (18 points)

### Story 1.1.1: Create Webhook Subdirectory Structure (2 points)
**Tasks:**
- Create `cert-manager-webhook/` directory with subdirs
- Initialize Go module
- Create `go.mod` with dependencies
- Add `.gitignore`

### Story 1.1.2: Configure Build Tooling (3 points)
**Tasks:**
- Create Makefile with `build`, `test`, `clean` targets
- Add kubebuilder test asset download
- Add `docker-build` target
- Add `helm-template` target

### Story 1.2.1: Create Webhook Dockerfile (3 points)
**Tasks:**
- Create multi-stage Dockerfile
- Configure non-root user
- Add CA certificates
- Configure health check

### Story 1.3.1: Adapt Helm Chart from Template (5 points)
**Tasks:**
- Create `Chart.yaml`
- Create `values.yaml` with mydns config
- Update `_helpers.tpl`
- Create `deployment.yaml` with DB secret mounts
- Create `rbac.yaml`
- Create `apiservice.yaml`, `pki.yaml`, `service.yaml`
- Add database credentials Secret template

### Story 1.3.2: Add Configuration Values (3 points)
**Tasks:**
- Add `mydns.database.*` values
- Add `mydns.secretName` for existing secret
- Add table name configuration
- Document all values

## Epic 2: Core Webhook Implementation (47 points)

### Story 2.1.1: Create Database Client Interface (5 points)
**Tasks:**
- Define `Client` interface
- Define `Config` struct
- Define `Zone` and `Record` types
- Create `pkg/mydns/client.go`

### Story 2.1.2: Implement MySQL Client (8 points)
**Tasks:**
- Implement MySQL connection
- Implement `GetZone(origin)`
- Implement `CreateTXT(zone, name, data, ttl)`
- Implement `DeleteTXT(zone, name, data)`
- Implement `UpdateSerial(zone)`
- Add connection pooling
- Add retry logic

### Story 2.1.3: Implement PostgreSQL Client (5 points) - Optional
**Tasks:**
- Implement PostgreSQL connection
- Adapt SQL queries for PostgreSQL
- Handle PostgreSQL-specific types

### Story 2.2.1: Implement Solver Skeleton (3 points)
**Tasks:**
- Create `pkg/solver/solver.go`
- Implement `Name()` returning `"mydns"`
- Create stub methods
- Define `mydnsProviderConfig` struct

### Story 2.2.2: Implement Initialize Method (5 points)
**Tasks:**
- Read database config from environment
- Initialize Kubernetes clientset
- Create database client instance
- Handle graceful shutdown
- Add connection health check

### Story 2.2.3: Implement Present Method (8 points)
**Tasks:**
- Parse `ChallengeRequest`
- Load issuer-specific config
- Fetch credentials from Secret
- Determine zone from `ResolvedZone`
- Extract record name
- Call `client.CreateTXT()`
- Handle idempotency
- Update SOA serial

### Story 2.2.4: Implement CleanUp Method (5 points)
**Tasks:**
- Parse `ChallengeRequest`
- Call `client.DeleteTXT()`
- Handle missing record gracefully
- Update SOA serial
- Log cleanup actions

### Story 2.3.1: Implement Secret-Based Credentials (5 points)
**Tasks:**
- Define `SecretKeySelector` in config
- Implement secret fetching
- Support namespace-scoped and cluster-scoped secrets
- Cache credentials
- Add RBAC for secret access

### Story 2.3.2: Implement Provider Config Parsing (3 points)
**Tasks:**
- Define complete config struct
- Implement JSON unmarshaling
- Support optional fields with defaults
- Document config schema

### Story 2.4.1: Implement Main Entry Point (2 points)
**Tasks:**
- Create `cmd/webhook/main.go`
- Read GROUP_NAME from environment
- Register solver
- Add version/build info flags

## Epic 3: Testing & Documentation (24 points)

### Story 3.1.1: Create Database Client Unit Tests (5 points)
**Tasks:**
- Create mock database client
- Test `GetZone()` with various inputs
- Test `CreateTXT()` success and error cases
- Test `DeleteTXT()` success and error cases
- Test connection error handling

### Story 3.1.2: Create Solver Unit Tests (5 points)
**Tasks:**
- Test `Present()` with mock client
- Test `CleanUp()` with mock client
- Test config parsing edge cases
- Test zone/name extraction logic

### Story 3.2.1: Set Up Conformance Test Framework (5 points)
**Tasks:**
- Create `main_test.go` with conformance fixture
- Create `testdata/mydns-solver/config.json`
- Set up MySQL container for tests
- Initialize test database with schema
- Configure test zone
- Run conformance tests

### Story 3.2.2: Add E2E Test Documentation (2 points)
**Tasks:**
- Document kind/minikube test setup
- Create example Certificate resource
- Document verification steps

### Story 3.3.1: Create User Documentation (5 points)
**Tasks:**
- Write README.md with overview
- Document prerequisites
- Write Helm installation instructions
- Document Issuer/ClusterIssuer configuration
- Add example Certificate resources
- Write troubleshooting section

### Story 3.3.2: Create Developer Documentation (2 points)
**Tasks:**
- Document development environment setup
- Document testing procedures
- Add CONTRIBUTING.md

## Critical Path

```
[Project Structure] → [Go Module] → [DB Client Interface]
                                          ↓
                     [MySQL Client] → [Solver Skeleton]
                                          ↓
                                    [Initialize Method]
                                          ↓
                                    [Present Method]
                                          ↓
                                    [CleanUp Method]
                                          ↓
                                    [Main Entry Point]
                                          ↓
                     [Dockerfile] → [Helm Chart] → [Conformance Tests]
                                                        ↓
                                                     [DONE]
```

## Sprint Planning (Suggested)

### Sprint 1 (10 points)
- Story 1.1.1: Project structure (2)
- Story 1.1.2: Build tooling (3)
- Story 2.1.1: DB client interface (5)

### Sprint 2 (24 points)
- Story 2.1.2: MySQL client (8)
- Story 2.2.1: Solver skeleton (3)
- Story 2.2.2: Initialize method (5)
- Story 2.2.3: Present method (8)

### Sprint 3 (26 points)
- Story 2.2.4: CleanUp method (5)
- Story 2.3.1: Secret credentials (5)
- Story 2.3.2: Config parsing (3)
- Story 2.4.1: Main entry point (2)
- Story 1.2.1: Dockerfile (3)
- Story 1.3.1: Helm chart (5)
- Story 1.3.2: Config values (3)

### Sprint 4 (29 points)
- Story 3.1.1: DB client tests (5)
- Story 3.1.2: Solver tests (5)
- Story 3.2.1: Conformance tests (5)
- Story 3.2.2: E2E docs (2)
- Story 3.3.1: User docs (5)
- Story 3.3.2: Dev docs (2)
- Story 2.1.3: PostgreSQL (5) - optional
