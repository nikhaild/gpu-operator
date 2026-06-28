# Testing Plan: Mock NVIDIA GPU System

> **Status:** Design complete, ready for implementation.
> **Audience:** Sonnet model executing/implementing the tests.
> **Companion:** Implements verification for the system described in [PLAN.md](PLAN.md).
>   Section references like "PLAN §5.2" point into that file.
> **Golden rule:** All versions/IDs/expected values come from `versions.mk` (PLAN §9).
>   Tests must **never** hardcode a value that already lives there — import it via `-D`
>   defines, a generated header, or shell `include versions.mk`.

---

## 0. Test Strategy Overview

Three levels, each with a distinct focus:

| Level | Focus | Needs | Where it runs |
|---|---|---|---|
| **Unit** | Individual code functions, in isolation | C compiler, bats. **No** kernel load, no cluster, no privilege. | Any machine / CI / inside builder container |
| **Functional** | One built component running for real at runtime | Root + Ubuntu 24.04 host kernel (for the module), Docker | Target host / disposable VM |
| **E2E** | Whole mock-GPU system **with the real GPU Operator** | kind + host kernel + operator + Docker | Target host |

**Privilege boundary (critical):** Unit tests are safe everywhere. Functional and E2E tests
**load the mock module into the host kernel** (same accepted constraint as PLAN §2) and require
root — they must **detect and SKIP with a clear message** when run without root, on a non-Ubuntu
24.04 host, or when the `.ko` vermagic ≠ `uname -r`. Take a snapshot before running them.

### Coverage matrix (component → levels)

| Component (PLAN §5) | Unit | Functional | E2E |
|---|---|---|---|
| 5.1 Kernel module `mock_gpu.ko` | pure helpers only | load/inspect/unload | via operator |
| 5.2 Mock NVML `libnvidia-ml.so.1` | full symbol coverage | exercised via go-nvml | via GFD/device-plugin |
| 5.3 Stub driver libs | ELF validity | injection list | via toolkit |
| 5.4 `nvidia-smi` shim | output/exit | run | via validators |
| 5.5 Image entrypoint (2 modes) | bats (sourced fns) | run each mode in container | as bootstrap + driver DS |
| 5.6 cuda-validator shim | exit code | run | validator passes |
| Build pipeline / Makefile | target wiring smoke | builder produces artifacts | `make up` |

### Test tree & Make targets

```
mock-gpu/tests/
  lib/assert.sh              # shared bash assertions (assert_eq, assert_contains, retry/wait_for)
  unit/
    nvml/test_nvml.c         # C unit tests for mock NVML
    kmod/test_config_space.c # userspace tests for extracted pure PCI-config helpers
    shell/*.bats             # entrypoint.sh, install.sh, nvidia-smi.sh
    Makefile
  functional/
    kmod_load.sh
    nvml_runtime/            # Go program using NVIDIA/go-nvml (the lib GFD uses)
    image_modes.sh
    validator_shim.sh
  e2e/
    e2e_test.sh              # full operator bring-up + assertions + teardown
    manifests/test-pod.yaml
```

```
make test-unit         # all unit tests (no privilege)         — gates every PR
make test-functional   # component runtime tests (root)        — gated to target host
make test-e2e          # full operator E2E (root + kind)       — gated to target host
make test              # = test-unit (+ functional if root)
```

Add to `versions.mk` (test-only deps; pin exact):
```make
NVIDIA_GO_NVML_VERSION ?= vX.Y.Z     # MUST match the go-nvml the pinned GFD uses
BATS_VERSION           ?= X.Y.Z      # run via container if not on host
TEST_TIMEOUT_SECS      ?= 600        # E2E rollout/label wait budget
```

---

## 1. Unit Tests (code-function level)

Goal: assert each function returns the correct canned value / behaves correctly, with **no**
kernel, cluster, or privilege. Build and run **inside the builder container** (PLAN §7) so the
toolchain matches.

