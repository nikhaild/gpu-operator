#!/usr/bin/env bats
# Unit tests for driver-root/install.sh (TESTING.md §1.3).
# Sources the script's functions and runs them against temp dirs (no real
# /run/nvidia, no kernel).

setup() {
  REPO="$BATS_TEST_DIRNAME/../../.."
  TMP="$(mktemp -d)"
  export SRC_DIR="$TMP/src"
  export DRIVER_ROOT="$TMP/root"
  export VALIDATION_DIR="$TMP/validations"
  export ARCH_TRIPLET="x86_64-linux-gnu"

  mkdir -p "$SRC_DIR/lib" "$SRC_DIR/bin"
  # Minimal fake artifacts.
  echo "fake-ml"  > "$SRC_DIR/lib/libnvidia-ml.so.1"
  echo "fake-cuda" > "$SRC_DIR/lib/libcuda.so.1"
  install -m 0755 "$REPO/driver-root/nvidia-smi.sh" "$SRC_DIR/bin/nvidia-smi"

  # shellcheck disable=SC1090
  source "$REPO/driver-root/install.sh"
}

teardown() { rm -rf "$TMP"; }

@test "install places libnvidia-ml.so.1 and .so symlink" {
  dr_install_libs
  [ -f "$DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libnvidia-ml.so.1" ]
  [ -L "$DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libnvidia-ml.so" ]
}

@test "install places libcuda stub" {
  dr_install_libs
  [ -f "$DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libcuda.so.1" ]
}

@test "install places nvidia-smi shim, executable" {
  dr_install_smi
  [ -x "$DRIVER_ROOT/usr/bin/nvidia-smi" ]
}

@test "build_ldcache creates ld.so.conf (and cache if ldconfig present)" {
  dr_install_libs
  dr_build_ldcache
  [ -f "$DRIVER_ROOT/etc/ld.so.conf" ]
}

@test "write_sentinels creates driver-ready and toolkit-ready" {
  dr_write_sentinels
  [ -f "$VALIDATION_DIR/driver-ready" ]
  [ -f "$VALIDATION_DIR/toolkit-ready" ]
}

@test "dr_install_all is idempotent (second run succeeds)" {
  dr_install_all
  dr_install_all
  [ -f "$DRIVER_ROOT/usr/lib/$ARCH_TRIPLET/libnvidia-ml.so.1" ]
}
