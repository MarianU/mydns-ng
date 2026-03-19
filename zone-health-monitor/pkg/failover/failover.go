// Package failover manages database record activation and deactivation based on quorum consensus.
package failover

import (
	"context"
	"fmt"
	"time"

	"k8s.io/klog/v2"

	"github.com/mydns-ng/mydns-health-monitor/pkg/mydns"
	"github.com/mydns-ng/mydns-health-monitor/pkg/quorum"
)

// Manager handles failover actions based on quorum consensus results.
type Manager struct {
	db     *mydns.DBClient
	dryRun bool

	// Track which zones are currently in failover state to avoid redundant DB operations.
	failedZones map[string]time.Time // zone name → time of failover
}

// NewManager creates a new failover manager.
func NewManager(db *mydns.DBClient, dryRun bool) *Manager {
	return &Manager{
		db:          db,
		dryRun:      dryRun,
		failedZones: make(map[string]time.Time),
	}
}

// ProcessConsensus processes quorum consensus results and takes appropriate database actions.
// For each zone with a decided consensus:
//   - If UNHEALTHY and not already failed over → deactivate records.
//   - If HEALTHY and currently in failover → reactivate records.
//
// monitorInfo is used for audit logging (pod name, zone).
func (m *Manager) ProcessConsensus(ctx context.Context, results map[string]quorum.ConsensusResult, podName, myZone string) error {
	for zone, result := range results {
		if !result.Decided {
			continue
		}

		if !result.Healthy {
			// Zone is unhealthy - deactivate if not already done.
			if _, alreadyFailed := m.failedZones[zone]; alreadyFailed {
				klog.V(4).Infof("Zone %q already in failover, skipping deactivation", zone)
				continue
			}
			if err := m.deactivateZone(ctx, zone, podName, myZone); err != nil {
				return fmt.Errorf("failed to deactivate zone %q: %w", zone, err)
			}
		} else {
			// Zone is healthy - reactivate if currently in failover.
			if _, inFailover := m.failedZones[zone]; !inFailover {
				klog.V(4).Infof("Zone %q not in failover, skipping reactivation", zone)
				continue
			}
			if err := m.reactivateZone(ctx, zone, podName, myZone); err != nil {
				return fmt.Errorf("failed to reactivate zone %q: %w", zone, err)
			}
		}
	}
	return nil
}

// deactivateZone deactivates records for a zone.
func (m *Manager) deactivateZone(ctx context.Context, zone, podName, myZone string) error {
	klog.V(2).Infof("Deactivating records for zone %q (monitor=%s/%s)", zone, myZone, podName)

	if m.dryRun {
		klog.Infof("[DRY-RUN] Would deactivate records for zone %q", zone)
		m.failedZones[zone] = time.Now()
		return nil
	}

	affected, err := m.db.DeactivateZoneRecords(ctx, zone)
	if err != nil {
		return err
	}

	if affected > 0 {
		klog.Infof("Deactivated %d records for zone %q (monitor=%s/%s)", affected, zone, myZone, podName)
		m.failedZones[zone] = time.Now()
	} else {
		klog.V(2).Infof("No records to deactivate for zone %q (already handled by another monitor or no records)", zone)
		// Still mark as failed so we can reactivate later.
		m.failedZones[zone] = time.Now()
	}

	return nil
}

// reactivateZone reactivates records for a zone.
func (m *Manager) reactivateZone(ctx context.Context, zone, podName, myZone string) error {
	klog.V(2).Infof("Reactivating records for zone %q (monitor=%s/%s)", zone, myZone, podName)

	if m.dryRun {
		klog.Infof("[DRY-RUN] Would reactivate records for zone %q", zone)
		delete(m.failedZones, zone)
		return nil
	}

	affected, err := m.db.ReactivateZoneRecords(ctx, zone)
	if err != nil {
		return err
	}

	if affected > 0 {
		klog.Infof("Reactivated %d records for zone %q (monitor=%s/%s)", affected, zone, myZone, podName)
	} else {
		klog.V(2).Infof("No records to reactivate for zone %q (already handled by another monitor)", zone)
	}

	delete(m.failedZones, zone)
	return nil
}

// GetFailedZones returns the current set of zones in failover state.
func (m *Manager) GetFailedZones() map[string]time.Time {
	result := make(map[string]time.Time, len(m.failedZones))
	for k, v := range m.failedZones {
		result[k] = v
	}
	return result
}