### 1.1 Mock NVML — `tests/unit/nvml/test_nvml.c`

`dlopen("build/libnvidia-ml.so.1", RTLD_NOW)` (this also validates there are no unresolved
symbols), then resolve and call each function. Expected values come from `versions.mk` via `-D`
or the same generated `nvml_stubs.h` the library uses.

Required cases:
- `nvmlInit_v2()` → `NVML_SUCCESS`; `nvmlShutdown()` → `NVML_SUCCESS`. Init/shutdown is
  re-entrant (call twice).
- `nvmlSystemGetDriverVersion` → `NV_DRIVER_VERSION`; `nvmlSystemGetNVMLVersion` →
  `NVML_VERSION`; `nvmlSystemGetCudaDriverVersion(_v2)` → integer encoding of `NV_CUDA_VERSION`
  (e.g. 12.2 → 12020).
- `nvmlDeviceGetCount_v2` → `MOCK_GPU_COUNT`.
- For **each** index `0..MOCK_GPU_COUNT-1`:
  - `nvmlDeviceGetHandleByIndex_v2` → `NVML_SUCCESS`, non-NULL handle.
  - `nvmlDeviceGetName` → `NV_PRODUCT_NAME` ("Tesla T4").
  - `nvmlDeviceGetUUID` → matches `^GPU-[0-9a-f-]{36}$` **and is unique** across indices.
  - `nvmlDeviceGetMemoryInfo(_v2)` → `.total == NV_MEMORY_MB * 1024 * 1024`, `.free <= .total`.
  - `nvmlDeviceGetCudaComputeCapability` → major/minor from `NV_COMPUTE_CAP` (7,5).
  - `nvmlDeviceGetArchitecture` → Turing enum; `nvmlDeviceGetBrand` → Tesla/NVIDIA enum.
  - `nvmlDeviceGetMinorNumber` → equals index.
  - `nvmlDeviceGetPciInfo_v3` → busId string consistent with the fake PCI device (vendor/device
    fields = `NV_VENDOR_ID`/`NV_DEVICE_ID`).
- `nvmlDeviceGetHandleByIndex_v2(MOCK_GPU_COUNT)` (out of range) → an error code, not crash.
- `nvmlDeviceGetMigMode` → `NVML_DEVICE_MIG_DISABLE` or `NVML_ERROR_NOT_SUPPORTED`.
- Event APIs (`nvmlEventSetCreate`, `nvmlDeviceRegisterEvents`) → `NVML_ERROR_NOT_SUPPORTED`.
- **Catch-all safety:** resolve a deliberately-unimplemented-but-real NVML symbol and confirm it
  returns `NVML_ERROR_NOT_SUPPORTED` (validates the graceful-degradation directive, PLAN §5.2).
- `nvmlErrorString(NVML_ERROR_NOT_SUPPORTED)` → non-NULL, non-empty.

Use a tiny assert/TAP harness (plain C `assert` + a counter, or `cmocka` if added to the
builder). Each case prints PASS/FAIL; non-zero exit on any failure.

### 1.2 Kernel module pure helpers — `tests/unit/kmod/test_config_space.c`

The kernel module is not unit-testable in-kernel cheaply, so **refactor the error-prone pure
logic out** (PLAN directive): the PCI config-space generator must live in its own source
(e.g. `kmod/mock_pci_cfg.c` + `.h`) using fixed-width types behind a small typedef shim so it
compiles **both** in-kernel and in a userspace test. (Optionally also add a KUnit suite for
in-kernel coverage; not required for v1.)

Test the config-space buffer the module hands back to `pci_ops.read`:
- offset `0x00` (u16) == `NV_VENDOR_ID` (0x10de).
- offset `0x02` (u16) == `NV_DEVICE_ID`.
- class code: `0x0B`==0x03 (display), full 24-bit class == `0x030000`.
- offset `0x2C`/`0x2E` == `NV_SUBSYS_VENDOR`/`NV_SUBSYS_DEVICE`.
- header type at `0x0E` sane; reads past the defined space return 0/0xff per chosen convention.
- a read with `len` 1/2/4 returns correctly-masked/aligned values.

