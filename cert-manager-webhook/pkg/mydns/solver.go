// Package mydns implements a cert-manager DNS-01 webhook solver for MyDNS-NG.
package mydns

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync"
	"time"

	extapi "k8s.io/apiextensions-apiserver/pkg/apis/apiextensions/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/klog/v2"

	acme "github.com/cert-manager/cert-manager/pkg/acme/webhook/apis/acme/v1alpha1"
)

const (
	// DefaultTTL is the default TTL for TXT records.
	DefaultTTL = 60

	// SolverName is the name of this solver.
	SolverName = "mydns"
)

// SecretRef references a Kubernetes secret.
type SecretRef struct {
	// Name is the name of the secret.
	Name string `json:"name"`

	// Namespace is the namespace of the secret.
	// If empty, the challenge's namespace is used.
	Namespace string `json:"namespace,omitempty"`
}

// ProviderConfig is the configuration for the mydns solver.
// This is parsed from the ChallengeRequest.Config field.
type ProviderConfig struct {
	// DBSecretRef references the secret containing database credentials.
	DBSecretRef SecretRef `json:"dbSecretRef"`

	// TTL is the TTL for TXT records. Defaults to 60.
	TTL int `json:"ttl,omitempty"`
}

// Solver implements the webhook.Solver interface for MyDNS-NG.
type Solver struct {
	kubeClient kubernetes.Interface
	stopCh     <-chan struct{}

	// clientCache caches database clients by secret key.
	clientCache   map[string]*DBClient
	clientCacheMu sync.RWMutex
}

// NewSolver creates a new MyDNS solver.
func NewSolver() *Solver {
	return &Solver{
		clientCache: make(map[string]*DBClient),
	}
}

// Name returns the name of the solver.
func (s *Solver) Name() string {
	return SolverName
}

// Present creates a TXT record in the MyDNS database.
func (s *Solver) Present(ch *acme.ChallengeRequest) error {
	klog.V(2).Infof("Present: fqdn=%q zone=%q", ch.ResolvedFQDN, ch.ResolvedZone)

	cfg, err := loadConfig(ch.Config)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	client, err := s.getOrCreateClient(ctx, cfg.DBSecretRef, ch.ResourceNamespace)
	if err != nil {
		return fmt.Errorf("failed to get database client: %w", err)
	}

	// Get zone ID from the resolved zone
	zoneID, err := client.GetZone(ctx, ch.ResolvedZone)
	if err != nil {
		return fmt.Errorf("failed to get zone for %q: %w", ch.ResolvedZone, err)
	}

	// Extract record name (e.g., "_acme-challenge.www" from "_acme-challenge.www.example.com.")
	recordName := extractRecordName(ch.ResolvedFQDN, ch.ResolvedZone)

	ttl := cfg.TTL
	if ttl == 0 {
		ttl = DefaultTTL
	}

	// Create the TXT record
	if err := client.CreateTXTRecord(ctx, zoneID, recordName, ch.Key, ttl); err != nil {
		return fmt.Errorf("failed to create TXT record: %w", err)
	}

	klog.Infof("Successfully created TXT record: name=%q zone=%q", recordName, ch.ResolvedZone)
	return nil
}

// CleanUp removes a TXT record from the MyDNS database.
func (s *Solver) CleanUp(ch *acme.ChallengeRequest) error {
	klog.V(2).Infof("CleanUp: fqdn=%q zone=%q", ch.ResolvedFQDN, ch.ResolvedZone)

	cfg, err := loadConfig(ch.Config)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	client, err := s.getOrCreateClient(ctx, cfg.DBSecretRef, ch.ResourceNamespace)
	if err != nil {
		return fmt.Errorf("failed to get database client: %w", err)
	}

	// Get zone ID from the resolved zone
	zoneID, err := client.GetZone(ctx, ch.ResolvedZone)
	if err != nil {
		return fmt.Errorf("failed to get zone for %q: %w", ch.ResolvedZone, err)
	}

	// Extract record name
	recordName := extractRecordName(ch.ResolvedFQDN, ch.ResolvedZone)

	// Delete the TXT record
	if err := client.DeleteTXTRecord(ctx, zoneID, recordName, ch.Key); err != nil {
		return fmt.Errorf("failed to delete TXT record: %w", err)
	}

	klog.Infof("Successfully deleted TXT record: name=%q zone=%q", recordName, ch.ResolvedZone)
	return nil
}

// Initialize sets up the Kubernetes client for fetching secrets.
func (s *Solver) Initialize(kubeClientConfig *rest.Config, stopCh <-chan struct{}) error {
	klog.Info("Initializing MyDNS solver")

	client, err := kubernetes.NewForConfig(kubeClientConfig)
	if err != nil {
		return fmt.Errorf("failed to create kubernetes client: %w", err)
	}

	s.kubeClient = client
	s.stopCh = stopCh

	// Handle graceful shutdown
	go func() {
		<-stopCh
		klog.Info("Shutting down MyDNS solver")
		s.closeAllClients()
	}()

	return nil
}

