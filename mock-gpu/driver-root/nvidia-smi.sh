#!/usr/bin/env bash
# nvidia-smi.sh - Mock nvidia-smi shim (PLAN §5.4).
#
# Installed into the driver root as `nvidia-smi`. The operator's driver- and
# toolkit-validation containers run nvidia-smi and check it exits 0; some paths
# parse the driver/CUDA version line. This prints plausible output and always
# exits 0. It performs NO GPU work.
#
# Values come from env (set by the image build from versions.mk), with defaults.
set -u

PRODUCT="${MOCK_NV_PRODUCT_NAME:-Tesla T4}"
DRIVER="${MOCK_NV_DRIVER_VERSION:-535.104.05}"
CUDA="${MOCK_NV_CUDA_VERSION:-12.2}"
MEM_MIB="${MOCK_NV_MEMORY_MIB:-16384}"
COUNT="${NVML_MOCK_GPU_COUNT:-1}"

uuid_for() { printf 'GPU-%08x-0000-4000-8000-%012x' "$((0x10de0000 + $1))" "$1"; }

# --list-gpus / -L
for arg in "$@"; do
  case "$arg" in
    -L|--list-gpus)
      i=0
      while [ "$i" -lt "$COUNT" ]; do
        echo "GPU $i: $PRODUCT (UUID: $(uuid_for "$i"))"
        i=$((i+1))
      done
      exit 0
      ;;
  esac
done

# --query-gpu=...  --format=csv[,noheader]   (best-effort CSV)
QUERY=""
NOHEADER=0
for arg in "$@"; do
  case "$arg" in
    --query-gpu=*) QUERY="${arg#*=}" ;;
    --format=*)    case "$arg" in *noheader*) NOHEADER=1 ;; esac ;;
  esac
done
if [ -n "$QUERY" ]; then
  [ "$NOHEADER" -eq 1 ] || echo "$QUERY"
  i=0
  while [ "$i" -lt "$COUNT" ]; do
    line=""
    IFS=',' read -ra fields <<< "$QUERY"
    for f in "${fields[@]}"; do
      f="$(echo "$f" | tr -d ' ')"
      case "$f" in
        index)               val="$i" ;;
        name|gpu_name)       val="$PRODUCT" ;;
        uuid|gpu_uuid)       val="$(uuid_for "$i")" ;;
        driver_version)      val="$DRIVER" ;;
        memory.total)        val="${MEM_MIB} MiB" ;;
        memory.free)         val="${MEM_MIB} MiB" ;;
        memory.used)         val="0 MiB" ;;
        compute_cap)         val="7.5" ;;
        *)                   val="N/A" ;;
      esac
      [ -z "$line" ] && line="$val" || line="$line, $val"
    done
    echo "$line"
    i=$((i+1))
  done
  exit 0
fi

# Default table view.
cat <<EOF
+-----------------------------------------------------------------------------+
| NVIDIA-SMI ${DRIVER}    Driver Version: ${DRIVER}    CUDA Version: ${CUDA}   |
|-------------------------------+----------------------+----------------------+
| GPU  Name        Persistence-M| Bus-Id        Disp.A | Volatile Uncorr. ECC |
| Fan  Temp  Perf  Pwr:Usage/Cap|         Memory-Usage | GPU-Util  Compute M. |
|===============================+======================+======================|
EOF
i=0
while [ "$i" -lt "$COUNT" ]; do
  printf '|   %d  %-11s      Off | 1000:00:%02X.0     Off |                    0 |\n' "$i" "$PRODUCT" "$i"
  printf '| N/A   N/A    P0    N/A / N/A |      0MiB / %5dMiB |      0%%      Default |\n' "$MEM_MIB"
  i=$((i+1))
done
cat <<'EOF'
+-----------------------------------------------------------------------------+
EOF
exit 0
