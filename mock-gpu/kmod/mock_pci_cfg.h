/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mock_pci_cfg.h - Pure PCI config-space generation for the mock GPU.
 *
 * This logic is deliberately kept free of kernel API calls so it can be
 * compiled and unit-tested in userspace (tests/unit/kmod/test_config_space.c)
 * AND linked into the kernel module (mock_gpu.c). It only fabricates the bytes
 * of a 256-byte PCI configuration space; it performs no I/O.
 */
#ifndef MOCK_PCI_CFG_H
#define MOCK_PCI_CFG_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#define MOCK_CFG_SPACE_SIZE 256

/* Identity of one fabricated PCI function. */
struct mock_pci_ids {
	u16 vendor;
	u16 device;
	u16 subsys_vendor;
	u16 subsys_device;
	u8  revision;
	u8  class_prog;   /* programming interface (offset 0x09) */
	u8  class_sub;    /* sub-class             (offset 0x0a) */
	u8  class_base;   /* base class            (offset 0x0b) */
};

/*
 * Initialise a 256-byte config-space buffer to represent an inert NVIDIA GPU:
 * correct vendor/device/class/subsystem IDs, NO BARs, NO capability list,
 * NO interrupt pin. The result is intentionally minimal so the PCI core never
 * tries to assign resources, walk capabilities, or wire up interrupts.
 *
 * @cfg must point to at least MOCK_CFG_SPACE_SIZE bytes.
 */
void mock_cfg_init(u8 *cfg, const struct mock_pci_ids *ids);

/*
 * Read @size (1, 2 or 4) bytes from config space at byte offset @where,
 * little-endian assembled. Reads fully outside the 256-byte space return
 * all-ones (the PCI convention for "not present"/unimplemented).
 */
u32 mock_cfg_read(const u8 *cfg, int where, int size);

/*
 * Write @size (1, 2 or 4) bytes to config space at @where. Only a small set of
 * genuinely writable registers persist (Command, Cache Line Size, Latency
 * Timer, Interrupt Line); all identity registers and BARs are read-only, so BAR
 * sizing probes read back 0 (unimplemented) and the device identity is immutable.
 */
void mock_cfg_write(u8 *cfg, int where, int size, u32 val);

#endif /* MOCK_PCI_CFG_H */
