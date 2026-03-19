// Package observer provides HTTP API types and client/server for sharing health observations between monitors.
package observer

import "time"

// MonitorIdentity identifies a health monitor instance.
type MonitorIdentity struct {
	Zone      string    `json:"zone"`
	PodName   string    `json:"podName"`
	PodIP     string    `json:"podIP"`
	StartedAt time.Time `json:"startedAt"`
	Version   string    `json:"version"`
}

// ZoneObservation represents a monitor's health observation of a single zone.
type ZoneObservation struct {
	LastCheck            time.Time `json:"lastCheck"`
	Healthy              bool      `json:"healthy"`
	ReadyNodes           int       `json:"readyNodes"`
	TotalNodes           int       `json:"totalNodes"`
	ConsecutiveFailures  int       `json:"consecutiveFailures"`
	ConsecutiveSuccesses int       `json:"consecutiveSuccesses"`
}

// ObservationReport is the full observation payload shared between monitors.
type ObservationReport struct {
	Observations map[string]ZoneObservation `json:"observations"`
	Monitor      MonitorIdentity            `json:"monitor"`
}
