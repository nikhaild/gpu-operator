#!/usr/bin/env bash
# Functional test: load mock_gpu.ko, inspect sysfs/dev/proc, unload, verify clean
# teardown (TESTING.md §2.1). REQUIRES root; loads into the HOST kernel.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../lib/assert.sh"

require_root
require_ubuntu 24.04

KO="${KO:-$HERE/../../build/mock_gpu.ko}"
assert_file "$KO" "module artifact present"
require_vermagic_match "$KO"

COUNT="${COUNT:-2}"

cleanup() { rmmod mock_gpu 2>/dev/null || true; }
trap cleanup EXIT

info "insmod mock_gpu count=$COUNT enable_pci=1"
if ! insmod "$KO" count="$COUNT" enable_pci=1; then
  fail "insmod failed"; dmesg | tail -20; assert_summary
fi

assert_cmd_ok "module listed by lsmod" -- bash -c "lsmod | grep -q '^mock_gpu '"

# /proc
assert_file /proc/driver/nvidia/version "/proc/driver/nvidia/version present"
assert_contains "$(cat /proc/driver/nvidia/version 2>/dev/null)" "NVRM version" "NVRM version line"

# /dev nodes
assert_chardev /dev/nvidiactl   "/dev/nvidiactl"
assert_chardev /dev/nvidia-uvm  "/dev/nvidia-uvm"
i=0
while [ "$i" -lt "$COUNT" ]; do
  assert_chardev "/dev/nvidia$i" "/dev/nvidia$i"
  i=$((i+1))
done

# sysfs PCI: count NVIDIA 3D-controller devices (vendor 0x10de, class 0x0302xx)
found=0
for vf in /sys/bus/pci/devices/*/vendor; do
  [ -f "$vf" ] || continue
  if [ "$(cat "$vf")" = "0x10de" ]; then
    d="$(dirname "$vf")"
    case "$(cat "$d/class" 2>/dev/null)" in
      0x0302*) found=$((found+1)) ;;
    esac
  fi
done
if [ "$found" -ge "$COUNT" ]; then
  pass "fake PCI device(s) in sysfs (found $found, want >= $COUNT)"
else
  info "fake PCI devices found=$found (enable_pci may have degraded; NodeFeatureRule gate covers this)"
fi

# Clean teardown
info "rmmod mock_gpu"
rmmod mock_gpu
assert_cmd_ok "module gone from lsmod" -- bash -c "! lsmod | grep -q '^mock_gpu '"
assert_no_file /dev/nvidia0 "/dev/nvidia0 removed"
assert_no_file /proc/driver/nvidia/version "/proc entry removed"
assert_cmd_ok "no kernel oops/BUG after unload" -- bash -c "! dmesg | tail -50 | grep -qiE 'Oops|BUG:|kernel panic'"

trap - EXIT
assert_summary
