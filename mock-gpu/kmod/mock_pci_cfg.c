// SPDX-License-Identifier: GPL-2.0
/*
 * mock_pci_cfg.c - Pure PCI config-space generation for the mock GPU.
 * No kernel API calls: see mock_pci_cfg.h for the rationale (userspace-testable).
 */
#include "mock_pci_cfg.h"

/* Standard PCI configuration-space register offsets we populate. */
#define CFG_VENDOR_ID     0x00
#define CFG_DEVICE_ID     0x02
#define CFG_COMMAND       0x04
#define CFG_STATUS        0x06
#define CFG_REVISION      0x08
#define CFG_CLASS_PROG    0x09
#define CFG_CLASS_SUB     0x0a
#define CFG_CLASS_BASE    0x0b
#define CFG_CACHE_LINE    0x0c
#define CFG_LATENCY       0x0d
#define CFG_HEADER_TYPE   0x0e
#define CFG_BAR0          0x10
#define CFG_BAR5          0x24
#define CFG_SUBSYS_VENDOR 0x2c
#define CFG_SUBSYS_DEVICE 0x2e
#define CFG_ROM_BAR       0x30
#define CFG_CAP_PTR       0x34
#define CFG_INT_LINE      0x3c
#define CFG_INT_PIN       0x3d

static void put8(u8 *cfg, int off, u8 v)
{
	cfg[off] = v;
}

static void put16(u8 *cfg, int off, u16 v)
{
	cfg[off]     = (u8)(v & 0xff);
	cfg[off + 1] = (u8)((v >> 8) & 0xff);
}

void mock_cfg_init(u8 *cfg, const struct mock_pci_ids *ids)
{
	int i;

	for (i = 0; i < MOCK_CFG_SPACE_SIZE; i++)
		cfg[i] = 0;

	put16(cfg, CFG_VENDOR_ID, ids->vendor);
	put16(cfg, CFG_DEVICE_ID, ids->device);

	/* Command = 0 (memory/IO/bus-master disabled; no BARs to enable anyway). */
	put16(cfg, CFG_COMMAND, 0x0000);
	/* Status = 0 -> CAP_LIST bit (0x10) clear, so the core walks no capabilities. */
	put16(cfg, CFG_STATUS, 0x0000);

	put8(cfg, CFG_REVISION,   ids->revision);
	put8(cfg, CFG_CLASS_PROG, ids->class_prog);
	put8(cfg, CFG_CLASS_SUB,  ids->class_sub);
	put8(cfg, CFG_CLASS_BASE, ids->class_base);

	/* Header type 0 (normal device), single function (bit 7 clear). */
	put8(cfg, CFG_HEADER_TYPE, 0x00);

	/* BARs 0x10-0x27 and ROM BAR 0x30 left as zero => unimplemented. */

	put16(cfg, CFG_SUBSYS_VENDOR, ids->subsys_vendor);
	put16(cfg, CFG_SUBSYS_DEVICE, ids->subsys_device);

	put8(cfg, CFG_CAP_PTR,  0x00); /* no capability list */
	put8(cfg, CFG_INT_LINE, 0x00);
	put8(cfg, CFG_INT_PIN,  0x00); /* no INTx interrupt */
}

u32 mock_cfg_read(const u8 *cfg, int where, int size)
{
	u32 v = 0;
	int i;

	if (size != 1 && size != 2 && size != 4)
		return 0xffffffff;
	/* Any access wholly or partly outside config space reads as all-ones. */
	if (where < 0 || where + size > MOCK_CFG_SPACE_SIZE) {
		if (size == 1)
			return 0xff;
		if (size == 2)
			return 0xffff;
		return 0xffffffff;
	}

	for (i = 0; i < size; i++)
		v |= ((u32)cfg[where + i]) << (8 * i);
	return v;
}

/* Only these byte offsets accept writes; everything else is read-only. */
static int cfg_byte_writable(int off)
{
	switch (off) {
	case CFG_COMMAND:        /* command register, 2 bytes */
	case CFG_COMMAND + 1:
	case CFG_CACHE_LINE:
	case CFG_LATENCY:
	case CFG_INT_LINE:
		return 1;
	default:
		return 0;
	}
}

void mock_cfg_write(u8 *cfg, int where, int size, u32 val)
{
	int i;

	if (size != 1 && size != 2 && size != 4)
		return;
	if (where < 0 || where + size > MOCK_CFG_SPACE_SIZE)
		return;

	for (i = 0; i < size; i++) {
		if (cfg_byte_writable(where + i))
			cfg[where + i] = (u8)((val >> (8 * i)) & 0xff);
	}
}