### 1.3 Shell scripts — `tests/unit/shell/*.bats` (bats-core)

Write `entrypoint.sh`, `install.sh`, and `nvidia-smi.sh` so their logic is in **functions** that
can be sourced without executing side effects (guard the main body with
`[[ "${BASH_SOURCE[0]}" == "${0}" ]]`). Put `insmod`/`rmmod`/`lsmod`/`ldconfig` behind PATH
shims the bats tests prepend, so nothing touches the real kernel.

- `entrypoint.bats`:
  - `MODE=bootstrap` dispatches to the bootstrap function; `MODE=driver` to the driver function;
    unknown MODE → non-zero exit + message.
  - vermagic check: stubbed `uname -r` ≠ baked vermagic → exits non-zero with a clear error.
  - idempotent load: stubbed `lsmod` already lists `mock_gpu` → `insmod` is **not** called.
- `install.bats`: run `install.sh` against a temp `DRIVER_ROOT`; assert `libnvidia-ml.so.1`,
  stub libs, and `nvidia-smi` land at expected paths; `ldconfig` invoked on that root;
  `/run/nvidia/validations/*-ready` sentinels created (paths parameterized).
- `nvidia_smi.bats`: shim exits 0 and output contains `NV_PRODUCT_NAME` and `NV_DRIVER_VERSION`.

> If `bats` is not installed on the host, run these in a small container that `apt-get install`s
> `bats` (pin `BATS_VERSION`), consistent with the containerized approach.

### 1.4 Build wiring smoke — part of `make test-unit`

Assert `make -n artifacts` resolves and that every `versions.mk` placeholder has been pinned
(grep for unresolved `X`/`vXX.Y.Z`/`v1.31.X` patterns → fail if found). Cheap guard against
shipping a half-configured `versions.mk`.

---

## 2. Functional Tests (component running at runtime)

Goal: run each **built** component for real and verify behavior. **Root + Ubuntu 24.04 host**
required for module-loading tests. Every script begins with a guard:

```sh
require_root; require_ubuntu 24.04; require_vermagic_match   # else: echo SKIP + exit 0
```

(SKIP, not FAIL, so CI without privilege stays green; the host run must show PASS, not SKIP.)

### 2.1 Kernel module load/inspect/unload — `tests/functional/kmod_load.sh`

1. `insmod build/mock_gpu.ko count=2` (use `MOCK_GPU_COUNT` and also test count>1).
2. Assert:
   - A PCI device with vendor `0x10de` and class `0x0300` is present under
     `/sys/bus/pci/devices/` (and visible via `lspci -d 10de:`).
   - `/dev/nvidia0`, `/dev/nvidia1`, `/dev/nvidiactl`, `/dev/nvidia-uvm` exist with char-dev type.
   - `/proc/driver/nvidia/version` exists and contains `NV_DRIVER_VERSION`.
   - `dmesg` since load shows **no** Oops/BUG/“tainting”-from-fault; module is listed by `lsmod`.
3. `rmmod mock_gpu`; assert the sysfs PCI entry, `/dev/nvidia*`, and `/proc/driver/nvidia` are
   **gone**, `lsmod` no longer lists it, and `dmesg` shows clean teardown (no crash).
4. Always attempt `rmmod` in a trap on exit so a failed assertion doesn't leave the host dirty.

### 2.2 Mock NVML via the real consumer library — `tests/functional/nvml_runtime/`

This is the highest-value functional test: it exercises the mock `.so` through
**`github.com/NVIDIA/go-nvml`** — the exact library GFD/device-plugin use — catching ABI/soname
mismatches a direct C test cannot.

- A small Go program (`main.go`, `go.mod` pinning `NVIDIA_GO_NVML_VERSION`) that:
  `nvml.Init()` → check `SUCCESS`; `DeviceGetCount()` → expect `MOCK_GPU_COUNT`; loop devices and
  read Name, UUID, MemoryInfo, CudaComputeCapability; print as JSON; `nvml.Shutdown()`.
