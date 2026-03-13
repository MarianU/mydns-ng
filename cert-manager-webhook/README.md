# MyDNS-NG cert-manager Webhook

A [cert-manager](https://cert-manager.io/) ACME DNS-01 webhook solver for [MyDNS-NG](https://github.com/mydns-ng).

## Overview

This webhook enables automated DNS-01 ACME challenges for domains managed by MyDNS-NG, allowing cert-manager to obtain and renew TLS certificates from Let's Encrypt (or other ACME providers) for those domains.

## Prerequisites

- Kubernetes 1.25+
- cert-manager 1.13+
- Helm 3.x
- MyDNS-NG database (MySQL/MariaDB) accessible from the cluster
- Database user with INSERT/DELETE permissions on the `rr` table and UPDATE on `soa` table

## Installation

### 1. Create Database Credentials Secret

Create a secret containing your MyDNS database credentials:

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

### 2. Install the Webhook

```bash
helm install mydns-webhook ./deploy/mydns-webhook \
  --namespace cert-manager
```

### 3. Configure ClusterIssuer

Create a ClusterIssuer that uses this webhook:

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
            groupName: acme.example.com  # Must match values.yaml groupName
            solverName: mydns
            config:
              dbSecretRef:
                name: mydns-db-credentials
                namespace: cert-manager
              ttl: 60
```

### 4. Request a Certificate

```yaml
apiVersion: cert-manager.io/v1
kind: Certificate
metadata:
  name: example-com-tls
  namespace: default
spec:
  secretName: example-com-tls
  issuerRef:
    name: letsencrypt-prod
    kind: ClusterIssuer
  dnsNames:
    - example.com
    - "*.example.com"
```

## Configuration

### Webhook Configuration (via Issuer/ClusterIssuer)

| Parameter | Description | Required | Default |
|-----------|-------------|----------|---------|
| `dbSecretRef.name` | Name of the secret containing DB credentials | Yes | - |
| `dbSecretRef.namespace` | Namespace of the secret | No | Same as resource |
| `ttl` | TTL for TXT records (seconds) | No | 60 |

### Helm Values

| Parameter | Description | Default |
|-----------|-------------|---------|
| `groupName` | API group for the webhook | `acme.example.com` |
| `image.repository` | Webhook image repository | `ghcr.io/mydns-ng/mydns-acme-webhook` |
| `image.tag` | Webhook image tag | `latest` |
| `replicaCount` | Number of replicas | `1` |
| `resources` | Resource requests/limits | See `values.yaml` |

### Database Secret Format

The database secret must contain these keys:

| Key | Description | Required |
|-----|-------------|----------|
| `host` | Database hostname | Yes |
| `port` | Database port | No (default: 3306) |
| `user` | Database username | Yes |
| `password` | Database password | Yes |
| `database` | Database name | Yes |

## Database Requirements

The webhook requires a MySQL/MariaDB database user with the following permissions:

```sql
GRANT SELECT ON mydns.soa TO 'mydns_webhook'@'%';
GRANT SELECT, INSERT, DELETE ON mydns.rr TO 'mydns_webhook'@'%';
GRANT UPDATE (serial) ON mydns.soa TO 'mydns_webhook'@'%';
```

## How It Works

1. When cert-manager requests a DNS-01 challenge, it calls the webhook's `Present` endpoint
2. The webhook:
   - Parses the configuration from the Issuer
   - Reads database credentials from the referenced Secret
   - Queries the `soa` table to find the zone ID for the domain
   - Inserts a TXT record into the `rr` table with the challenge token
   - Increments the SOA serial number
3. After ACME verification, cert-manager calls the `CleanUp` endpoint
4. The webhook deletes the TXT record and increments the SOA serial

## Troubleshooting

### Check webhook logs

```bash
kubectl logs -n cert-manager -l app=mydns-webhook
```

### Verify the webhook is registered

```bash
kubectl get apiservice v1alpha1.acme.example.com
```

### Test DNS propagation

```bash
dig TXT _acme-challenge.example.com @your-dns-server
```

### Common Issues

**"zone not found"**: Ensure the zone exists in the `soa` table with `active = 'Y'` and the `origin` includes the trailing dot (e.g., `example.com.`)

**"failed to get secret"**: Check that the secret exists and the webhook has RBAC permissions to read it

**Database connection issues**: Verify network connectivity from the cluster to the database

## Development

### Build

```bash
make build
```

### Run tests

```bash
make test
```

### Build Docker image

```bash
make docker-build
```

### Lint code

```bash
# Install golangci-lint
go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest

# Run linter
golangci-lint run
```

## CI/CD

This project uses Forgejo Actions for continuous integration and deployment.

### Workflows

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `ci.yaml` | Push/PR to main | Lint, test, build validation |
| `release.yaml` | Tag push (v*) | Build & push container + Helm chart |
| `scheduled.yaml` | Weekly / Manual | Security scans, dependency checks |

### CI Pipeline (Pull Requests)

On every pull request:
1. **Lint** - Go vet, fmt check, golangci-lint
2. **Test** - Unit tests with coverage
3. **Build** - Multi-arch binary compilation
4. **Docker Build** - Container image build test
5. **Helm Lint** - Chart validation
6. **Security Scan** - govulncheck, gosec

### Release Pipeline (Tags)

When you push a tag like `v1.0.0`:
1. Validates tests pass
2. Builds multi-arch container images (amd64, arm64)
3. Pushes to container registry with tags:
   - `v1.0.0` (exact version)
   - `v1.0` (minor version)
   - `v1` (major version)
   - `latest` (if not prerelease)
4. Packages Helm chart with matching version
5. Pushes Helm chart to OCI registry
6. Creates GitHub/Forgejo release with:
   - Binary downloads for multiple platforms
   - Checksums
   - Helm chart archive
   - SBOM (Software Bill of Materials)

### Creating a Release

```bash
# Tag a release
git tag v1.0.0
git push origin v1.0.0

# Tag a prerelease
git tag v1.1.0-rc1
git push origin v1.1.0-rc1
```

### Installing from OCI Registry

After release, install directly from OCI (update registry URL to match your deployment):

```bash
# Install Helm chart from OCI registry
helm install mydns-webhook oci://YOUR_REGISTRY/mydns-ng/helm-charts/mydns-webhook \
  --version 1.0.0 \
  --namespace cert-manager

# Pull container image
docker pull YOUR_REGISTRY/mydns-ng/mydns-acme-webhook:1.0.0
```

### Required Secrets

Configure these secrets in your Forgejo repository:

| Secret | Description |
|--------|-------------|
| `REGISTRY_USERNAME` | Container registry username (optional, defaults to actor) |
| `REGISTRY_PASSWORD` | Container registry password/token |

### Repository Variables

Configure these variables for customization:

| Variable | Default | Description |
|----------|---------|-------------|
| `REGISTRY_HOST` | (required) | Container/OCI registry hostname |
| `IMAGE_NAME` | `mydns-ng/mydns-acme-webhook` | Container image path |
| `HELM_REPO` | `mydns-ng/helm-charts` | Helm OCI repository path |

## License

Apache 2.0
