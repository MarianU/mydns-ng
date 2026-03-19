package quorum

import (
	"testing"
	"time"

	"github.com/mydns-ng/mydns-health-monitor/pkg/observer"
)

func healthyObs() observer.ZoneObservation {
	return observer.ZoneObservation{
		Healthy:   true,
		LastCheck: time.Now(),
	}
}

func unhealthyObs() observer.ZoneObservation {
	return observer.ZoneObservation{
		Healthy:   false,
		LastCheck: time.Now(),
	}
}

func peerReport(zone string, obs map[string]observer.ZoneObservation) observer.ObservationReport {
	return observer.ObservationReport{
		Monitor: observer.MonitorIdentity{
			Zone:    zone,
			PodName: "pod-" + zone,
		},
		Observations: obs,
	}
}

func TestCalculateConsensus_ThreeZonesOneDown(t *testing.T) {
	// Monitors A and B both report C unhealthy → consensus UNHEALTHY for C.
	engine := NewEngine()

	myObs := map[string]observer.ZoneObservation{
		"zone-b": healthyObs(),
		"zone-c": unhealthyObs(),
	}

	peers := []observer.ObservationReport{
		peerReport("zone-b", map[string]observer.ZoneObservation{
			"zone-a": healthyObs(),
			"zone-c": unhealthyObs(),
		}),
		peerReport("zone-c", map[string]observer.ZoneObservation{
			"zone-a": healthyObs(),
			"zone-b": healthyObs(),
		}),
	}

	results := engine.CalculateConsensus("zone-a", myObs, peers, false)

	// zone-c: A says unhealthy, B says unhealthy. C is excluded (self-exclusion).
	// 2 voters, 2 unhealthy → decided UNHEALTHY.
	rc, ok := results["zone-c"]
	if !ok {
		t.Fatal("expected result for zone-c")
	}
	if !rc.Decided {
		t.Error("expected zone-c to be decided")
	}
	if rc.Healthy {
		t.Error("expected zone-c to be unhealthy")
	}
	if len(rc.UnhealthyVoters) != 2 {
		t.Errorf("expected 2 unhealthy voters for zone-c, got %d", len(rc.UnhealthyVoters))
	}

	// zone-a: B says healthy, C says healthy. A is excluded.
	// 2 voters, 2 healthy → decided HEALTHY.
	ra, ok := results["zone-a"]
	if !ok {
		t.Fatal("expected result for zone-a")
	}
	if !ra.Decided {
		t.Error("expected zone-a to be decided")
	}
	if !ra.Healthy {
		t.Error("expected zone-a to be healthy")
	}

	// zone-b: A says healthy, C says healthy. B is excluded.
	// 2 voters, 2 healthy → decided HEALTHY.
	rb, ok := results["zone-b"]
	if !ok {
		t.Fatal("expected result for zone-b")
	}
	if !rb.Decided {
		t.Error("expected zone-b to be decided")
	}
	if !rb.Healthy {
		t.Error("expected zone-b to be healthy")
	}
}

func TestCalculateConsensus_Isolation(t *testing.T) {
	// isolated=true → empty results.
	engine := NewEngine()

	myObs := map[string]observer.ZoneObservation{
		"zone-b": unhealthyObs(),
	}

	results := engine.CalculateConsensus("zone-a", myObs, nil, true)

	if len(results) != 0 {
		t.Errorf("expected empty results when isolated, got %d entries", len(results))
	}
}

func TestCalculateConsensus_SelfExclusion(t *testing.T) {
	// Monitor A doesn't vote on zone-a's health.
	engine := NewEngine()

	// Monitor A observes itself as unhealthy (shouldn't be counted).
	myObs := map[string]observer.ZoneObservation{
		"zone-a": unhealthyObs(),
		"zone-b": healthyObs(),
	}

	// Monitor B observes zone-a as healthy.
	peers := []observer.ObservationReport{
		peerReport("zone-b", map[string]observer.ZoneObservation{
			"zone-a": healthyObs(),
		}),
	}

	results := engine.CalculateConsensus("zone-a", myObs, peers, false)

	// zone-a: A excluded (self), B votes healthy. 1 voter, 1 healthy → decided HEALTHY.
	ra := results["zone-a"]
	if !ra.Decided {
		t.Error("expected zone-a to be decided")
	}
	if !ra.Healthy {
		t.Error("expected zone-a to be healthy (self-exclusion should ignore A's unhealthy vote)")
	}
	if ra.TotalVoters != 1 {
		t.Errorf("expected 1 total voter for zone-a, got %d", ra.TotalVoters)
	}
}

func TestCalculateConsensus_TiedVote(t *testing.T) {
	// 4 zones: for zone-d, 2 say unhealthy, 2 say healthy → UNDECIDED.
	engine := NewEngine()

	// Monitor A (zone-a): zone-d is unhealthy.
	myObs := map[string]observer.ZoneObservation{
		"zone-d": unhealthyObs(),
	}

	peers := []observer.ObservationReport{
		// Monitor B (zone-b): zone-d is unhealthy.
		peerReport("zone-b", map[string]observer.ZoneObservation{
			"zone-d": unhealthyObs(),
		}),
		// Monitor C (zone-c): zone-d is healthy.
		peerReport("zone-c", map[string]observer.ZoneObservation{
			"zone-d": healthyObs(),
		}),
		// Monitor D (zone-d): excluded (self-exclusion).
		// Monitor E (zone-e): zone-d is healthy.
		peerReport("zone-e", map[string]observer.ZoneObservation{
			"zone-d": healthyObs(),
		}),
	}

	results := engine.CalculateConsensus("zone-a", myObs, peers, false)

	// zone-d: A=unhealthy, B=unhealthy, C=healthy, E=healthy. 4 voters, 2 vs 2.
	// 2 > 4/2=2 is false for both sides → UNDECIDED.
	rd := results["zone-d"]
	if rd.Decided {
		t.Error("expected zone-d to be undecided (tied vote)")
	}
	if rd.TotalVoters != 4 {
		t.Errorf("expected 4 total voters for zone-d, got %d", rd.TotalVoters)
	}
}

