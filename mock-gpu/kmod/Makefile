# kbuild for the mock GPU kernel module (PLAN §7).
#
# Built INSIDE the Ubuntu 24.04 builder container against the host kernel's
# headers, e.g.:  make -C /src/kmod modules KERNEL_VERSION=$(uname -r)
#
# Identity/config defines come from ../versions.mk (single source of truth).

# Absolute path to this kmod/ dir; works in both the standalone and kbuild phases.
MOCK_KMOD_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(MOCK_KMOD_DIR)../versions.mk

ifeq ($(KERNELRELEASE),)
# ---- standalone phase: invoke kbuild against the kernel build tree ----------
KERNEL_VERSION ?= $(shell uname -r)
KDIR           ?= /lib/modules/$(strip $(KERNEL_VERSION))/build

.PHONY: modules clean
modules:
	$(MAKE) -C $(KDIR) M=$(MOCK_KMOD_DIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MOCK_KMOD_DIR) clean

else
# ---- kbuild phase ----------------------------------------------------------
obj-m      := mock_gpu.o
mock_gpu-y := mock_gpu_main.o mock_pci_cfg.o

ccflags-y := \
	-DNV_VENDOR_ID=$(NV_VENDOR_ID) \
	-DNV_DEVICE_ID=$(NV_DEVICE_ID) \
	-DNV_SUBSYS_VENDOR=$(NV_SUBSYS_VENDOR) \
	-DNV_SUBSYS_DEVICE=$(NV_SUBSYS_DEVICE) \
	-DNV_REVISION=$(NV_REVISION) \
	-DNV_CLASS_PROG=$(NV_CLASS_PROG) \
	-DNV_CLASS_SUB=$(NV_CLASS_SUB) \
	-DNV_CLASS_BASE=$(NV_CLASS_BASE) \
	-DNV_DRIVER_VERSION=\"$(NV_DRIVER_VERSION)\" \
	-DMOCK_GPU_COUNT=$(MOCK_GPU_COUNT) \
	-DMOCK_ENABLE_PCI=$(MOCK_ENABLE_PCI) \
	-DMOCK_PCI_DOMAIN=$(MOCK_PCI_DOMAIN) \
	-DMOCK_PCI_BUSNR=$(MOCK_PCI_BUSNR)

endif
