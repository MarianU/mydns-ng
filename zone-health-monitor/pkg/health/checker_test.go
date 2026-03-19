package health

import (
	"context"
	"testing"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes/fake"
)

const (
	zoneLabel = "topology.kubernetes.io/zone"
)

func newNode(name, zone string, ready bool) *corev1.Node {
	status := corev1.ConditionFalse
	if ready {
		status = corev1.ConditionTrue
	}
	return &corev1.Node{
		ObjectMeta: metav1.ObjectMeta{
			Name:   name,
			Labels: map[string]string{zoneLabel: zone},
		},
		Status: corev1.NodeStatus{
			Conditions: []corev1.NodeCondition{
				{
					Type:   corev1.NodeReady,
					Status: status,
				},
			},
		},
	}
}

func TestCheckZone_AllReady(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
		newNode("node-2", "zone-a", true),
		newNode("node-3", "zone-a", true),
	)

	checker := NewChecker(client, "zone-b", 3, 1)
	ctx := context.Background()

	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh == nil {
		t.Fatal("expected non-nil ZoneHealth")
	}
	if zh.ReadyNodes != 3 {
		t.Errorf("expected 3 ready nodes, got %d", zh.ReadyNodes)
	}
	if zh.TotalNodes != 3 {
		t.Errorf("expected 3 total nodes, got %d", zh.TotalNodes)
	}
	if !zh.Healthy {
		t.Error("expected zone to be healthy")
	}
}

func TestCheckZone_NoReadyNodes(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", false),
		newNode("node-2", "zone-a", false),
	)

	// Failure threshold of 2: need 2 consecutive failures to report unhealthy.
	checker := NewChecker(client, "zone-b", 2, 1)
	ctx := context.Background()

	// First check: consecutiveFailures=1, still healthy (below threshold).
	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !zh.Healthy {
		t.Error("expected zone to still be healthy after first failure (below threshold)")
	}

	// Second check: consecutiveFailures=2, now unhealthy.
	zh, err = checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh.Healthy {
		t.Error("expected zone to be unhealthy after reaching failure threshold")
	}
	if zh.ReadyNodes != 0 {
		t.Errorf("expected 0 ready nodes, got %d", zh.ReadyNodes)
	}
	if zh.TotalNodes != 2 {
		t.Errorf("expected 2 total nodes, got %d", zh.TotalNodes)
	}
}

func TestCheckZone_ZeroTotalNodes(t *testing.T) {
	// No nodes in zone-a at all.
	client := fake.NewSimpleClientset()

	checker := NewChecker(client, "zone-b", 2, 1)
	ctx := context.Background()

	// First check: consecutiveFailures=1, still healthy.
	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh.TotalNodes != 0 {
		t.Errorf("expected 0 total nodes, got %d", zh.TotalNodes)
	}
	if !zh.Healthy {
		t.Error("expected zone to still be healthy after first failure")
	}

	// Second check: reaches threshold.
	zh, err = checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh.Healthy {
		t.Error("expected zone to be unhealthy after reaching failure threshold")
	}
}

func TestCheckZone_SelfExclusion(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
	)

	checker := NewChecker(client, "zone-a", 3, 1)
	ctx := context.Background()

	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh != nil {
		t.Error("expected nil ZoneHealth for self-zone")
	}
}

