// Package health provides Kubernetes node health checking for zone monitoring.
package health

import (
	"context"
	"fmt"
	"sync"
	"time"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/klog/v2"
)

// ZoneHealth represents the observed health of a single zone.
type ZoneHealth struct {
	Zone                 string
	Healthy              bool
	ReadyNodes           int
	TotalNodes           int
	ConsecutiveFailures  int
	ConsecutiveSuccesses int
	LastCheck            time.Time
	LastTransition       time.Time
}

// Checker performs health checks against Kubernetes zones by inspecting node Ready conditions.
type Checker struct {
	kubeClient       kubernetes.Interface
	myZone           string
	failureThreshold int
	successThreshold int

	// state tracks health per zone across check cycles.
	state map[string]*ZoneHealth
	mu    sync.RWMutex
}

// NewChecker creates a new health checker.
func NewChecker(kubeClient kubernetes.Interface, myZone string, failureThreshold, successThreshold int) *Checker {
	return &Checker{
		kubeClient:       kubeClient,
		myZone:           myZone,
		failureThreshold: failureThreshold,
		successThreshold: successThreshold,
		state:            make(map[string]*ZoneHealth),
	}
}

// CheckZone checks the health of a specific zone by listing nodes with topology.kubernetes.io/zone=<zone>.
// It updates internal state (consecutive failures/successes) and applies hysteresis.
// Returns the current ZoneHealth. A zone with 0 total nodes is treated as unhealthy.
// Checking the monitor's own zone returns nil with no error.
func (c *Checker) CheckZone(ctx context.Context, zone string) (*ZoneHealth, error) {
	if zone == c.myZone {
		klog.V(4).InfoS("Skipping self-zone check", "zone", zone)
		return nil, nil
	}

	selector := "topology.kubernetes.io/zone=" + zone
	nodes, err := c.kubeClient.CoreV1().Nodes().List(ctx, metav1.ListOptions{
		LabelSelector: selector,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to list nodes for zone %s: %w", zone, err)
	}

	totalNodes := len(nodes.Items)
	readyNodes := 0
	for i := range nodes.Items {
		if isNodeReady(&nodes.Items[i]) {
			readyNodes++
		}
	}

	rawHealthy := readyNodes > 0
	now := time.Now()

	c.mu.Lock()
	defer c.mu.Unlock()

	zh, exists := c.state[zone]
	if !exists {
		zh = &ZoneHealth{
			Zone:           zone,
			Healthy:        true,
			LastTransition: now,
		}
		c.state[zone] = zh
	}

	previousHealthy := zh.Healthy

	if rawHealthy {
		zh.ConsecutiveSuccesses++
		zh.ConsecutiveFailures = 0
	} else {
		zh.ConsecutiveFailures++
		zh.ConsecutiveSuccesses = 0
	}

	// Apply hysteresis: only change reported state when threshold is reached.
	if zh.ConsecutiveFailures >= c.failureThreshold {
		zh.Healthy = false
	} else if zh.ConsecutiveSuccesses >= c.successThreshold {
		zh.Healthy = true
	}
	// Otherwise keep previous state.

	zh.ReadyNodes = readyNodes
	zh.TotalNodes = totalNodes
	zh.LastCheck = now

	if zh.Healthy != previousHealthy {
		zh.LastTransition = now
	}

	klog.V(4).InfoS("Zone health check complete",
		"zone", zone,
		"readyNodes", readyNodes,
		"totalNodes", totalNodes,
		"healthy", zh.Healthy,
		"consecutiveFailures", zh.ConsecutiveFailures,
		"consecutiveSuccesses", zh.ConsecutiveSuccesses,
	)

	result := *zh
	return &result, nil
}

// CheckAllZones checks all zones except the monitor's own zone.
// The zones parameter is the list of all known zones (from discovery or config).
func (c *Checker) CheckAllZones(ctx context.Context, zones []string) (map[string]*ZoneHealth, error) {
	results := make(map[string]*ZoneHealth, len(zones))
	var firstErr error

	for _, zone := range zones {
		if zone == c.myZone {
			continue
		}

		zh, err := c.CheckZone(ctx, zone)
		if err != nil {
			klog.V(2).ErrorS(err, "Failed to check zone health", "zone", zone)
			if firstErr == nil {
				firstErr = err
			}
			continue
		}
		if zh != nil {
			results[zone] = zh
		}
	}

	return results, firstErr
}

// GetState returns a copy of the current health state for all zones.
func (c *Checker) GetState() map[string]*ZoneHealth {
	c.mu.RLock()
	defer c.mu.RUnlock()

	result := make(map[string]*ZoneHealth, len(c.state))
	for k, v := range c.state {
		cp := *v
		result[k] = &cp
	}
	return result
}

// isNodeReady checks if a node has the Ready condition set to True.
func isNodeReady(node *corev1.Node) bool {
	for _, condition := range node.Status.Conditions {
		if condition.Type == corev1.NodeReady {
			return condition.Status == corev1.ConditionTrue
		}
	}
	return false
}
