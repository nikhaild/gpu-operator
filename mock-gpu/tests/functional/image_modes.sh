#!/usr/bin/env bash
# Functional test: run the single image in both modes (TESTING.md §2.3).
# REQUIRES root (bootstrap mode loads the module into the host kernel via a
# privileged container).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../lib/assert.sh"
REPO="$(cd "$HERE/../.." && pwd)"

require_root
require_ubuntu 24.04
require_cmd docker

IMG="${MOCK_IMAGE:-localhost/mock-gpu-driver:dev}"
docker image inspect "$IMG" >/dev/null 2>&1 || { info "building image"; make -C "$REPO" image >/dev/null; }

cleanup() { docker rm -f mockgpu-boot mockgpu-drv >/dev/null 2>&1 || true; rmmod mock_gpu 2>/dev/null || true; }
trap cleanup EXIT

# --- bootstrap mode: loads the module ---
info "running image in MODE=bootstrap"
docker run -d --name mockgpu-boot --privileged --pid=host \
  -v /lib/modules:/lib/modules:ro -v /usr/src:/usr/src:ro -v /dev:/dev -v /sys:/sys \
  "$IMG" bootstrap >/dev/null
sleep 5
assert_cmd_ok "bootstrap loaded mock_gpu" -- bash -c "lsmod | grep -q '^mock_gpu '"
assert_file /proc/driver/nvidia/version "/proc version present after bootstrap"
info "stopping bootstrap (SIGTERM should rmmod)"
docker stop -t 15 mockgpu-boot >/dev/null
sleep 2
assert_cmd_ok "bootstrap unloaded module on SIGTERM" -- bash -c "! lsmod | grep -q '^mock_gpu '"

# --- driver mode: populates the driver root ---
DRV_ROOT="$(mktemp -d)"
info "running image in MODE=driver (driver root -> $DRV_ROOT)"
docker run -d --name mockgpu-drv --privileged --pid=host \
  -v /lib/modules:/lib/modules:ro -v /usr/src:/usr/src:ro -v /dev:/dev -v /sys:/sys \
  -v "$DRV_ROOT":/run/nvidia \
  "$IMG" driver >/dev/null
sleep 6
assert_file "$DRV_ROOT/driver/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1" "driver root has mock NVML"
assert_file "$DRV_ROOT/driver/usr/bin/nvidia-smi" "driver root has nvidia-smi shim"
assert_file "$DRV_ROOT/validations/driver-ready" "driver-ready sentinel written"
docker stop -t 15 mockgpu-drv >/dev/null

cleanup; trap - EXIT
rm -rf "$DRV_ROOT"
assert_summary
