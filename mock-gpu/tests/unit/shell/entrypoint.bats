#!/usr/bin/env bats
# Unit tests for image/entrypoint.sh (TESTING.md §1.3).
# insmod/rmmod/lsmod/modinfo are stubbed via a PATH shim dir so NOTHING touches
# the real kernel.

setup() {
  REPO="$BATS_TEST_DIRNAME/../../.."
  TMP="$(mktemp -d)"
  SHIM="$TMP/bin"
  mkdir -p "$SHIM"
  export CALLS="$TMP/calls.log"
  : > "$CALLS"

  # Fake kernel-tool shims that record invocations instead of acting.
  cat > "$SHIM/insmod" <<EOF
#!/usr/bin/env bash
echo "insmod \$*" >> "$CALLS"
touch "$TMP/loaded"
EOF
  cat > "$SHIM/rmmod" <<EOF
#!/usr/bin/env bash
echo "rmmod \$*" >> "$CALLS"
rm -f "$TMP/loaded"
EOF
  cat > "$SHIM/lsmod" <<EOF
#!/usr/bin/env bash
if [ -f "$TMP/loaded" ]; then echo "mock_gpu 16384 0"; fi
EOF
  # modinfo: vermagic = whatever VERMAGIC env says (defaults to running kernel).
  cat > "$SHIM/modinfo" <<EOF
#!/usr/bin/env bash
if [ "\$1" = "-F" ] && [ "\$2" = "vermagic" ]; then
  echo "\${VERMAGIC:-\$(uname -r)} SMP preempt mod_unload modversions"
fi
EOF
  chmod +x "$SHIM"/*
  export PATH="$SHIM:$PATH"

  # A fake .ko so the existence check passes.
  export KO_PATH="$TMP/mock_gpu.ko"
  : > "$KO_PATH"

  # Force /sys/module check to fall through to lsmod (no real module loaded).
  # shellcheck disable=SC1090
  source "$REPO/image/entrypoint.sh"
}

teardown() { rm -rf "$TMP"; }

@test "unknown MODE fails" {
  run ep_main "wibble"
  [ "$status" -ne 0 ]
  [[ "$output" == *"unknown MODE"* ]]
}

@test "vermagic match passes" {
  run ep_vermagic_check
  [ "$status" -eq 0 ]
  [[ "$output" == *"vermagic OK"* ]]
}

@test "vermagic mismatch fails loudly" {
  VERMAGIC="9.9.9-nope" run ep_vermagic_check
  [ "$status" -ne 0 ]
  [[ "$output" == *"vermagic mismatch"* ]]
}

@test "load_module calls insmod when not loaded" {
  run ep_load_module
  [ "$status" -eq 0 ]
  grep -q "insmod" "$CALLS"
}

@test "load_module is idempotent (no second insmod when already loaded)" {
  ep_load_module
  : > "$CALLS"            # reset call log; module now 'loaded'
  run ep_load_module
  [ "$status" -eq 0 ]
  ! grep -q "insmod" "$CALLS"
}

@test "unload_module calls rmmod when loaded" {
  ep_load_module
  run ep_unload_module
  [ "$status" -eq 0 ]
  grep -q "rmmod" "$CALLS"
}
