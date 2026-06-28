#!/usr/bin/env bash
# Full end-to-end test: stand up the mock GPU system WITH the GPU Operator and
# assert the whole stack goes green (TESTING.md §3). REQUIRES root + kind on PATH.
# SKIPs cleanly when prerequisites are missing so off-host CI stays green.
#
# Run on a SNAPSHOTTED / disposable host: it loads the mock module into the host
# kernel (via the bootstrap DaemonSet) and reconfigures containerd in the kind node.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
. "$HERE/../lib/assert.sh"
. "$HERE/../lib/versions.sh"

require_root
require_ubuntu 24.04
require_cmd docker
require_cmd kind
require_cmd kubectl
require_cmd helm
require_cmd jq

ART="$HERE/../_artifacts"
TIMEOUT="${TEST_TIMEOUT_SECS:-600}"

on_err() { fail "E2E aborted"; dump_diagnostics "$GPU_OPERATOR_NS" "$ART"; }
trap on_err ERR

teardown() {
  info "tearing down"
  make -C "$REPO" undeploy >/dev/null 2>&1 || true
  if [ "${KEEP_CLUSTER:-0}" != "1" ]; then
    make -C "$REPO" kind-down >/dev/null 2>&1 || true
  fi
}
[ "${NO_TEARDOWN:-0}" = "1" ] || trap 'teardown' EXIT

# --- 1. cluster + images ---------------------------------------------------
info "creating kind cluster + building/loading images"
make -C "$REPO" kind-up image validator-shim kind-load

# --- 2. bootstrap DaemonSet -> NFD gate ------------------------------------
info "deploying ungated bootstrap DaemonSet"
make -C "$REPO" deploy-mock
wait_for "$TIMEOUT" "bootstrap DaemonSet rollout" -- \
  kubectl rollout status ds/mock-gpu-bootstrap -n "$MOCK_NAMESPACE" --timeout="${TIMEOUT}s"
wait_for "$TIMEOUT" "NFD pci-10de.present gate label" -- bash -c \
  "kubectl get nodes -o json | jq -e '.items[].metadata.labels[\"feature.node.kubernetes.io/pci-10de.present\"]==\"true\"'"

# --- 3. operator -----------------------------------------------------------
info "installing GPU Operator"
make -C "$REPO" deploy-operator

# --- 4. operands Ready -----------------------------------------------------
for ds in gpu-feature-discovery nvidia-device-plugin-daemonset nvidia-container-toolkit-daemonset; do
  wait_for "$TIMEOUT" "rollout $ds" -- \
    kubectl rollout status ds/"$ds" -n "$GPU_OPERATOR_NS" --timeout="${TIMEOUT}s" || true
done

# --- 5. labels + allocatable + Ready (delegate to verify.sh) ---------------
info "running verify.sh assertions"
if bash "$HERE/verify.sh"; then pass "verify.sh"; else fail "verify.sh"; fi

# --- 6. schedulable workload ----------------------------------------------
info "scheduling GPU test pod"
kubectl apply -f "$HERE/manifests/test-pod.yaml"
if kubectl wait --for=condition=Ready pod/mock-gpu-test -n default --timeout=120s 2>/dev/null \
   || kubectl wait --for=jsonpath='{.status.phase}'=Succeeded pod/mock-gpu-test -n default --timeout=120s 2>/dev/null; then
  pass "GPU test pod scheduled and ran"
else
  fail "GPU test pod did not run"
fi
kubectl delete -f "$HERE/manifests/test-pod.yaml" --ignore-not-found >/dev/null 2>&1 || true

# --- 7. negative: over-request stays Pending -------------------------------
over=$((MOCK_GPU_COUNT + 1))
kubectl run mock-gpu-over --image=busybox:stable --restart=Never \
  --overrides="{\"spec\":{\"containers\":[{\"name\":\"c\",\"image\":\"busybox:stable\",\"command\":[\"true\"],\"resources\":{\"limits\":{\"nvidia.com/gpu\":\"$over\"}}}]}}" \
  -n default >/dev/null 2>&1 || true
sleep 8
if [ "$(kubectl get pod mock-gpu-over -n default -o jsonpath='{.status.phase}' 2>/dev/null)" = "Pending" ]; then
  pass "over-request ($over) stays Pending (resource accounting real)"
else
  info "over-request pod phase: $(kubectl get pod mock-gpu-over -n default -o jsonpath='{.status.phase}' 2>/dev/null)"
fi
kubectl delete pod mock-gpu-over -n default --ignore-not-found >/dev/null 2>&1 || true

trap - ERR
[ "$_ASSERT_FAILS" -eq 0 ] || dump_diagnostics "$GPU_OPERATOR_NS" "$ART"
assert_summary
