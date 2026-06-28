#!/bin/sh
# Validator no-op shim (PLAN §3, §5.6).
#
# Replaces the gpu-operator-validator image so EVERY validation passes on the
# mock GPU (the real cuda validation would run a CUDA workload that cannot work).
#
# The operator uses the validator image two ways:
#   - short-lived init/component validations -> must EXIT 0
#   - the long-running validator daemonset container -> must STAY running
# We distinguish them heuristically: a single named component => exit 0;
# otherwise treat as the long-running container and sleep.
#
# IMPORTANT (E2E): the exact invocation/readiness-file contract is operator-
# version specific. Validate and adjust against the pinned GPU_OPERATOR_VERSION.
set -u

echo "[validator-shim] argv: $*  COMPONENT=${COMPONENT:-} WITH_WORKLOAD=${WITH_WORKLOAD:-}"

# Best-effort: satisfy any readiness-file pollers.
mkdir -p /run/nvidia/validations 2>/dev/null || true
for f in driver toolkit cuda plugin nvidia-fs vfio-pci cc-manager workload all; do
  : > "/run/nvidia/validations/${f}-ready" 2>/dev/null || true
done

# Decide run-once vs long-running.
component="${COMPONENT:-}"
for a in "$@"; do
  case "$a" in
    --component=*) component="${a#*=}" ;;
    --component)   : ;;  # next arg is value; handled by COMPONENT env in practice
  esac
done

if [ -n "$component" ] && [ "$component" != "all" ]; then
  echo "[validator-shim] component '$component' validated (no-op), exiting 0"
  exit 0
fi

# Long-running validator container: hold so the pod stays Ready.
echo "[validator-shim] running as long-lived validator; holding"
while true; do sleep 3600; done
