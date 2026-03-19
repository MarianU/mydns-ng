package observer

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"k8s.io/klog/v2"
)

// Client fetches observations from peer monitors.
type Client struct {
	httpClient *http.Client
	httpPort   int
}

// NewClient creates a new observer client.
func NewClient(httpPort int, timeout time.Duration) *Client {
	return &Client{
		httpClient: &http.Client{Timeout: timeout},
		httpPort:   httpPort,
	}
}

// FetchObservations fetches the observation report from a peer at the given IP.
func (c *Client) FetchObservations(ctx context.Context, peerIP string) (*ObservationReport, error) {
	url := fmt.Sprintf("http://%s:%d/api/v1/observations", peerIP, c.httpPort)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch observations from %s: %w", peerIP, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("peer %s returned status %d", peerIP, resp.StatusCode)
	}

	var report ObservationReport
	if err := json.NewDecoder(resp.Body).Decode(&report); err != nil {
		return nil, fmt.Errorf("failed to decode observations from %s: %w", peerIP, err)
	}

	klog.V(4).Infof("Fetched observations from peer %s (zone=%s): %d zones observed",
		peerIP, report.Monitor.Zone, len(report.Observations))
	return &report, nil
}

// FetchAllObservations fetches observations from all given peer IPs.
// Returns successfully fetched reports and the count of failures.
func (c *Client) FetchAllObservations(ctx context.Context, peerIPs []string) ([]ObservationReport, int) {
	var reports []ObservationReport
	failures := 0

	for _, ip := range peerIPs {
		report, err := c.FetchObservations(ctx, ip)
		if err != nil {
			klog.V(3).Infof("Failed to fetch observations from peer %s: %v", ip, err)
			failures++
			continue
		}
		reports = append(reports, *report)
	}

	return reports, failures
}
