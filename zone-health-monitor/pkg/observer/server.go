package observer

import (
	"encoding/json"
	"net/http"
	"sync"

	"github.com/prometheus/client_golang/prometheus/promhttp"
	"k8s.io/klog/v2"
)

// Server exposes HTTP endpoints for health monitoring.
type Server struct {
	observations map[string]ZoneObservation
	dbHealthy    func() bool
	hasPeers     func() bool
	identity     MonitorIdentity
	mu           sync.RWMutex
}

// NewServer creates a new observation HTTP server.
func NewServer(identity *MonitorIdentity, dbHealthy, hasPeers func() bool) *Server {
	return &Server{
		identity:     *identity,
		observations: make(map[string]ZoneObservation),
		dbHealthy:    dbHealthy,
		hasPeers:     hasPeers,
	}
}

// UpdateObservations replaces the current observations with new ones.
func (s *Server) UpdateObservations(obs map[string]ZoneObservation) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.observations = obs
}

// Handler returns an http.Handler with all routes registered.
func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/identity", s.handleIdentity)
	mux.HandleFunc("/api/v1/observations", s.handleObservations)
	mux.HandleFunc("/healthz", s.handleHealthz)
	mux.HandleFunc("/readyz", s.handleReadyz)
	mux.Handle("/metrics", promhttp.Handler())
	return mux
}

// handleIdentity returns this monitor's identity.
func (s *Server) handleIdentity(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(s.identity); err != nil {
		klog.Errorf("Failed to encode identity response: %v", err)
	}
}

// handleObservations returns current observations.
func (s *Server) handleObservations(w http.ResponseWriter, r *http.Request) {
	s.mu.RLock()
	report := ObservationReport{
		Monitor:      s.identity,
		Observations: s.observations,
	}
	report.Monitor.StartedAt = s.identity.StartedAt // preserve original start time
	s.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(report); err != nil {
		klog.Errorf("Failed to encode observations response: %v", err)
	}
}

// handleHealthz is the liveness probe - always returns 200 if server is running.
func (s *Server) handleHealthz(w http.ResponseWriter, r *http.Request) {
	w.WriteHeader(http.StatusOK)
	if _, err := w.Write([]byte("ok")); err != nil {
		klog.V(4).Infof("Failed to write healthz response: %v", err)
	}
}

// handleReadyz is the readiness probe - returns 200 if DB is healthy.
func (s *Server) handleReadyz(w http.ResponseWriter, r *http.Request) {
	if s.dbHealthy != nil && !s.dbHealthy() {
		http.Error(w, "database not healthy", http.StatusServiceUnavailable)
		return
	}
	w.WriteHeader(http.StatusOK)
	if _, err := w.Write([]byte("ok")); err != nil {
		klog.V(4).Infof("Failed to write readyz response: %v", err)
	}
}
