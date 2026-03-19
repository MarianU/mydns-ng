// Package quorum implements the quorum-based consensus algorithm for zone health decisions.
package quorum

import (
	"time"

	"k8s.io/klog/v2"

	"github.com/mydns-ng/mydns-health-monitor/pkg/observer"
)

// ConsensusResult represents the quorum decision for a single zone.
type ConsensusResult struct {
	Timestamp       time.Time
	Zone            string
	UnhealthyVoters []string
	HealthyVoters   []string
	TotalVoters     int
	Healthy         bool
	Decided         bool
}

// Engine calculates quorum consensus from collected observations.
type Engine struct{}

// NewEngine creates a new quorum engine.
func NewEngine() *Engine {
	return &Engine{}
}

// voter represents a single monitor's zone and observations for consensus calculation.
type voter struct {
	observations map[string]observer.ZoneObservation
	zone         string
}

// CalculateConsensus determines consensus for each zone based on observations from all monitors.
// MyZone is this monitor's zone (for self-exclusion).
// MyObservations is this monitor's view. PeerReports are observations from other monitors.
// Isolated indicates whether this monitor can reach any peers.
//
// Rules:
//  1. If isolated (can't reach ANY peer), return empty map (no actions).
//  2. Self-exclusion: a monitor never votes on its own zone.
//  3. For each target zone, collect votes from monitors NOT in that zone.
//  4. >50% unhealthy votes → consensus UNHEALTHY. >50% healthy → consensus HEALTHY. Otherwise UNDECIDED.
func (e *Engine) CalculateConsensus(
	myZone string,
	myObservations map[string]observer.ZoneObservation,
	peerReports []observer.ObservationReport,
	isolated bool,
) map[string]ConsensusResult {
	if isolated {
		klog.V(2).Info("Monitor is isolated, skipping consensus (no actions will be taken)")
		return make(map[string]ConsensusResult)
	}

	// Build the list of all voters: self + peers.
	voters := make([]voter, 0, 1+len(peerReports))
	voters = append(voters, voter{
		zone:         myZone,
		observations: myObservations,
	})
	for _, pr := range peerReports {
		voters = append(voters, voter{
			zone:         pr.Monitor.Zone,
			observations: pr.Observations,
		})
	}

	// Collect all unique target zones from all observations.
	targetZones := make(map[string]struct{})
	for _, v := range voters {
		for zone := range v.observations {
			targetZones[zone] = struct{}{}
		}
	}

	now := time.Now()
	results := make(map[string]ConsensusResult, len(targetZones))

	for target := range targetZones {
		var healthyVoters []string
		var unhealthyVoters []string
		totalVoters := 0

		for _, v := range voters {
			// Self-exclusion: a monitor never votes on its own zone.
			if v.zone == target {
				continue
			}

			obs, hasObs := v.observations[target]
			if !hasObs {
				continue
			}

			totalVoters++
			if obs.Healthy {
				healthyVoters = append(healthyVoters, v.zone)
			} else {
				unhealthyVoters = append(unhealthyVoters, v.zone)
			}
		}

		result := ConsensusResult{
			Zone:            target,
			TotalVoters:     totalVoters,
			HealthyVoters:   healthyVoters,
			UnhealthyVoters: unhealthyVoters,
			Timestamp:       now,
		}

		switch {
		case totalVoters == 0:
			// No eligible voters with observations for this zone.
			result.Decided = false
			result.Healthy = false
		case len(unhealthyVoters) > totalVoters/2:
			// Strict majority unhealthy.
			result.Decided = true
			result.Healthy = false
		case len(healthyVoters) > totalVoters/2:
			// Strict majority healthy.
			result.Decided = true
			result.Healthy = true
		default:
			// No majority either way → undecided.
			result.Decided = false
			result.Healthy = false
		}

		results[target] = result

		klog.V(4).Infof("Consensus for zone %q: decided=%t healthy=%t (healthy=%d unhealthy=%d total=%d)",
			target, result.Decided, result.Healthy,
			len(healthyVoters), len(unhealthyVoters), totalVoters)
	}

	return results
}
