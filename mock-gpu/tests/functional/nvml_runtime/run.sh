#!/usr/bin/env bash
# Functional NVML test (TESTING.md §2.2): run the mock libnvidia-ml.so.1 through
# the real NVIDIA/go-nvml binding inside a golang container, assert the values.
# SAFE: does not touch the kernel (no module load).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../../lib/assert.sh"
include_versions() { eval "$(make -s -C "$HERE/../../.." -f - <<'EOF' 2>/dev/null
include versions.mk
p:
	@echo "NV_PRODUCT_NAME='$(NV_PRODUCT_NAME)'"
	@echo "NV_DRIVER_VERSION='$(NV_DRIVER_VERSION)'"
	@echo "NV_MEMORY_MIB='$(NV_MEMORY_MIB)'"
	@echo "NV_COMPUTE_MAJOR='$(NV_COMPUTE_MAJOR)'"
	@echo "NV_COMPUTE_MINOR='$(NV_COMPUTE_MINOR)'"
	@echo "MOCK_GPU_COUNT='$(MOCK_GPU_COUNT)'"
EOF
)"; }

require_cmd docker
require_cmd jq

REPO="$(cd "$HERE/../../.." && pwd)"
LIB="$REPO/build/libnvidia-ml.so.1"
if [ ! -f "$LIB" ]; then
  info "building mock NVML (.so) first"
  make -C "$REPO" nvml >/dev/null || { fail "nvml build failed"; assert_summary; }
fi
include_versions

OUT="$HERE/out.json"
info "running go-nvml against the mock .so (golang container)"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$REPO":/work -w /work/tests/functional/nvml_runtime \
  -e LD_LIBRARY_PATH=/work/build \
  -e GOFLAGS=-mod=mod \
  -e GOCACHE=/tmp/.gocache -e GOMODCACHE=/tmp/.gomodcache -e HOME=/tmp \
  golang:1.23 bash -c "go mod tidy >/dev/null 2>&1; go run ." > "$OUT" 2> "$HERE/err.log"
rc=$?
if [ $rc -ne 0 ]; then
  fail "go-nvml run failed (rc=$rc)"; cat "$HERE/err.log" >&2; assert_summary
fi

cat "$OUT"
assert_eq "$NV_DRIVER_VERSION" "$(jq -r .driverVersion "$OUT")" "driver version via go-nvml"
assert_eq "$MOCK_GPU_COUNT"    "$(jq -r .count "$OUT")"          "device count via go-nvml"
assert_eq "$NV_PRODUCT_NAME"   "$(jq -r '.devices[0].name' "$OUT")" "product name via go-nvml"
assert_eq "$((NV_MEMORY_MIB*1024*1024))" "$(jq -r '.devices[0].memoryTotal' "$OUT")" "memory total via go-nvml"
assert_eq "$NV_COMPUTE_MAJOR"  "$(jq -r '.devices[0].ccMajor' "$OUT")" "compute major via go-nvml"
assert_eq "$NV_COMPUTE_MINOR"  "$(jq -r '.devices[0].ccMinor' "$OUT")" "compute minor via go-nvml"
case "$(jq -r '.devices[0].uuid' "$OUT")" in
  GPU-*) pass "UUID starts with GPU- via go-nvml" ;;
  *)     fail "UUID format via go-nvml" ;;
esac
rm -f "$OUT" "$HERE/err.log"
assert_summary
