// Package monitor provides the main orchestration loop for the zone health monitor.
package monitor

import (
	"fmt"
	"os"
	"strconv"
	"time"
)

// Config holds all monitor configuration.
type Config struct {
	// Identity
	MyZone  string
	PodName string
	PodIP   string

	// Peer discovery
	PeerServiceFQDN   string
	DiscoveryInterval time.Duration

	// Health checking
	CheckInterval    time.Duration
	CheckTimeout     time.Duration
	FailureThreshold int
	SuccessThreshold int

	// Peer communication
	HTTPPort    int
	PeerTimeout time.Duration

	// Database
	DBHost     string
	DBPort     string
	DBUser     string
	DBPassword string
	DBName     string

	// Feature flags
	DryRun bool

	// Version (set at build time)
	Version string
}

// LoadConfigFromEnv loads configuration from environment variables.
func LoadConfigFromEnv() (*Config, error) {
	cfg := &Config{
		MyZone:          os.Getenv("MY_ZONE"),
		PodName:         os.Getenv("MY_POD_NAME"),
		PodIP:           os.Getenv("MY_POD_IP"),
		PeerServiceFQDN: os.Getenv("PEER_SERVICE_FQDN"),
		DBHost:          os.Getenv("DB_HOST"),
		DBPort:          os.Getenv("DB_PORT"),
		DBUser:          os.Getenv("DB_USER"),
		DBPassword:      os.Getenv("DB_PASSWORD"),
		DBName:          os.Getenv("DB_NAME"),
		Version:         "0.1.0",
	}

	// Parse optional values with defaults.
	cfg.HTTPPort = getEnvInt("HTTP_PORT", 8080)
	cfg.CheckInterval = getEnvDuration("CHECK_INTERVAL", 30*time.Second)
	cfg.DiscoveryInterval = getEnvDuration("DISCOVERY_INTERVAL", 15*time.Second)
	cfg.CheckTimeout = getEnvDuration("CHECK_TIMEOUT", 10*time.Second)
	cfg.PeerTimeout = getEnvDuration("PEER_TIMEOUT", 5*time.Second)
	cfg.FailureThreshold = getEnvInt("FAILURE_THRESHOLD", 3)
	cfg.SuccessThreshold = getEnvInt("SUCCESS_THRESHOLD", 2)
	cfg.DryRun = os.Getenv("DRY_RUN") == "true"

	if cfg.DBPort == "" {
		cfg.DBPort = "3306"
	}

	// Validate required fields.
	required := map[string]string{
		"MY_ZONE":           cfg.MyZone,
		"MY_POD_NAME":       cfg.PodName,
		"MY_POD_IP":         cfg.PodIP,
		"PEER_SERVICE_FQDN": cfg.PeerServiceFQDN,
		"DB_HOST":           cfg.DBHost,
		"DB_USER":           cfg.DBUser,
		"DB_PASSWORD":       cfg.DBPassword,
		"DB_NAME":           cfg.DBName,
	}

	for name, value := range required {
		if value == "" {
			return nil, fmt.Errorf("%s environment variable must be set", name)
		}
	}

	return cfg, nil
}

// getEnvInt reads an integer from the environment variable, returning defaultVal if unset or invalid.
func getEnvInt(key string, defaultVal int) int {
	v := os.Getenv(key)
	if v == "" {
		return defaultVal
	}
	i, err := strconv.Atoi(v)
	if err != nil {
		return defaultVal
	}
	return i
}

// getEnvDuration reads a duration from the environment variable, returning defaultVal if unset or invalid.
func getEnvDuration(key string, defaultVal time.Duration) time.Duration {
	v := os.Getenv(key)
	if v == "" {
		return defaultVal
	}
	d, err := time.ParseDuration(v)
	if err != nil {
		return defaultVal
	}
	return d
}