// loadConfig parses the ProviderConfig from the ChallengeRequest.
func loadConfig(cfgJSON *extapi.JSON) (*ProviderConfig, error) {
	cfg := &ProviderConfig{}
	if cfgJSON == nil {
		return nil, fmt.Errorf("no solver config provided")
	}
	if err := json.Unmarshal(cfgJSON.Raw, cfg); err != nil {
		return nil, fmt.Errorf("failed to parse solver config: %w", err)
	}
	if cfg.DBSecretRef.Name == "" {
		return nil, fmt.Errorf("dbSecretRef.name is required")
	}
	return cfg, nil
}

// extractRecordName extracts the relative record name from the FQDN.
// For example: "_acme-challenge.www.example.com." with zone "example.com." returns "_acme-challenge.www"
func extractRecordName(fqdn, zone string) string {
	// Both should have trailing dots, but handle cases where they don't
	fqdn = strings.TrimSuffix(fqdn, ".")
	zone = strings.TrimSuffix(zone, ".")

	// Remove the zone suffix from the FQDN
	name := strings.TrimSuffix(fqdn, "."+zone)

	// If the FQDN equals the zone, return empty (apex record)
	if name == fqdn {
		return ""
	}

	return name
}

// getOrCreateClient gets or creates a database client for the given secret reference.
func (s *Solver) getOrCreateClient(ctx context.Context, secretRef SecretRef, defaultNamespace string) (*DBClient, error) {
	namespace := secretRef.Namespace
	if namespace == "" {
		namespace = defaultNamespace
	}

	cacheKey := fmt.Sprintf("%s/%s", namespace, secretRef.Name)

	// Check cache first
	s.clientCacheMu.RLock()
	if client, ok := s.clientCache[cacheKey]; ok {
		s.clientCacheMu.RUnlock()
		return client, nil
	}
	s.clientCacheMu.RUnlock()

	// Create new client
	s.clientCacheMu.Lock()
	defer s.clientCacheMu.Unlock()

	// Double-check after acquiring write lock
	if client, ok := s.clientCache[cacheKey]; ok {
		return client, nil
	}

	// Fetch secret
	dbConfig, err := s.getDBConfigFromSecret(ctx, namespace, secretRef.Name)
	if err != nil {
		return nil, err
	}

	// Create database client
	client, err := NewDBClient(dbConfig)
	if err != nil {
		return nil, fmt.Errorf("failed to create database client: %w", err)
	}

	s.clientCache[cacheKey] = client
	klog.V(2).Infof("Created new database client for secret %s", cacheKey)

	return client, nil
}

// getDBConfigFromSecret reads database configuration from a Kubernetes secret.
func (s *Solver) getDBConfigFromSecret(ctx context.Context, namespace, name string) (*DBConfig, error) {
	secret, err := s.kubeClient.CoreV1().Secrets(namespace).Get(ctx, name, metav1.GetOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to get secret %s/%s: %w", namespace, name, err)
	}

	cfg := &DBConfig{}

	// Helper to get string value from secret
	getString := func(key string) string {
		if v, ok := secret.Data[key]; ok {
			return string(v)
		}
		return ""
	}

	cfg.Host = getString("host")
	cfg.Port = getString("port")
	cfg.User = getString("user")
	cfg.Password = getString("password")
	cfg.Database = getString("database")

	// Validate required fields
	if cfg.Host == "" {
		return nil, fmt.Errorf("secret %s/%s missing 'host' field", namespace, name)
	}
	if cfg.User == "" {
		return nil, fmt.Errorf("secret %s/%s missing 'user' field", namespace, name)
	}
	if cfg.Password == "" {
		return nil, fmt.Errorf("secret %s/%s missing 'password' field", namespace, name)
	}
	if cfg.Database == "" {
		return nil, fmt.Errorf("secret %s/%s missing 'database' field", namespace, name)
	}

	// Default port
	if cfg.Port == "" {
		cfg.Port = "3306"
	}

	klog.V(4).Infof("Loaded database config from secret %s/%s: host=%s port=%s user=%s database=%s",
		namespace, name, cfg.Host, cfg.Port, cfg.User, cfg.Database)

	return cfg, nil
}

// closeAllClients closes all cached database clients.
func (s *Solver) closeAllClients() {
	s.clientCacheMu.Lock()
	defer s.clientCacheMu.Unlock()

	for key, client := range s.clientCache {
		klog.V(2).Infof("Closing database client for %s", key)
		if err := client.Close(); err != nil {
			klog.Errorf("Error closing database client for %s: %v", key, err)
		}
	}
	s.clientCache = make(map[string]*DBClient)
}

// Ensure Solver implements the required interface.
var _ interface {
	Name() string
	Present(*acme.ChallengeRequest) error
	CleanUp(*acme.ChallengeRequest) error
	Initialize(*rest.Config, <-chan struct{}) error
} = &Solver{}