- Run with `LD_LIBRARY_PATH=$(PWD)/build` (so go-nvml `dlopen`s the mock by soname).
- Assert the emitted JSON matches expected values from `versions.mk` (parse with `jq`).
- Build + run inside the builder container (Go assumed available per PLAN §2; otherwise add Go to
  the builder, pinned).

### 2.3 Container image modes — `tests/functional/image_modes.sh`

Build the single image (`make image`), then:
- **bootstrap mode:** `docker run --rm --privileged --pid=host -v /lib/modules:/lib/modules:ro
  -v /usr/src:/usr/src:ro -v /dev:/dev -v /sys:/sys <image> bootstrap` (or `MODE=bootstrap`),
  give it a moment, then from the host assert the module is loaded + fake PCI/`/dev`/`/proc`
  present (reuse 2.1 assertions). Stop the container; clean up the module.
- **driver mode:** run with `MODE=driver` and a tmp host dir bind-mounted at `/run/nvidia`
  (bidirectional propagation). Assert `/run/nvidia/driver/...` is populated (mock NVML, stub libs,
  `nvidia-smi`, generated `ld.so.cache`) and `/run/nvidia/validations/*-ready` sentinels exist.
  Send `SIGTERM`; assert cleanup per the chosen ownership rule (PLAN §5.5).
- Vermagic-mismatch path: if feasible, run on a deliberately wrong baked `.ko` and assert the
  entrypoint fails loudly (covers the runtime guard).

### 2.4 Stub libs & shims

- `tests/functional/validator_shim.sh`: `docker run` the cuda-validator shim → assert exit 0.
- Stub `.so` validity: `readelf -d build/<stub>.so` shows the expected `SONAME`; `nm -D` is empty
  or only the intended no-op symbols. (Can fold into unit 1.x if preferred.)

---

## 3. End-to-End Tests (full system + GPU Operator)

Goal: prove the **whole mock-GPU system makes the real GPU Operator green**, matching the PLAN
§14 validation checklist, plus negative and idempotency checks. Root + kind + Docker required;
SKIP guard as in §2. Driver is `tests/e2e/e2e_test.sh` (a Go test harness is an acceptable
alternative if the implementer prefers, but bash + kubectl + jq is the minimal path).

### 3.1 Happy-path bring-up & assertions

Sequence (each step waits with `TEST_TIMEOUT_SECS` budget; fail with diagnostics on timeout):
1. `make kind-up` (or assume a clean cluster); load locally-built images into kind.
2. `make deploy-mock` (ungated bootstrap DaemonSet, PLAN §6). Wait DS Ready.
3. **Assert the gate appears from native detection:** node gets
   `feature.node.kubernetes.io/pci-10de.present=true` (this proves the bootstrap→NFD path, the
   trickiest ordering in the design).
4. `make deploy-operator` (pinned `GPU_OPERATOR_VERSION`, values from PLAN §11). Wait for rollout.
5. **Assert all operands Ready:** driver DS (our image), toolkit, gfd, device-plugin,
   operator-validator. (Use `kubectl rollout status` / pod phase polling.)
6. **Assert GFD labels** on the node: `nvidia.com/gpu.product` (≈ `Tesla-T4`), `nvidia.com/gpu.count`,
   `nvidia.com/gpu.memory`, `nvidia.com/gpu.family`.
7. **Assert allocatable:** `.status.allocatable["nvidia.com/gpu"] == MOCK_GPU_COUNT`.
8. **Schedule a workload:** apply `manifests/test-pod.yaml` (requests `nvidia.com/gpu: 1`,
   command `true`/sleep). Assert it reaches `Running`/`Succeeded` **on the mock node** (it must
   not stay `Pending` for lack of the resource). It does **not** run CUDA.

### 3.2 Negative / configuration assertions

