package monitor

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/klog/v2"

	"github.com/mydns-ng/mydns-health-monitor/pkg/discovery"
	"github.com/mydns-ng/mydns-health-monitor/pkg/failover"
	"github.com/mydns-ng/mydns-health-monitor/pkg/health"
	"github.com/mydns-ng/mydns-health-monitor/pkg/mydns"
	"github.com/mydns-ng/mydns-health-monitor/pkg/observer"
	"github.com/mydns-ng/mydns-health-monitor/pkg/quorum"
)

// Monitor is the main orchestrator for zone health monitoring.
type Monitor struct {
	cfg         *Config
	kubeClient  kubernetes.Interface
	dbClient    *mydns.DBClient
	checker     *health.Checker
	disc        *discovery.Discovery
	obsServer   *observer.Server
	obsClient   *observer.Client
	quorumEng   *quorum.Engine
	failoverMgr *failover.Manager
}

// New creates a new Monitor with all components initialized.
func New(cfg *Config) (*Monitor, error) {
	// 1. Create in-cluster Kubernetes client.
	kubeConfig, err := rest.InClusterConfig()
	if err != nil {
		return nil, fmt.Errorf("failed to create in-cluster config: %w", err)
	}
	kubeClient, err := kubernetes.NewForConfig(kubeConfig)
	if err != nil {
		return nil, fmt.Errorf("failed to create kubernetes client: %w", err)
	}

	// 2. Create database client.
	dbCfg := &mydns.DBConfig{
		Host:     cfg.DBHost,
		Port:     cfg.DBPort,
		User:     cfg.DBUser,
		Password: cfg.DBPassword,
		Database: cfg.DBName,
	}
	dbClient, err := mydns.NewDBClient(dbCfg)
	if err != nil {
		return nil, fmt.Errorf("failed to create database client: %w", err)
	}

	// 3. Create components.
	identity := observer.MonitorIdentity{
		Zone:      cfg.MyZone,
		PodName:   cfg.PodName,
		PodIP:     cfg.PodIP,
		StartedAt: time.Now(),
		Version:   cfg.Version,
	}

	checker := health.NewChecker(kubeClient, cfg.MyZone, cfg.FailureThreshold, cfg.SuccessThreshold)
	disc := discovery.NewDiscovery(cfg.PeerServiceFQDN, cfg.HTTPPort, cfg.PodIP, cfg.PeerTimeout)
	obsClient := observer.NewClient(cfg.HTTPPort, cfg.PeerTimeout)

	m := &Monitor{
		cfg:         cfg,
		kubeClient:  kubeClient,
		dbClient:    dbClient,
		checker:     checker,
		disc:        disc,
		obsClient:   obsClient,
		quorumEng:   quorum.NewEngine(),
		failoverMgr: failover.NewManager(dbClient, cfg.DryRun),
	}

	// Create observer server with health check callbacks.
	m.obsServer = observer.NewServer(&identity,
		func() bool { return dbClient.Ping() },
		func() bool { return disc.HasReachablePeers() },
	)

	return m, nil
}

// Run starts the monitor and blocks until the context is canceled.
func (m *Monitor) Run(ctx context.Context) error {
	// 1. Start HTTP server in goroutine.
	httpServer := &http.Server{
		Addr:              fmt.Sprintf(":%d", m.cfg.HTTPPort),
		Handler:           m.obsServer.Handler(),
		ReadHeaderTimeout: 10 * time.Second,
	}
	go func() {
		klog.Infof("Starting HTTP server on :%d", m.cfg.HTTPPort)
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			klog.Errorf("HTTP server error: %v", err)
		}
	}()

	// 2. Start discovery loop in goroutine.
	go m.discoveryLoop(ctx)

	// 3. Run main check loop.
	klog.Infof("Starting main check loop (interval=%s)", m.cfg.CheckInterval)
	ticker := time.NewTicker(m.cfg.CheckInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			klog.Info("Shutting down monitor")
			shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			err := httpServer.Shutdown(shutdownCtx)
			cancel()
			if err != nil {
				klog.Errorf("HTTP server shutdown error: %v", err)
			}
			_ = m.dbClient.Close()
			return nil
		case <-ticker.C:
			if err := m.checkCycle(ctx); err != nil {
				klog.Errorf("Check cycle error: %v", err)
			}
		}
	}
}

// discoveryLoop runs peer discovery at the configured interval.
func (m *Monitor) discoveryLoop(ctx context.Context) {
	ticker := time.NewTicker(m.cfg.DiscoveryInterval)
	defer ticker.Stop()

	// Run once immediately.
	if err := m.disc.Discover(ctx); err != nil {
		klog.Errorf("Initial discovery failed: %v", err)
	}

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if err := m.disc.Discover(ctx); err != nil {
				klog.V(3).Infof("Discovery error: %v", err)
			}
			m.disc.RemoveStalePeers(m.cfg.DiscoveryInterval * 3)
		}
	}
}

// checkCycle performs one complete check cycle: check → share → consensus → act.
func (m *Monitor) checkCycle(ctx context.Context) error {
	// 1. Get all known zones.
	allZones := m.disc.AllKnownZones(m.cfg.MyZone)
	klog.V(4).Infof("Known zones: %v", allZones)

	// 2. Check health of all zones (except own).
	healthResults, err := m.checker.CheckAllZones(ctx, allZones)
	if err != nil {
		klog.V(3).Infof("Some health checks failed: %v", err)
	}

	// 3. Convert health results to observations and update server.
	observations := make(map[string]observer.ZoneObservation)
	for zone, zh := range healthResults {
		observations[zone] = observer.ZoneObservation{
			Healthy:              zh.Healthy,
			ReadyNodes:           zh.ReadyNodes,
			TotalNodes:           zh.TotalNodes,
			ConsecutiveFailures:  zh.ConsecutiveFailures,
			ConsecutiveSuccesses: zh.ConsecutiveSuccesses,
			LastCheck:            zh.LastCheck,
		}
	}
	m.obsServer.UpdateObservations(observations)

	// 4. Fetch observations from peers.
	peers := m.disc.GetPeers()
	peerIPs := make([]string, 0, len(peers))
	for ip := range peers {
		peerIPs = append(peerIPs, ip)
	}

	peerReports, failures := m.obsClient.FetchAllObservations(ctx, peerIPs)
	isolated := len(peerIPs) > 0 && failures == len(peerIPs)

	if isolated {
		klog.V(2).Info("Monitor is isolated (cannot reach any peer), taking no action")
		return nil
	}

	// 5. Calculate quorum consensus.
	results := m.quorumEng.CalculateConsensus(m.cfg.MyZone, observations, peerReports, isolated)

	// 6. Process consensus results (failover actions).
	if err := m.failoverMgr.ProcessConsensus(ctx, results, m.cfg.PodName, m.cfg.MyZone); err != nil {
		return fmt.Errorf("failed to process consensus: %w", err)
	}

	return nil
}

// Close cleans up monitor resources.
func (m *Monitor) Close() error {
	if m.dbClient != nil {
		return m.dbClient.Close()
	}
	return nil
}
