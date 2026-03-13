package main

import (
	"os"
	"testing"

	acmetest "github.com/cert-manager/cert-manager/test/acme"

	"github.com/mydns-ng/mydns-acme-webhook/pkg/mydns"
)

var (
	zone = os.Getenv("TEST_ZONE_NAME")
)

func TestRunsSuite(t *testing.T) {
	// The manifest path should contain a file named config.json that is a
	// snippet of valid configuration that should be included on the
	// ChallengeRequest passed as part of the test cases.

	if zone == "" {
		zone = "example.com."
	}

	solver := mydns.NewSolver()

	fixture := acmetest.NewFixture(solver,
		acmetest.SetResolvedZone(zone),
		acmetest.SetManifestPath("testdata/mydns-solver"),
		acmetest.SetAllowAmbientCredentials(false),
		// For local testing with a mock DNS server:
		// acmetest.SetDNSServer("127.0.0.1:59351"),
		// acmetest.SetUseAuthoritative(false),
	)

	fixture.RunConformance(t)
}
