# Plan: Mock NVIDIA GPU Device for Testing the GPU Operator

> **Status:** Design complete, ready for implementation.
> **Audience:** A later Opus/Sonnet LLM (or human) executing this plan end-to-end.
> **Author intent:** Minimal, correct, maintainable test harness that makes the NVIDIA
> GPU Operator believe a mock NVIDIA GPU is present on a Linux host, with **no real GPU
> hardware and no functional CUDA**.
> **Execution method:** **Test-Driven Development (TDD) is mandatory** — see §1.1 and the
> companion guide [TESTING.md](TESTING.md).

---

## 1. Goal & Success Criteria

Build a **mock NVIDIA GPU** that the NVIDIA GPU Operator recognizes as a valid, supported
GPU, so the full operator stack reaches a healthy ("green") state on a node with no real GPU.

"Full operator green" here means, on the test node:

1. **NFD** labels the node `feature.node.kubernetes.io/pci-10de.present=true`
   (this is the operator's scheduling gate for GPU operands).
2. **GFD** emits `nvidia.com/gpu.product`, `nvidia.com/gpu.count`, `nvidia.com/gpu.memory`,
   `nvidia.com/gpu.family`, etc.
3. **device-plugin** advertises allocatable `nvidia.com/gpu: N` and a pod requesting
   `nvidia.com/gpu: 1` schedules and runs (it will **not** execute real GPU work).
4. All `gpu-operator`-namespace pods report `Ready`.

The GPU is intentionally **non-functional** as a GPU — it cannot run CUDA/GPU workloads.

---

## 1.1 Execution Methodology: Test-Driven Development (MANDATORY)

**Execute this entire plan using Test-Driven Development.** The companion file
[TESTING.md](TESTING.md) is the authoritative specification of *what* to test at each level
(unit → functional → E2E) and *how*; this plan describes *what to build*. Read both together.
For every component in §5 and every build/deploy step:

1. **Red — write the test first.** Before writing a component's implementation, write the
   corresponding **unit test(s)** from TESTING §1 (e.g. the mock-NVML symbol assertions before
   `nvml_mock.c`; the PCI config-space assertions before extracting/writing the kernel helper;
   the bats cases before the entrypoint/install/shim scripts). The test must fail (or not
   compile) initially.
2. **Green — implement the minimum** to make that test pass. Nothing more.
3. **Refactor** with the test as a safety net. Structure code *for testability* as the tests
   demand — notably the PLAN directives to extract the kernel module's pure PCI-config logic
   into a userspace-compilable helper (§5.1 / TESTING §1.2) and to put shell logic in sourceable
   functions guarded by `[[ "${BASH_SOURCE[0]}" == "${0}" ]]` (§5.5 / TESTING §1.3).
4. **Layer outward.** Promote each component up the test pyramid as soon as it exists: pass its
   unit tests, then its **functional** test (TESTING §2 — run it for real), and only wire it into
   the **E2E** flow (TESTING §3) once it is functionally green in isolation.
5. **E2E last, but assertions first.** When building the deploy artifacts (§6, §11, §12), write
   the E2E assertions (TESTING §3.1–3.5) before/as you wire `make up`, so "done" is defined by a
   passing E2E run, not by pods merely existing.

**Gating rules:** do not advance to the next component until the current one's unit + functional
tests pass. Do not declare the system complete until `make test-unit`, `make test-functional`,
and `make test-e2e` all pass (TESTING §0). Follow the build/test order in TESTING §6, which is
sequenced to support this TDD flow. Every expected value asserted in a test and every value
produced by the implementation must come from the **same** `versions.mk` entry (§9) — the test
and the code share one source of truth.

---

## 2. Key Constraints (from requirements gathering)

These were explicitly decided with the requester; **do not silently revisit them**:

- **Host OS:** Always **Ubuntu 24.04**. Captured as a `versions.mk` variable.
- **Kernel target:** The mock module is loaded into the **current host kernel** (the same
  machine that runs `kind`). The requester accepted the host-kernel-pollution/crash risk of
  fake-PCI registration. **Strongly recommend taking a filesystem/VM snapshot before testing.**
- **Detection depth:** **Full operator green** (NFD + GFD + device-plugin allocatable + a
  schedulable test pod), with the caveats in §3.
- **Kernel module build:** **Fully containerized.** A builder container (Ubuntu 24.04 base)
  is bootstrapped to fetch/install the matching kernel headers + toolchain from the Ubuntu
  package archive, then the module is compiled inside the container against a bind-mounted
  source tree. **No build happens on the node at runtime** (prebuilt `.ko` is packaged).
- **NVML strategy:** Use a small **userspace mock `libnvidia-ml.so.1`** (see §5.2). Do **not**
  attempt to fool the *real* NVML via the kernel ioctl ABI for v1 (documented as a future
  high-fidelity mode in §10).
- **Injection / packaging:** Package the mock kernel module **and** mock NVML as a **single
  container image** supplied to the operator as a **custom driver image**
  (`driver.repository`/`driver.image`/`driver.version`). The operator runs it as its
  privileged driver DaemonSet, and the **NVIDIA Container Toolkit injects the mock NVML into
  GFD/device-plugin natively** (no Kyverno/webhook in the primary design).
- **NFD gate bootstrap:** Use **Option (b) — ungated bootstrap DaemonSet** (see §6).
- **Build system:** GNU Make. **All external versions/IDs live centrally in `versions.mk`.**
- **Tooling assumed already installed on host:** `kind`, `docker`, `helm`, `kubectl`, and a
  working Docker daemon. Do **not** add Makefile rules to install these. (Kernel headers and
  the C toolchain are NOT assumed on the host — they are installed *inside* the builder
  container by the `bootstrap` target.)

---

## 3. What Cannot Pass, and How We Neutralize It

"Full operator green" with no real hardware is achievable only if the components that execute
real hardware/CUDA are disabled or stubbed. The implementer **must** apply all of these:

| Component | Why it fails on a mock | Action |
|---|---|---|
| `driver` (operator's own) | We supply our own mock driver image | Point `driver.image` at our image (we **are** the driver) |
| `dcgm`, `dcgmExporter` | Talk to real driver/DCGM | `dcgm.enabled=false`, `dcgmExporter.enabled=false` |
| `migManager` | MIG on real HW | `migManager.enabled=false` (mock is Tesla T4 = no MIG) |
| `nodeStatusExporter`, `gdrcopy`, `nvidiaDriverCRD`/`nvidia-fs`, `vfioManager`, `sandboxDevicePlugin`, `vgpuManager`, `vgpuDeviceManager`, `ccManager` | Real HW / not needed | disabled |
| **cuda-validator** (part of operator-validator) | Runs a real CUDA `vectorAdd` workload | **Replace its image** with a no-op shim that exits 0 (see §5.6) |
| `toolkit` | — | **Enabled** (`toolkit.enabled=true`) — required for NVML injection |

> Surface in the final report any operator-version-specific knob whose name differs from the
> above; the exact field paths must be verified against the **pinned** operator version.

---

## 4. Architecture Overview

```
                         ┌─────────────────────────────────────────────┐
                         │              kind cluster (host kernel)        │
                         │                                                │
  (b) ungated bootstrap  │  ┌──────────────────────────┐                  │
  DaemonSet (our image,  │  │ bootstrap pod (mode=boot) │ insmod mock_gpu.ko
  mode=bootstrap)        │  │  - load fake-PCI module   │──────────────┐   │
  NOT gated by NFD label │  └──────────────────────────┘              ▼   │
                         │                                   /sys fake PCI │
                         │   NFD sees vendor 0x10DE ─────►  pci-10de.present=true
                         │                                          │       │
                         │   operator schedules operands ◄──────────┘       │
                         │                                                  │
                         │  ┌──────────────────────────┐                    │
  operator driver        │  │ driver pod (mode=driver)  │ populate /run/nvidia/driver
  DaemonSet (OUR image,  │  │  - mock libnvidia-ml.so.1 │  (+ stub libs, nvidia-smi shim,
  mode=driver)           │  │  - /dev/nvidia*           │   ld.so.cache, readiness files)
                         │  └──────────────────────────┘                    │
                         │            │ toolkit (libnvidia-container)        │
                         │            ▼ injects mock libnvidia-ml.so.1       │
                         │  ┌─────────┐   ┌──────────────┐                   │
                         │  │   GFD   │   │ device-plugin│  → nvidia.com/gpu  │
                         │  └─────────┘   └──────────────┘                   │
                         └─────────────────────────────────────────────────┘
```

**One image, two modes** (selected by entrypoint arg / env `MODE`):
- `MODE=bootstrap` → ungated DaemonSet we deploy: loads the kernel module so NFD detects the
  fake PCI device *before* the operator gate is evaluated.
- `MODE=driver` → run by the operator as its driver DaemonSet: ensures the module is loaded
  (idempotent), lays down the mock driver root, writes readiness sentinels, stays alive.

The kernel module is loaded exactly once (bootstrap mode loads it; driver mode is idempotent
and skips if already present).

---

## 5. Components

### 5.1 Mock kernel module — `kmod/mock_gpu.c` (C)

Responsibilities:

1. **Fake PCI device (for NFD).** Create a synthetic PCI root bus and device whose config
   space reports: vendor `0x10DE`, the chosen device ID (`NV_DEVICE_ID`), class `0x030000`
   (VGA/3D controller), and subsystem IDs. Approach: `pci_create_root_bus()` with a custom
   `struct pci_ops` whose `.read` returns the canned config space and `.write` is a no-op,
   then `pci_scan_single_device()` / `pci_scan_child_bus()` so the device appears under
   `/sys/bus/pci/devices/`. NFD's PCI source reads `vendor`/`device`/`class` from there.
   - **This is the highest-risk code** on a live host kernel; it is kernel-version-sensitive.
     Guard with thorough error handling; fail cleanly (don't panic) on any API mismatch.
   - Make the number of mock GPUs a module parameter (`count`, default from `MOCK_GPU_COUNT`).
2. **Char devices.** Create `/dev/nvidia0..N-1`, `/dev/nvidiactl`, `/dev/nvidia-uvm` so
   `open()` and device-plugin `Allocate` succeed and libnvidia-container finds them.
3. **`/proc/driver/nvidia/version`.** Write a plausible version string (`NV_DRIVER_VERSION`)
   so libnvidia-container detects a "driver."
4. Clean teardown on module unload (remove devices, proc entry, fake bus).

Build: standard kbuild out-of-tree module (`make -C /lib/modules/$(KERNEL_VERSION)/build
M=$(PWD) modules`), executed **inside the builder container** (§7).

### 5.2 Mock NVML library — `nvml/nvml_mock.c` → `libnvidia-ml.so.1` (C)

A userspace shared object that GFD and the device-plugin `dlopen` by soname. Build with
`-shared -fPIC -Wl,-soname,libnvidia-ml.so.1`.

Implement **only** the subset of NVML symbols GFD + device-plugin call. Known-needed set
(verify against pinned operator/go-nvml version, add as needed):

- `nvmlInit_v2`, `nvmlInitWithFlags`, `nvmlShutdown`
- `nvmlSystemGetDriverVersion`, `nvmlSystemGetNVMLVersion`, `nvmlSystemGetCudaDriverVersion`,
  `nvmlSystemGetCudaDriverVersion_v2`
- `nvmlDeviceGetCount_v2`, `nvmlDeviceGetHandleByIndex_v2`, `nvmlDeviceGetHandleByUUID`
- `nvmlDeviceGetName`, `nvmlDeviceGetUUID`, `nvmlDeviceGetMinorNumber`
- `nvmlDeviceGetMemoryInfo`, `nvmlDeviceGetMemoryInfo_v2`
- `nvmlDeviceGetPciInfo_v3`
- `nvmlDeviceGetCudaComputeCapability`, `nvmlDeviceGetArchitecture`, `nvmlDeviceGetBrand`
- `nvmlDeviceGetMigMode` → return `NVML_DEVICE_MIG_DISABLE` / `NVML_ERROR_NOT_SUPPORTED`
- `nvmlErrorString`

**Directives:**
- All returned values come from compile-time constants generated from `versions.mk`
  (product name, memory, compute capability, arch, UUID scheme, PCI bus IDs that match the
  fake PCI device).
- For **any symbol not implemented**, the default behavior must be a safe stub returning
  `NVML_ERROR_NOT_SUPPORTED` (a value GFD/device-plugin tolerate) — never crash. Consider a
  weak-symbol/catch-all pattern so a newer GFD calling an unknown symbol degrades gracefully.
- Event APIs (`nvmlEventSetCreate`, `nvmlDeviceRegisterEvents`, …) → return
  `NVML_ERROR_NOT_SUPPORTED` so the device-plugin disables health monitoring instead of
  failing.
- UUIDs must be stable and unique per device index and formatted `GPU-xxxxxxxx-...`.

### 5.3 Stub driver libraries — `driver-root/`

`libnvidia-container` injects a set of driver libraries into operand containers based on the
driver root's `ld.so.cache`. Provide minimal valid ELF shared-object **stubs** (correct
sonames, empty/no-op) for the libs it expects beyond NVML — at minimum `libcuda.so.1`; add
others only if injection logs show them required. Generate `ld.so.cache` via `ldconfig -r`
over the driver root.

### 5.4 `nvidia-smi` shim — `driver-root/nvidia-smi.sh`

A small script printing plausible `nvidia-smi` output (product, driver version, 0% util) and
exiting 0. Required because the operator's `driver-validation` / `toolkit-validation` init
containers `chroot` into the driver root and run `nvidia-smi`.

### 5.5 Single container image — `image/Dockerfile` (+ `image/entrypoint.sh`)

- Base: minimal Ubuntu 24.04 runtime (or `BUILD_BASE_IMAGE`).
- COPY in the **prebuilt** `mock_gpu.ko`, `libnvidia-ml.so.1`, stub libs, `nvidia-smi` shim.
- `entrypoint.sh` dispatches on `MODE`:
  - `bootstrap`: verify `uname -r` matches the `.ko` vermagic (fail loudly otherwise);
    `insmod` the module if not already loaded; sleep/hold; on `SIGTERM` leave the module
    loaded (driver mode may still need it) — OR `rmmod` only if it loaded it. Document the
    chosen ownership rule clearly.
  - `driver`: ensure module loaded (idempotent); populate `/run/nvidia/driver/...` (mock NVML,
    stub libs, `nvidia-smi` shim, `ld.so.cache`); create `/run/nvidia/validations/*-ready`
    sentinels; hold; on `SIGTERM`, clean up driver root and `rmmod`.
- Must run privileged with host `/run/nvidia` (bidirectional mount propagation), `/dev`,
  `/sys`, `/lib/modules`, and host PID as the operator provides for driver pods.

### 5.6 No-op cuda-validator image — `deploy/cuda-validator-shim/Dockerfile`

Tiny image whose entrypoint exits 0, used to override the operator's cuda-validator so the
operator-validator passes without running real CUDA. Wire via the operator's validator image
override (verify exact field for the pinned operator version).

---

## 6. NFD Gate Bootstrap — Option (b), ungated bootstrap DaemonSet

**Problem:** The operator gates its driver DaemonSet (and all GPU operands) on the NFD label
`feature.node.kubernetes.io/pci-10de.present=true`. That label only appears once NFD sees the
fake PCI device — which our module creates. But the operator won't schedule our driver image
until the label exists. Deadlock.

**Resolution (chosen): Option (b).** Deploy our **same image** as a small, **ungated**,
all-tolerating privileged DaemonSet in `MODE=bootstrap`. Its only job is to load
`mock_gpu.ko` early so the fake PCI device appears in `/sys` and **NFD detects it natively**.
NFD then sets `pci-10de.present=true`, after which the operator schedules its driver DaemonSet
(our image, `MODE=driver`) and the rest of the operands.

Implementation notes:
- The bootstrap DaemonSet is applied by us (`make deploy-mock`) **before** `helm install` of
  the operator (or concurrently; the operator will simply wait for the gate).
- It must tolerate all taints and have **no** nodeSelector on the NFD label.
- Loading the module in bootstrap mode and having the driver pod treat it as already-present
  avoids double-load races; make module load idempotent and serialize via a lock file under
  `/run/nvidia` if necessary.

---

## 7. Containerized Build Pipeline (Ubuntu 24.04)

`bootstrap` builds an Ubuntu 24.04 builder image with the **host kernel's** headers +
toolchain fetched from the Ubuntu archive; build targets then compile inside it against the
bind-mounted source tree.

`image/Dockerfile.builder`:

```dockerfile
ARG BUILD_BASE_IMAGE=ubuntu:24.04
FROM ${BUILD_BASE_IMAGE}
ARG KERNEL_VERSION
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential bc flex bison libelf-dev kmod \
      linux-headers-${KERNEL_VERSION} \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
```

**Caveats the implementer must handle / surface:**
- `linux-headers-$(uname -r)` must exist in the configured apt archive. True for stock 24.04
  GA/HWE `-generic` kernels; a custom/mainline kernel will not have a matching package —
  detect and fail with a clear message (offer `APT_MIRROR` override).
- The builder's gcc must match the kernel's build compiler (stock 24.04 kernels → gcc-13 via
  `build-essential`).
- Because host == build machine == kind node host, the `.ko` built against `$(uname -r)`
  matches the running kernel. The entrypoint still asserts the vermagic match at load time.

---

## 8. Repository Layout

```
mock-gpu/
  versions.mk                       # SINGLE SOURCE OF TRUTH for all versions/IDs
  Makefile                          # top-level; `include versions.mk`
  docs/PLAN.md                      # this file
  kmod/
    mock_gpu.c
    Makefile                        # kbuild wrapper
  nvml/
    nvml_mock.c
    nvml_stubs.h                    # generated constants from versions.mk (or via -D flags)
    Makefile
  driver-root/
    nvidia-smi.sh
    stubs/                          # stub .so sources (e.g. libcuda)
    install.sh                      # populate /run/nvidia/driver + ldconfig
  image/
    Dockerfile.builder             # bootstrap: toolchain + matching kernel headers
    Dockerfile                     # single runtime image (packages ./build artifacts)
    entrypoint.sh                  # MODE=bootstrap | driver
  deploy/
    bootstrap-daemonset.yaml       # ungated MODE=bootstrap DaemonSet (Option b)
    clusterpolicy-values.yaml      # operator Helm overrides (driver.image, disables, etc.)
    cuda-validator-shim/Dockerfile
    kyverno-injection.yaml         # FALLBACK ONLY (documented, not primary)
  kind/
    kind-config.yaml
  build/                            # .ko + .so build outputs (gitignored)
```

---

## 9. `versions.mk` — Centralized Configuration

All externally-versioned or identity values live here. Pin exact versions before implementing.

```make
# --- Host / build assumptions ---
HOST_OS            ?= ubuntu
HOST_OS_VERSION    ?= 24.04
BUILD_BASE_IMAGE   ?= ubuntu:24.04
KERNEL_VERSION     ?= $(shell uname -r)          # host kernel, detected; overridable
APT_MIRROR         ?=                             # optional apt archive override

# --- Kubernetes / kind ---
KIND_NODE_IMAGE    ?= kindest/node:v1.31.X        # pin exact tag/sha

# --- GPU Operator (pin exact) ---
GPU_OPERATOR_REPO_URL ?= https://helm.ngc.nvidia.com/nvidia
GPU_OPERATOR_CHART    ?= gpu-operator
GPU_OPERATOR_VERSION  ?= vXX.Y.Z
GPU_OPERATOR_NS       ?= gpu-operator

# --- Kyverno (FALLBACK injection path only; pin if used) ---
KYVERNO_CHART_VERSION ?= X.Y.Z

# --- Mock GPU identity (Tesla T4 = simple, no MIG) ---
NV_VENDOR_ID       ?= 0x10de
NV_DEVICE_ID       ?= 0x1eb8
NV_SUBSYS_VENDOR   ?= 0x10de
NV_SUBSYS_DEVICE   ?= 0x12a2
NV_PRODUCT_NAME    ?= Tesla T4
NV_MEMORY_MB       ?= 16384
NV_COMPUTE_CAP     ?= 7.5
NV_ARCH            ?= turing
MOCK_GPU_COUNT     ?= 1

# --- Reported software versions (must be self-consistent) ---
NV_DRIVER_VERSION  ?= 535.104.05
NV_CUDA_VERSION    ?= 12.2
NVML_VERSION       ?= 12.535.104.05

# --- Images ---
REGISTRY           ?= localhost
MOCK_IMAGE         ?= $(REGISTRY)/mock-gpu-driver
MOCK_IMAGE_TAG     ?= dev                          # MUST match operator-computed driver ref
BUILDER_IMAGE      ?= $(REGISTRY)/mock-gpu-builder:$(KERNEL_VERSION)
VALIDATOR_SHIM     ?= $(REGISTRY)/cuda-validator-noop:dev

# --- Operator driver-image wiring (verify tag scheme vs pinned operator version) ---
DRIVER_REPOSITORY      ?= $(REGISTRY)
DRIVER_IMAGE_NAME      ?= mock-gpu-driver
DRIVER_VERSION         ?= $(MOCK_IMAGE_TAG)
DRIVER_USE_PRECOMPILED ?= false

# --- Test-only dependencies (see docs/TESTING.md; pin exact) ---
NVIDIA_GO_NVML_VERSION ?= vX.Y.Z     # go-nvml used by the functional NVML test
BATS_VERSION           ?= X.Y.Z      # shell unit tests (run via container if absent)
TEST_TIMEOUT_SECS      ?= 600        # E2E rollout/label wait budget
```

> **Directive:** Anything an implementer might be tempted to hardcode (a version, an image
> ref, a device ID, the operator namespace) must be referenced from `versions.mk`.

> **CRITICAL version linkage — `NVIDIA_GO_NVML_VERSION` ⇄ GFD:** The functional NVML test
> (TESTING §2.2) exercises the mock `libnvidia-ml.so.1` through `github.com/NVIDIA/go-nvml`,
> which is the **same** binding GFD and the device-plugin use. For the test to faithfully
> mirror what GFD does at runtime, `NVIDIA_GO_NVML_VERSION` **must be pinned to the exact
> go-nvml version vendored by the `gfd`/`device-plugin` images shipped with the pinned
> `GPU_OPERATOR_VERSION`**. When `GPU_OPERATOR_VERSION` changes, re-derive `NVIDIA_GO_NVML_VERSION`
> from that release's GFD `go.mod` and re-run the functional + E2E NVML tests — a drift here is
> exactly how a newly-required NVML symbol (PLAN §5.2 catch-all) would otherwise slip through.

---

## 10. Makefile Targets

```
make bootstrap        # build Ubuntu 24.04 builder image w/ host-kernel headers + toolchain
make kmod             # compile mock_gpu.ko inside builder → build/   (depends: bootstrap)
make nvml             # compile libnvidia-ml.so.1 inside builder → build/
make stubs            # compile stub driver libs → build/
make artifacts        # kmod + nvml + stubs
make image            # build single mock-gpu-driver image (packages build/ artifacts)
make validator-shim   # build no-op cuda-validator image
make kind-up          # create kind cluster from kind/kind-config.yaml; load local images
make kind-down        # delete kind cluster
make deploy-mock      # apply ungated bootstrap DaemonSet (Option b)
make deploy-operator  # helm install GPU Operator with deploy/clusterpolicy-values.yaml
make verify           # assert node labels + nvidia.com/gpu allocatable + all pods Ready
make test-pod         # schedule a pod requesting nvidia.com/gpu:1 and confirm it runs
make up               # kind-up → artifacts → image → validator-shim → kind load →
                      #   deploy-mock → deploy-operator → verify
make down             # kind-down
make clean            # remove build/ artifacts and local images
```

Build commands run **inside** the builder via bind mount, e.g.:

```make
kmod: bootstrap
	docker run --rm -v $(PWD):/src $(BUILDER_IMAGE) \
	  make -C /src/kmod KERNEL_VERSION=$(KERNEL_VERSION)
```

---

## 11. Operator Helm Values (`deploy/clusterpolicy-values.yaml`)

Driven by `versions.mk`. Must set (verify exact paths against pinned operator version):

- `driver.enabled=true`, `driver.repository`, `driver.image`, `driver.version`,
  `driver.usePrecompiled` → point at our single mock image.
- `toolkit.enabled=true` (required for NVML injection).
- `devicePlugin.enabled=true`, `gfd.enabled=true`, `nfd.enabled=true`.
- `dcgm.enabled=false`, `dcgmExporter.enabled=false`, `migManager.enabled=false`,
  `nodeStatusExporter.enabled=false`, `gdrcopy.enabled=false`, plus other sandbox/vGPU/cc
  components disabled (§3).
- `validator` cuda check → no-op shim image (`VALIDATOR_SHIM`).
- `mig.strategy=none`.

---

## 12. kind Configuration (`kind/kind-config.yaml`)

- Single-node cluster using `KIND_NODE_IMAGE`.
- The kind node is a container sharing the **host kernel**; the module loaded by the bootstrap
  pod affects the host kernel and host `/sys` (accepted constraint).
- Ensure containerd in the node is reconfigurable by the operator's toolkit (kind uses
  containerd by default — OK).
- Mount host paths as needed for the driver/bootstrap pods (the operator provides most driver
  mounts; the bootstrap DaemonSet manifest must request `/lib/modules`, `/usr/src`, `/dev`,
  `/sys`, `/run/nvidia` with appropriate propagation, privileged: true).

---

## 13. End-to-End Flow

`make up`:
1. `kind-up` — create cluster, load locally-built images into kind.
2. Build `artifacts` → `image` (single mock-gpu-driver) → `validator-shim`.
3. `deploy-mock` — apply ungated bootstrap DaemonSet (`MODE=bootstrap`) → loads `mock_gpu.ko`
   → fake PCI device in `/sys`.
4. NFD detects vendor `0x10DE` → sets `pci-10de.present=true`.
5. `deploy-operator` — `helm install` operator with our values. Operator gate now satisfied →
   schedules driver DaemonSet (our image, `MODE=driver`) → populates `/run/nvidia/driver` with
   mock NVML + stubs + nvidia-smi shim + readiness files.
6. Toolkit injects mock `libnvidia-ml.so.1` into GFD + device-plugin → GFD labels node;
   device-plugin advertises `nvidia.com/gpu`.
7. `verify` + `test-pod` confirm success.

---

## 14. Validation Checklist (`make verify`)

- Node has `feature.node.kubernetes.io/pci-10de.present=true`.
- Node has `nvidia.com/gpu.product` (≈ `Tesla-T4`) and `nvidia.com/gpu.count`.
- `nvidia.com/gpu` allocatable == `MOCK_GPU_COUNT`.
- All pods in `GPU_OPERATOR_NS` are `Ready` (validator included, via shim).
- `make test-pod`: a pod requesting `nvidia.com/gpu: 1` reaches `Running`/`Completed`.

---

## 15. Risks & Open Items (resolve during implementation)

1. **Fake-PCI in a live host kernel is the highest-risk code.** `pci_create_root_bus` +
   custom `pci_ops` is kernel-version-sensitive. Test on a snapshot/disposable environment
   first. If it proves unstable on the target kernel, fall back to seeding the NFD gate via a
   NodeFeatureRule (documented fallback) and keep the PCI piece behind a build flag.
2. **Operator driver-image contract varies by version.** The operator computes the driver
   image reference (plain `:${version}` vs precompiled `:${version}-${kernel}-${os}`) and runs
   `k8s-driver-manager` init logic + readiness handshakes that differ across releases. Pin the
   operator version; verify the computed image ref matches `MOCK_IMAGE`/tag; replicate the
   readiness-sentinel contract the operands' `driver-validation` init container expects.
3. **cuda-validator override knob** differs across operator versions — verify the exact field.
4. **NVML symbol set** is go-nvml/version-dependent — implement the safe `NOT_SUPPORTED`
   catch-all so newer GFD versions degrade instead of crashing; add symbols as logs reveal.
5. **libnvidia-container injection list** — provide stub `.so`s for every lib it insists on;
   determine the exact set from its injection logs.
6. **kind kernel-headers availability** — `linux-headers-$(uname -r)` must be installable in
   the builder; fail clearly if not (custom/mainline kernels).

---

## 16. Documented Alternatives (not chosen for v1)

- **Kyverno mutating-policy NVML injection** (`deploy/kyverno-injection.yaml`): injects the
  mock `.so` + `LD_LIBRARY_PATH` into GFD/device-plugin pods by label, **without** the toolkit
  and **without** presenting as a driver image. Keep as the fallback if the driver-image
  contract proves too version-fragile. Trade-off: avoids toolkit, but adds a Kyverno
  dependency and must enumerate operands by label.
- **High-fidelity kernel-ABI mode:** have the kernel module implement the proprietary NVIDIA
  RM ioctl ABI (visible via `open-gpu-kernel-modules`) so the *real* `libnvidia-ml.so` and
  `nvidia-smi` work. Far larger and version-locked; only pursue if real-userspace fidelity
  ever outranks effort.

---

## 17. Execution Order for the Implementing LLM

1. Create `versions.mk` with all values pinned (resolve every `X`/`vXX.Y.Z` placeholder first).
2. Implement `kmod/mock_gpu.c` (+ `kmod/Makefile`); get it building in the builder and loading
   on the host (fake PCI visible in `lspci`/`/sys`, `/dev/nvidia*` and `/proc` present).
3. Implement `nvml/` mock `.so` and `driver-root/` (stubs, nvidia-smi shim, install.sh).
4. Implement `image/` (builder + runtime Dockerfiles + entrypoint with both modes).
5. Implement `deploy/` (bootstrap DaemonSet, clusterpolicy values, cuda-validator shim).
6. Implement `kind/kind-config.yaml` and the top-level `Makefile`.
7. Bring up end-to-end with `make up`; iterate using `make verify` and pod/container logs.
8. Document any operator-version-specific deviations discovered, and update `versions.mk`.

**Always prefer minimality and correctness. Reference all versions/IDs from `versions.mk`.**
