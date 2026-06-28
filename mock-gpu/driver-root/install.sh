#!/usr/bin/env bash
# install.sh - Populate the NVIDIA "driver root" with the mock userspace so the
# operator's toolkit (libnvidia-container) injects our mock libnvidia-ml.so.1
# into GFD / device-plugin (PLAN §5.3, §5.5).
#
# Layout produced (validate against the pinned operator version during E2E —
# the exact driver-root path/scheme can vary by operator release):
#   $DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libnvidia-ml.so.1 (+ .so symlink)
#   $DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libcuda.so.1       (+ .so symlink)
#   $DRIVER_ROOT/usr/bin/nvidia-smi
#   $DRIVER_ROOT/etc/ld.so.cache                          (via ldconfig -r)
#   $VALIDATION_DIR/{driver,toolkit}-ready                 readiness sentinels
#
# Idempotent. Sourceable for unit tests (functions guarded; see bottom).
set -euo pipefail

# Inputs (overridable; bats tests point these at temp dirs).
SRC_DIR="${SRC_DIR:-/opt/mock-gpu}"                 # baked artifacts in the image
DRIVER_ROOT="${DRIVER_ROOT:-/run/nvidia/driver}"
VALIDATION_DIR="${VALIDATION_DIR:-/run/nvidia/validations}"
ARCH_TRIPLET="${ARCH_TRIPLET:-x86_64-linux-gnu}"

log() { echo "[mock-driver-root] $*"; }

dr_install_libs() {
  local libdir="$DRIVER_ROOT/usr/lib/$ARCH_TRIPLET"
  mkdir -p "$libdir"

  install -m 0755 "$SRC_DIR/lib/libnvidia-ml.so.1" "$libdir/libnvidia-ml.so.1"
  ln -sf "libnvidia-ml.so.1" "$libdir/libnvidia-ml.so"

  if [ -f "$SRC_DIR/lib/libcuda.so.1" ]; then
    install -m 0755 "$SRC_DIR/lib/libcuda.so.1" "$libdir/libcuda.so.1"
    ln -sf "libcuda.so.1" "$libdir/libcuda.so"
  fi
  log "installed libs into $libdir"
}

dr_install_smi() {
  local bindir="$DRIVER_ROOT/usr/bin"
  mkdir -p "$bindir"
  install -m 0755 "$SRC_DIR/bin/nvidia-smi" "$bindir/nvidia-smi"
  log "installed nvidia-smi shim into $bindir"
}

dr_build_ldcache() {
  # Build an ld.so.cache rooted at $DRIVER_ROOT so libnvidia-container resolves
  # the mock libs from the driver root.
  mkdir -p "$DRIVER_ROOT/etc"
  printf '/usr/lib/%s\n' "$ARCH_TRIPLET" > "$DRIVER_ROOT/etc/ld.so.conf"
  if command -v ldconfig >/dev/null 2>&1; then
    ldconfig -r "$DRIVER_ROOT" || log "WARN: ldconfig -r failed (continuing)"
  else
    log "WARN: ldconfig not found; skipping ld.so.cache"
  fi
  log "built ld.so.cache under $DRIVER_ROOT/etc"
}

dr_write_sentinels() {
  mkdir -p "$VALIDATION_DIR"
  : > "$VALIDATION_DIR/driver-ready"
  : > "$VALIDATION_DIR/toolkit-ready"
  log "wrote readiness sentinels in $VALIDATION_DIR"
}

dr_install_all() {
  dr_install_libs
  dr_install_smi
  dr_build_ldcache
  dr_write_sentinels
  log "driver root ready at $DRIVER_ROOT"
}

# Only run when executed directly (so bats can source the functions).
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  dr_install_all
fi