func TestCalculateConsensus_SingleMonitor(t *testing.T) {
	// Only one monitor, no peers → can still reach consensus for zones it observes.
	engine := NewEngine()

	myObs := map[string]observer.ZoneObservation{
		"zone-b": healthyObs(),
		"zone-c": unhealthyObs(),
	}

	results := engine.CalculateConsensus("zone-a", myObs, nil, false)

	// zone-b: A votes healthy. 1 voter. 1 > 0 → decided HEALTHY.
	rb := results["zone-b"]
	if !rb.Decided {
		t.Error("expected zone-b to be decided")
	}
	if !rb.Healthy {
		t.Error("expected zone-b to be healthy")
	}
	if rb.TotalVoters != 1 {
		t.Errorf("expected 1 voter for zone-b, got %d", rb.TotalVoters)
	}

	// zone-c: A votes unhealthy. 1 voter. 1 > 0 → decided UNHEALTHY.
	rc := results["zone-c"]
	if !rc.Decided {
		t.Error("expected zone-c to be decided")
	}
	if rc.Healthy {
		t.Error("expected zone-c to be unhealthy")
	}
	if rc.TotalVoters != 1 {
		t.Errorf("expected 1 voter for zone-c, got %d", rc.TotalVoters)
	}
}

func TestCalculateConsensus_TwoMonitorsTwoZonesBothHealthy(t *testing.T) {
	// 2 monitors, 2 zones: each monitors the other, both report the other healthy → HEALTHY.
	engine := NewEngine()

	myObs := map[string]observer.ZoneObservation{
		"zone-b": healthyObs(),
	}

	peers := []observer.ObservationReport{
		peerReport("zone-b", map[string]observer.ZoneObservation{
			"zone-a": healthyObs(),
		}),
	}

	results := engine.CalculateConsensus("zone-a", myObs, peers, false)

	// zone-a: B votes healthy. A excluded. 1 voter → decided HEALTHY.
	ra := results["zone-a"]
	if !ra.Decided {
		t.Error("expected zone-a to be decided")
	}
	if !ra.Healthy {
		t.Error("expected zone-a to be healthy")
	}

	// zone-b: A votes healthy. B excluded. 1 voter → decided HEALTHY.
	rb := results["zone-b"]
	if !rb.Decided {
		t.Error("expected zone-b to be decided")
	}
	if !rb.Healthy {
		t.Error("expected zone-b to be healthy")
	}
}

func TestCalculateConsensus_ZoneNotObserved(t *testing.T) {
	// A zone that exists in observations but no eligible voter has an observation for it.
	// Only monitor in zone-a observes zone-a (but is excluded by self-exclusion).
	engine := NewEngine()

	myObs := map[string]observer.ZoneObservation{
		"zone-a": healthyObs(),
	}

	results := engine.CalculateConsensus("zone-a", myObs, nil, false)

	// zone-a: A is excluded (self-exclusion). 0 voters → UNDECIDED.
	ra := results["zone-a"]
	if ra.Decided {
		t.Error("expected zone-a to be undecided (no eligible voters)")
	}
	if ra.TotalVoters != 0 {
		t.Errorf("expected 0 voters for zone-a, got %d", ra.TotalVoters)
	}
}

func TestCalculateConsensus_FiveZonesThreeUnhealthy(t *testing.T) {
	// 5 zones (A-E). For zone-e: monitors A, B, C report unhealthy, D reports healthy.
	// E is excluded (self). 4 voters, 3 unhealthy = 75% → UNHEALTHY.
	engine := NewEngine()

	// Monitor A (zone-a): zone-e unhealthy.
	myObs := map[string]observer.ZoneObservation{
		"zone-e": unhealthyObs(),
	}

	peers := []observer.ObservationReport{
		// Monitor B (zone-b): zone-e unhealthy.
		peerReport("zone-b", map[string]observer.ZoneObservation{
			"zone-e": unhealthyObs(),
		}),
		// Monitor C (zone-c): zone-e unhealthy.
		peerReport("zone-c", map[string]observer.ZoneObservation{
			"zone-e": unhealthyObs(),
		}),
		// Monitor D (zone-d): zone-e healthy.
		peerReport("zone-d", map[string]observer.ZoneObservation{
			"zone-e": healthyObs(),
		}),
		// Monitor E (zone-e): zone-a healthy (excluded for zone-e vote).
		peerReport("zone-e", map[string]observer.ZoneObservation{
			"zone-a": healthyObs(),
		}),
	}

	results := engine.CalculateConsensus("zone-a", myObs, peers, false)

	re := results["zone-e"]
	if !re.Decided {
		t.Error("expected zone-e to be decided")
	}
	if re.Healthy {
		t.Error("expected zone-e to be unhealthy")
	}
	if re.TotalVoters != 4 {
		t.Errorf("expected 4 voters for zone-e, got %d", re.TotalVoters)
	}
	if len(re.UnhealthyVoters) != 3 {
		t.Errorf("expected 3 unhealthy voters for zone-e, got %d", len(re.UnhealthyVoters))
	}
	if len(re.HealthyVoters) != 1 {
		t.Errorf("expected 1 healthy voter for zone-e, got %d", len(re.HealthyVoters))
	}
}
