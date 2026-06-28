#!/usr/bin/env bash
# Quick E2E assertions (TESTING.md §3.1/§3.2): node labels, allocatable GPU,
# operator pods Ready. Used by `make verify`. SKIPs cleanly if no cluster.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../lib/assert.sh"
. "$HERE/../lib/versions.sh"

require_cmd kubectl
require_cmd jq
kubectl cluster-info >/dev/null 2>&1 || { info "SKIP: no reachable cluster"; exit 0; }

NODE_JSON="$(kubectl get nodes -o json)"

# 1. NFD gate label present on some node
if echo "$NODE_JSON" | jq -e '.items[].metadata.labels["feature.node.kubernetes.io/pci-10de.present"]=="true"' >/dev/null; then
  pass "NFD gate: feature.node.kubernetes.io/pci-10de.present=true"
else
  fail "NFD gate label missing"
fi

# 2. GFD product label present
if echo "$NODE_JSON" | jq -e '.items[].metadata.labels["nvidia.com/gpu.product"] // empty | length>0' >/dev/null; then
  pass "GFD label nvidia.com/gpu.product present"
else
  fail "GFD label nvidia.com/gpu.product missing"
fi

# 3. allocatable nvidia.com/gpu == MOCK_GPU_COUNT (on at least one node)
alloc="$(echo "$NODE_JSON" | jq -r '[.items[].status.allocatable["nvidia.com/gpu"] // "0" | tonumber] | max')"
assert_eq "$MOCK_GPU_COUNT" "$alloc" "allocatable nvidia.com/gpu"

# 4. all operator pods Ready (no non-Running/Completed)
notready="$(kubectl get pods -n "$GPU_OPERATOR_NS" --no-headers 2>/dev/null | grep -vE 'Running|Completed' || true)"
if [ -z "$notready" ]; then
  pass "all pods in $GPU_OPERATOR_NS are Running/Completed"
else
  fail "non-ready pods in $GPU_OPERATOR_NS:"; echo "$notready"
fi

# 5. disabled components absent
for app in nvidia-dcgm-exporter nvidia-dcgm nvidia-mig-manager; do
  if kubectl get pods -n "$GPU_OPERATOR_NS" -l app="$app" --no-headers 2>/dev/null | grep -q .; then
    fail "disabled component present: $app"
  else
    pass "disabled component absent: $app"
  fi
done

[ "$_ASSERT_FAILS" -eq 0 ] || dump_diagnostics "$GPU_OPERATOR_NS" "$HERE/../_artifacts"
assert_summary
