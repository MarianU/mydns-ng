// Package main is the entry point for the mydns-health-monitor.
package main

import (
	"context"
	"os"
	"os/signal"
	"syscall"

	"k8s.io/klog/v2"

	"github.com/mydns-ng/mydns-health-monitor/pkg/monitor"
)

func main() {
	klog.InitFlags(nil)

	cfg, err := monitor.LoadConfigFromEnv()
	if err != nil {
		klog.Fatalf("Failed to load configuration: %v", err)
	}

	klog.Infof("Starting mydns-health-monitor for zone: %s (pod=%s, dry-run=%v)",
		cfg.MyZone, cfg.PodName, cfg.DryRun)

	mon, err := monitor.New(cfg)
	if err != nil {
		klog.Fatalf("Failed to create monitor: %v", err)
	}

	// Handle shutdown signals.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		klog.Infof("Received signal %v, shutting down", sig)
		cancel()
	}()

	if err := mon.Run(ctx); err != nil {
		klog.Fatalf("Monitor exited with error: %v", err)
	}

	klog.Info("Monitor shut down gracefully")
}
