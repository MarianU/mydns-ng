# DNS-01 Cert-Manager Webhook: Extraction & Reuse Analysis

## Executive Summary

The webhook project requires creating a Go-based DNS-01 ACME solver that manages TXT records in MyDNS-NG's MySQL/PostgreSQL database. The analysis reveals significant reuse opportunities from both repositories, with the webhook template providing ~80% of the scaffolding and MyDNS-NG providing the database schema knowledge.

## 1. Reusable Components from mydns-ng

### Database Schema (DIRECT REUSE)

| Component | Source | Reuse Type |
|-----------|--------|------------|
| RR table structure | `src/lib/mydns.h` | Reference for Go structs |
| SOA table structure | `src/lib/mydns.h` | Reference for Go structs |
| Active field values | `src/lib/rr.c` | Copy values: "Y", "N", "D" |
| TXT constraints | `src/lib/mydns.h` | `DNS_MAXTXTLEN=2048` |

**Go Struct to Create:**
```go
type RRRecord struct {
    ID     uint32 `db:"id"`
    Zone   uint32 `db:"zone"`
    Name   string `db:"name"`
    Data   string `db:"data"`
    Aux    uint32 `db:"aux"`
    TTL    uint32 `db:"ttl"`
    Type   string `db:"type"`
    Active string `db:"active"`
}
```

### SQL Query Patterns (ADAPT)

```sql
-- DELETE
DELETE FROM rr WHERE zone=? AND name=? AND type=? AND data=?

-- INSERT  
INSERT INTO rr (zone,name,type,data,ttl,active) VALUES (?,?,?,?,?,?)

-- Zone lookup
SELECT id FROM soa WHERE origin = ? AND active = 'Y';
```

## 2. Reusable Components from Webhook Template

### Complete Webhook Scaffolding (DIRECT REUSE)

| Component | Source | Reuse Type |
|-----------|--------|------------|
| Main entry point | `main.go` | Copy & modify |
| Solver interface | `main.go` | Extend with DB fields |
| Config loading | `main.go` | Copy & extend |
| K8s client setup | `main.go` | Copy directly |
| Test framework | `main_test.go` | Copy & configure |

### Helm Chart (DIRECT REUSE)

| Component | Source | Modifications Needed |
|-----------|--------|---------------------|
| `Chart.yaml` | `deploy/example-webhook/` | Update name/version |
| `values.yaml` | `deploy/example-webhook/` | Add DB config values |
| `templates/*` | `deploy/example-webhook/templates/` | Add DB secret mount |

### Build Infrastructure (DIRECT REUSE)

| Component | Source | Modifications |
|-----------|--------|---------------|
| `Dockerfile` | Root | Add MySQL client libs |
| `Makefile` | Root | Update image name |
| `go.mod` | Root | Add database driver |

## 3. Extraction Opportunities

### MyDNS Go Client Library
```go
package mydns

type Client interface {
    GetZone(origin string) (*SOA, error)
    CreateTXTRecord(zone uint32, name, data string, ttl uint32) error
    DeleteTXTRecord(zone uint32, name, data string) error
    GetTXTRecords(zone uint32, name string) ([]RR, error)
}

type Config struct {
    Host     string
    Port     int
    User     string
    Password string
    Database string
    SOATable string
    RRTable  string
}
```

## 4. Effort Estimation

### With Reuse Strategy

| Task | Effort | Notes |
|------|--------|-------|
| Copy webhook template | 0.5 days | Rename, update go.mod |
| Create MyDNS client | 2 days | Core DB operations |
| Implement solver | 1 day | Present/CleanUp logic |
| Extend Helm chart | 0.5 days | Add DB config |
| Testing & integration | 1 day | With real MyDNS |
| **Total** | **5 days** | |

### Without Reuse (Greenfield)

| Task | Effort |
|------|--------|
| Webhook framework | 3 days |
| Database client | 2 days |
| Helm charts | 2 days |
| Testing framework | 2 days |
| **Total** | **9 days** |

### **Effort Savings: ~45% (4 days saved)**

## 5. Recommended Project Structure

```
mydns-ng/
└── cert-manager-webhook/
    ├── main.go                       # From template (modified)
    ├── main_test.go                  # From template (modified)
    ├── go.mod
    ├── go.sum
    ├── Dockerfile                    # From template (add DB drivers)
    ├── Makefile                      # From template (update names)
    ├── README.md
    ├── deploy/
    │   └── mydns-webhook/            # From template (modified)
    │       ├── Chart.yaml
    │       ├── values.yaml
    │       └── templates/
    └── testdata/
        └── mydns-solver/
            └── config.json
```

## 6. Dependencies to Add

```go
// go.mod additions
require (
    github.com/go-sql-driver/mysql v1.7.1
    github.com/cert-manager/cert-manager v1.13.3
    k8s.io/client-go v0.28.1
)
```
