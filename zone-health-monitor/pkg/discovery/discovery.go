// Package discovery provides peer discovery via headless Service DNS lookup.
package discovery

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"sync"
	"time"

	"k8s.io/klog/v2"

	"github.com/mydns-ng/mydns-health-monitor/pkg/observer"
)

// Peer represents a discovered health monitor peer.
type Peer struct {
	LastSeen time.Time
	IP       string
	Zone     string
	Healthy  bool
}

// Discovery manages peer discovery via headless Service DNS.
type Discovery struct {
	httpClient  *http.Client
	peers       map[string]*Peer // key: IP
	serviceFQDN string
	myPodIP     string
	mu          sync.RWMutex
	httpPort    int
}

// NewDiscovery creates a new peer discovery manager.
func NewDiscovery(serviceFQDN string, httpPort int, myPodIP string, peerTimeout time.Duration) *Discovery {
	return &Discovery{
		serviceFQDN: serviceFQDN,
		httpPort:    httpPort,
		myPodIP:     myPodIP,
		httpClient: &http.Client{
			Timeout: peerTimeout,
		},
		peers: make(map[string]*Peer),
	}
}

// Discover performs DNS lookup on the headless Service and queries each peer's identity endpoint.
// It updates the internal peer registry, adding new peers and marking stale ones.
func (d *Discovery) Discover(ctx context.Context) error {
	ips, err := net.DefaultResolver.LookupHost(ctx, d.serviceFQDN)
	if err != nil {
		return fmt.Errorf("failed to resolve service FQDN %s: %w", d.serviceFQDN, err)
	}

	klog.V(3).InfoS("DNS lookup completed", "serviceFQDN", d.serviceFQDN, "ips", ips)

	seen := make(map[string]bool, len(ips))

	for _, ip := range ips {
		if ip == d.myPodIP {
			klog.V(4).InfoS("Skipping self in peer discovery", "ip", ip)
			continue
		}

		seen[ip] = true

		identity, fetchErr := d.fetchIdentity(ctx, ip)
		if fetchErr != nil {
			klog.V(2).ErrorS(fetchErr, "Failed to fetch peer identity", "ip", ip)
			d.mu.Lock()
			if p, ok := d.peers[ip]; ok {
				p.Healthy = false
			}
			d.mu.Unlock()
			continue
		}

		d.mu.Lock()
		d.peers[ip] = &Peer{
			IP:       ip,
			Zone:     identity.Zone,
			LastSeen: time.Now(),
			Healthy:  true,
		}
		d.mu.Unlock()
	}

	// Mark peers not seen in this round as unhealthy.
	d.mu.Lock()
	for ip, peer := range d.peers {
		if !seen[ip] {
			peer.Healthy = false
		}
	}
	d.mu.Unlock()

	klog.V(2).InfoS("Peer discovery complete", "peersDiscovered", len(seen), "totalPeers", d.peerCount())

	return nil
}

// RemoveStalePeers removes peers not seen for longer than maxAge.
func (d *Discovery) RemoveStalePeers(maxAge time.Duration) {
	d.mu.Lock()
	defer d.mu.Unlock()

	now := time.Now()
	for ip, peer := range d.peers {
		if now.Sub(peer.LastSeen) > maxAge {
			klog.V(3).InfoS("Removing stale peer", "ip", ip, "zone", peer.Zone, "lastSeen", peer.LastSeen)
			delete(d.peers, ip)
		}
	}
}

// GetPeers returns a copy of all currently known peers.
func (d *Discovery) GetPeers() map[string]*Peer {
	d.mu.RLock()
	defer d.mu.RUnlock()

	result := make(map[string]*Peer, len(d.peers))
	for k, v := range d.peers {
		cp := *v
		result[k] = &cp
	}
	return result
}

// GetPeerZones returns the list of unique zone names from discovered peers (excluding self).
func (d *Discovery) GetPeerZones() []string {
	d.mu.RLock()
	defer d.mu.RUnlock()

	zoneSet := make(map[string]struct{})
	for _, peer := range d.peers {
		if peer.Zone != "" {
			zoneSet[peer.Zone] = struct{}{}
		}
	}

	zones := make([]string, 0, len(zoneSet))
	for zone := range zoneSet {
		zones = append(zones, zone)
	}
	return zones
}

// AllKnownZones returns all known zones including the monitor's own zone.
func (d *Discovery) AllKnownZones(myZone string) []string {
	peerZones := d.GetPeerZones()

	zoneSet := make(map[string]struct{}, len(peerZones)+1)
	if myZone != "" {
		zoneSet[myZone] = struct{}{}
	}
	for _, z := range peerZones {
		zoneSet[z] = struct{}{}
	}

	zones := make([]string, 0, len(zoneSet))
	for zone := range zoneSet {
		zones = append(zones, zone)
	}
	return zones
}

// HasReachablePeers returns true if at least one peer was successfully reached.
func (d *Discovery) HasReachablePeers() bool {
	d.mu.RLock()
	defer d.mu.RUnlock()

	for _, peer := range d.peers {
		if peer.Healthy {
			return true
		}
	}
	return false
}

// fetchIdentity fetches the identity of a peer at the given IP.
func (d *Discovery) fetchIdentity(ctx context.Context, ip string) (*observer.MonitorIdentity, error) {
	url := fmt.Sprintf("http://%s:%d/api/v1/identity", ip, d.httpPort)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, http.NoBody)
	if err != nil {
		return nil, fmt.Errorf("failed to create identity request for %s: %w", ip, err)
	}

	resp, err := d.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch identity from %s: %w", ip, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("failed to fetch identity from %s: status %d", ip, resp.StatusCode)
	}

	var identity observer.MonitorIdentity
	if err := json.NewDecoder(resp.Body).Decode(&identity); err != nil {
		return nil, fmt.Errorf("failed to decode identity from %s: %w", ip, err)
	}

	return &identity, nil
}

// peerCount returns the number of known peers (for logging).
func (d *Discovery) peerCount() int {
	d.mu.RLock()
	defer d.mu.RUnlock()
	return len(d.peers)
}
