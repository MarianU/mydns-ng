// Package main is the entry point for the mydns-acme-webhook.
package main

import (
	"os"

	"k8s.io/klog/v2"

	"github.com/cert-manager/cert-manager/pkg/acme/webhook/cmd"
	"github.com/mydns-ng/mydns-acme-webhook/pkg/mydns"
)

// GroupName is the API group name for the webhook.
// It is read from the GROUP_NAME environment variable.
var GroupName = os.Getenv("GROUP_NAME")

func main() {
	// Initialize klog flags
	klog.InitFlags(nil)

	if GroupName == "" {
		klog.Fatal("GROUP_NAME environment variable must be set")
	}

	klog.Infof("Starting mydns-acme-webhook with group name: %s", GroupName)

	// Register and run the webhook server
	cmd.RunWebhookServer(GroupName, mydns.NewSolver())
}
