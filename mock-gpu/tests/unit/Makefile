# Unit tests (TESTING.md §1). No kernel, no cluster, no privilege.
# Run from anywhere: `make -C mock-gpu/tests/unit` or via top-level `make test-unit`.

include ../../versions.mk

KMOD_DIR := ../../kmod
NVML_DIR := ../../nvml
OUT      := _out

CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -Werror -std=gnu11

# Identity defines shared with versions.mk (single source of truth, PLAN §9).
ID_DEFS := \
	-DNV_VENDOR_ID=$(NV_VENDOR_ID) \
	-DNV_DEVICE_ID=$(NV_DEVICE_ID) \
	-DNV_SUBSYS_VENDOR=$(NV_SUBSYS_VENDOR) \
	-DNV_SUBSYS_DEVICE=$(NV_SUBSYS_DEVICE) \
	-DNV_REVISION=$(NV_REVISION) \
	-DNV_CLASS_PROG=$(NV_CLASS_PROG) \
	-DNV_CLASS_SUB=$(NV_CLASS_SUB) \
	-DNV_CLASS_BASE=$(NV_CLASS_BASE)

# NVML_DEFS is defined centrally in versions.mk (shared by the .so and this test).

.PHONY: all config-space nvml shell clean
all: config-space

# --- kernel pure-helper config-space test (TESTING §1.2) -------------------
config-space: $(OUT)/test_config_space
	@echo ">> running config-space unit test"
	@$(OUT)/test_config_space

$(OUT)/test_config_space: kmod/test_config_space.c $(KMOD_DIR)/mock_pci_cfg.c $(KMOD_DIR)/mock_pci_cfg.h | $(OUT)
	$(CC) $(CFLAGS) $(ID_DEFS) -I$(KMOD_DIR) -o $@ kmod/test_config_space.c $(KMOD_DIR)/mock_pci_cfg.c

# --- mock NVML symbol test (TESTING §1.1) ---------------------------------
nvml: $(OUT)/test_nvml
	@echo ">> running mock NVML unit test"
	@$(OUT)/test_nvml

$(OUT)/test_nvml: nvml/test_nvml.c $(NVML_DIR)/nvml_mock.c $(NVML_DIR)/nvml_mock.h | $(OUT)
	$(CC) $(CFLAGS) $(NVML_DEFS) -I$(NVML_DIR) -o $@ nvml/test_nvml.c $(NVML_DIR)/nvml_mock.c

# --- shell unit tests via bats (TESTING §1.3) -----------------------------
shell:
	@if command -v bats >/dev/null 2>&1; then \
		bats shell/ ; \
	else \
		echo "bats not on PATH; running via container ($(BUILD_BASE_IMAGE))"; \
		docker run --rm -v "$(MOCK_GPU_DIR)":/src -w /src/tests/unit $(BUILD_BASE_IMAGE) \
			sh -c "apt-get update -qq && apt-get install -y -qq bats >/dev/null && bats shell/" ; \
	fi

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