- Disabled components are **absent**: no `dcgm`, `dcgm-exporter`, `mig-manager`,
  `node-status-exporter` pods (PLAN §3).
- cuda-validator passed via the shim (operator-validator Ready, no real CUDA pod).
- A pod requesting `nvidia.com/gpu: <MOCK_GPU_COUNT + 1>` stays `Pending` (resource accounting is
  real).

### 3.3 Idempotency & resilience

- Delete the GFD pod (`kubectl delete pod -l app=gpu-feature-discovery`); assert it returns Ready
  and the node is **re-labeled** (proves toolkit injection re-applies on restart).
- Re-run `make deploy-operator` (helm upgrade no-op); assert no churn / still green.
- Restart the bootstrap pod; assert the module stays loaded (idempotent) and nothing breaks.

### 3.4 Multi-GPU variation

- Re-run the suite with `MOCK_GPU_COUNT=2` (and `count=2` module param) and assert allocatable
  and `nvidia.com/gpu.count` both reflect 2.

### 3.5 Teardown & cleanliness

- `helm uninstall` operator; `make deploy-mock` delete; assert the module is unloaded and host
  `/sys`/`/dev`/`/proc` mock state is gone (host returns to clean state).
- `make kind-down` (optional in CI; keep cluster for debugging on failure).

### 3.6 Operator-version matrix (optional, documented)

Loop the happy-path over a list of pinned `GPU_OPERATOR_VERSION` values. This is the primary
guard for the version-coupling risks in PLAN §15 (driver-image contract, validator knob, NVML
symbols). Keep the list in `versions.mk`; default to a single pinned version, expand as needed.

---

## 4. Diagnostics on Failure (all functional/E2E tests)

On any failure, dump before exiting non-zero (to a `tests/_artifacts/` dir):
- `kubectl get pods,ds -A -o wide`, `kubectl describe` for any non-Ready operand.
- Logs of driver DS (our image), gfd, device-plugin, operator-validator, toolkit.
- `kubectl get node -o yaml` (labels + allocatable), relevant events.
- Host `dmesg | tail`, `lsmod | grep mock`, `ls -l /sys/bus/pci/devices/*/ /dev/nvidia*`.
- For functional NVML: the go-nvml program's stderr.

These artifacts are what lets the next iteration diagnose version-specific deviations (PLAN §15).

---

## 5. CI Integration Notes

- **PR gate (any runner):** `make test-unit` only — fast, no privilege.
- **Nightly / host runner (Ubuntu 24.04, root, Docker, kind):** `make test-functional` then
  `make test-e2e`. Snapshot/disposable host recommended (host-kernel module load).
- Functional/E2E **SKIP** cleanly off-host; treat a SKIP in the host job as a failure (the host
  job must produce PASS). Surface SKIP-vs-PASS clearly in the summary.
- Emit TAP or JUnit if the CI consumes it; otherwise rely on exit codes + the §4 artifacts.

---

## 6. Implementation Order for Sonnet

1. `tests/lib/assert.sh` + the `versions.mk` test additions (§0).
2. Unit: NVML C tests (1.1), then refactor kernel pure helpers out and add config-space tests
   (1.2), then bats shell tests (1.3), then the build-wiring smoke (1.4). Wire `make test-unit`.
3. Functional: `kmod_load.sh` (2.1), `nvml_runtime` go-nvml harness (2.2), `image_modes.sh`
   (2.3), shim/stub checks (2.4). Wire `make test-functional` with SKIP guards.
4. E2E: `e2e_test.sh` happy path (3.1) → negatives (3.2) → idempotency (3.3) → multi-GPU (3.4)
   → teardown (3.5). Wire `make test-e2e`. Add the version matrix (3.6) last.
5. Add §4 diagnostics to every functional/E2E script.

**Keep tests minimal and deterministic. Import every expected value from `versions.mk`.
Functional/E2E tests must guard privilege/OS/vermagic and SKIP (not fail) when unmet.**
