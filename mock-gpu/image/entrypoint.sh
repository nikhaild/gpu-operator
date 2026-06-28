#!/usr/bin/env bash
# entrypoint.sh - Single image, two modes (PLAN §5.5, §6).
#
#   MODE=bootstrap : ungated DaemonSet we deploy. OWNS the module lifecycle:
#                    loads mock_gpu.ko so the fake PCI device appears and NFD
#                    sets the pci-10de.present gate. Unloads on SIGTERM.
#   MODE=driver    : run by the operator as its driver DaemonSet. Ensures the
#                    module is loaded (idempotent), populates the driver root so
#                    the toolkit injects the mock NVML, writes readiness
#                    sentinels. Does NOT unload (bootstrap owns that).
#
# Safety: never proceeds if the baked .ko vermagic != running kernel.
set -euo pipefail

MODULE_NAME="mock_gpu"
KO_PATH="${KO_PATH:-/opt/mock-gpu/${MODULE_NAME}.ko}"

# Module params (env, defaulted; set by the DaemonSet from versions.mk).
MOCK_GPU_COUNT="${MOCK_GPU_COUNT:-1}"
MOCK_ENABLE_PCI="${MOCK_ENABLE_PCI:-1}"
MOCK_PCI_DOMAIN="${MOCK_PCI_DOMAIN:-0x1000}"
MOCK_PCI_BUSNR="${MOCK_PCI_BUSNR:-0}"

log() { echo "[mock-gpu entrypoint] $*"; }
die() { echo "[mock-gpu entrypoint] FATAL: $*" >&2; exit 1; }

ep_is_loaded() {
  [ -d "/sys/module/${MODULE_NAME}" ] || lsmod 2>/dev/null | grep -q "^${MODULE_NAME} "
}

ep_vermagic_check() {
  [ -f "$KO_PATH" ] || die "module not found at $KO_PATH"
  local running vm
  running="$(uname -r)"
  vm="$(modinfo -F vermagic "$KO_PATH" 2>/dev/null | awk '{print $1}')"
  [ -n "$vm" ] || die "cannot read vermagic from $KO_PATH"
  [ "$vm" = "$running" ] || die "vermagic mismatch: .ko='$vm' running='$running' (rebuild for this kernel)"
  log "vermagic OK ($vm)"
}

ep_load_module() {
  if ep_is_loaded; then
    log "module ${MODULE_NAME} already loaded; skipping insmod"
    return 0
  fi
  log "insmod ${KO_PATH} count=${MOCK_GPU_COUNT} enable_pci=${MOCK_ENABLE_PCI} pci_domain=${MOCK_PCI_DOMAIN} pci_busnr=${MOCK_PCI_BUSNR}"
  insmod "$KO_PATH" \
    count="$MOCK_GPU_COUNT" \
    enable_pci="$MOCK_ENABLE_PCI" \
    pci_domain="$MOCK_PCI_DOMAIN" \
    pci_busnr="$MOCK_PCI_BUSNR"
  log "module loaded"
}

ep_unload_module() {
  if ep_is_loaded; then
    log "rmmod ${MODULE_NAME}"
    rmmod "$MODULE_NAME" || log "WARN: rmmod failed"
  fi
}

ep_install_driver_root() {
  SRC_DIR="${SRC_DIR:-/opt/mock-gpu}" \
  DRIVER_ROOT="${DRIVER_ROOT:-/run/nvidia/driver}" \
  VALIDATION_DIR="${VALIDATION_DIR:-/run/nvidia/validations}" \
  MOCK_NV_PRODUCT_NAME="${MOCK_NV_PRODUCT_NAME:-}" \
    bash /opt/mock-gpu/install.sh
}

ep_hold() {
  # Block until signalled; the trap handles cleanup.
  log "ready; holding (mode=${MODE})"
  while true; do sleep 3600 & wait $! || true; done
}

ep_mode_bootstrap() {
  ep_vermagic_check
  ep_load_module
  trap 'log "SIGTERM: unloading module (bootstrap owns lifecycle)"; ep_unload_module; exit 0' TERM INT
  ep_hold
}

ep_mode_driver() {
  ep_vermagic_check
  ep_load_module                 # idempotent; bootstrap usually loaded it already
  ep_install_driver_root
  trap 'log "SIGTERM: leaving module loaded (bootstrap owns it); exiting"; exit 0' TERM INT
  ep_hold
}

ep_main() {
  MODE="${MODE:-${1:-}}"
  case "$MODE" in
    bootstrap) ep_mode_bootstrap ;;
    driver)    ep_mode_driver ;;
    *)         die "unknown MODE='${MODE}' (expected 'bootstrap' or 'driver')" ;;
  esac
}

# Only run when executed directly (so bats can source the functions).
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  ep_main "$@"
fi