func TestCheckZone_Hysteresis(t *testing.T) {
	// Start with healthy nodes, then flap.
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
	)

	// Failure threshold=3, success threshold=2.
	checker := NewChecker(client, "zone-b", 3, 2)
	ctx := context.Background()

	// Initial check: healthy.
	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !zh.Healthy {
		t.Error("expected initial check to be healthy")
	}

	// Now make the node not ready and flap.
	// We need to recreate the client with unhealthy node.
	clientUnhealthy := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", false),
	)
	checker.kubeClient = clientUnhealthy

	// Failure 1: still healthy (threshold=3).
	zh, err = checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !zh.Healthy {
		t.Error("expected zone to remain healthy after 1 failure")
	}

	// Failure 2: still healthy.
	zh, err = checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !zh.Healthy {
		t.Error("expected zone to remain healthy after 2 failures")
	}

	// Now recover: this resets consecutive failures.
	clientHealthy := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
	)
	checker.kubeClient = clientHealthy

	zh, err = checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !zh.Healthy {
		t.Error("expected zone to remain healthy after recovery")
	}
	if zh.ConsecutiveFailures != 0 {
		t.Errorf("expected consecutive failures reset, got %d", zh.ConsecutiveFailures)
	}

	// Go unhealthy again: need 3 consecutive failures.
	checker.kubeClient = clientUnhealthy

	// Failure 1.
	zh, _ = checker.CheckZone(ctx, "zone-a")
	if !zh.Healthy {
		t.Error("expected healthy after 1 failure (post-recovery)")
	}

	// Failure 2.
	zh, _ = checker.CheckZone(ctx, "zone-a")
	if !zh.Healthy {
		t.Error("expected healthy after 2 failures (post-recovery)")
	}

	// Failure 3: now should flip to unhealthy.
	zh, _ = checker.CheckZone(ctx, "zone-a")
	if zh.Healthy {
		t.Error("expected unhealthy after 3 consecutive failures")
	}

	// Now recover: need 2 consecutive successes to flip back.
	checker.kubeClient = clientHealthy

	// Success 1: still unhealthy (threshold=2).
	zh, _ = checker.CheckZone(ctx, "zone-a")
	if zh.Healthy {
		t.Error("expected zone to remain unhealthy after 1 success")
	}

	// Success 2: now healthy.
	zh, _ = checker.CheckZone(ctx, "zone-a")
	if !zh.Healthy {
		t.Error("expected zone to become healthy after 2 consecutive successes")
	}
}

func TestCheckZone_MixedReadyNotReady(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
		newNode("node-2", "zone-a", false),
		newNode("node-3", "zone-a", false),
	)

	checker := NewChecker(client, "zone-b", 3, 1)
	ctx := context.Background()

	zh, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if zh.ReadyNodes != 1 {
		t.Errorf("expected 1 ready node, got %d", zh.ReadyNodes)
	}
	if zh.TotalNodes != 3 {
		t.Errorf("expected 3 total nodes, got %d", zh.TotalNodes)
	}
	if !zh.Healthy {
		t.Error("expected zone to be healthy with at least 1 ready node")
	}
}

func TestCheckAllZones(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
		newNode("node-2", "zone-b", true),
		newNode("node-3", "zone-c", false),
	)

	checker := NewChecker(client, "zone-b", 1, 1)
	ctx := context.Background()

	results, err := checker.CheckAllZones(ctx, []string{"zone-a", "zone-b", "zone-c"})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if _, ok := results["zone-b"]; ok {
		t.Error("expected self-zone zone-b to be excluded from results")
	}

	if zh, ok := results["zone-a"]; !ok {
		t.Error("expected zone-a in results")
	} else if !zh.Healthy {
		t.Error("expected zone-a to be healthy")
	}

	if zh, ok := results["zone-c"]; !ok {
		t.Error("expected zone-c in results")
	} else if zh.Healthy {
		t.Error("expected zone-c to be unhealthy (failure threshold=1)")
	}
}

func TestGetState(t *testing.T) {
	client := fake.NewSimpleClientset(
		newNode("node-1", "zone-a", true),
	)

	checker := NewChecker(client, "zone-b", 3, 1)
	ctx := context.Background()

	_, err := checker.CheckZone(ctx, "zone-a")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	state := checker.GetState()
	if len(state) != 1 {
		t.Fatalf("expected 1 zone in state, got %d", len(state))
	}

	zh, ok := state["zone-a"]
	if !ok {
		t.Fatal("expected zone-a in state")
	}
	if !zh.Healthy {
		t.Error("expected zone-a to be healthy in state")
	}

	// Verify it's a copy by mutating.
	zh.Healthy = false
	original := checker.GetState()
	if !original["zone-a"].Healthy {
		t.Error("GetState should return a copy; mutating the copy should not affect internal state")
	}
}
