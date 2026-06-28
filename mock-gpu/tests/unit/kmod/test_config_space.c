/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Unit tests for the pure PCI config-space helper (kmod/mock_pci_cfg.c).
 * Compiled in userspace; no kernel involved (TESTING.md §1.2).
 *
 * Expected identity values are injected from versions.mk via -D defines so the
 * test and the kernel module share one source of truth (PLAN §9).
 */
#include <stdio.h>
#include <stdint.h>
#include "mock_pci_cfg.h"

#ifndef NV_VENDOR_ID
#error "NV_VENDOR_ID must be provided via -D (from versions.mk)"
#endif

static int g_fail, g_ok;

#define CHECK(cond, fmt, ...)                                            \
	do {                                                             \
		if (cond) { g_ok++; }                                    \
		else { g_fail++;                                         \
			printf("FAIL: " fmt "\n", ##__VA_ARGS__); }      \
	} while (0)

#define CHECK_EQ(exp, act, what)                                         \
	do {                                                             \
		unsigned long _e = (unsigned long)(exp);                 \
		unsigned long _a = (unsigned long)(act);                 \
		if (_e == _a) { g_ok++; }                                \
		else { g_fail++;                                         \
			printf("FAIL: %s: expected 0x%lx got 0x%lx\n",   \
			       what, _e, _a); }                          \
	} while (0)

int main(void)
{
	u8 cfg[MOCK_CFG_SPACE_SIZE];
	struct mock_pci_ids ids = {
		.vendor        = NV_VENDOR_ID,
		.device        = NV_DEVICE_ID,
		.subsys_vendor = NV_SUBSYS_VENDOR,
		.subsys_device = NV_SUBSYS_DEVICE,
		.revision      = NV_REVISION,
		.class_prog    = NV_CLASS_PROG,
		.class_sub     = NV_CLASS_SUB,
		.class_base    = NV_CLASS_BASE,
	};
	int i;

	mock_cfg_init(cfg, &ids);

	/* Identity registers */
	CHECK_EQ(NV_VENDOR_ID,      mock_cfg_read(cfg, 0x00, 2), "vendor id");
	CHECK_EQ(NV_DEVICE_ID,      mock_cfg_read(cfg, 0x02, 2), "device id");
	CHECK_EQ(NV_REVISION,       mock_cfg_read(cfg, 0x08, 1), "revision");
	CHECK_EQ(NV_CLASS_PROG,     mock_cfg_read(cfg, 0x09, 1), "class prog-if");
	CHECK_EQ(NV_CLASS_SUB,      mock_cfg_read(cfg, 0x0a, 1), "class sub");
	CHECK_EQ(NV_CLASS_BASE,     mock_cfg_read(cfg, 0x0b, 1), "class base");
	CHECK_EQ(NV_SUBSYS_VENDOR,  mock_cfg_read(cfg, 0x2c, 2), "subsys vendor");
	CHECK_EQ(NV_SUBSYS_DEVICE,  mock_cfg_read(cfg, 0x2e, 2), "subsys device");

	/* 4-byte class+revision read: rev | prog<<8 | sub<<16 | base<<24 */
	{
		u32 want = (u32)NV_REVISION
			 | ((u32)NV_CLASS_PROG << 8)
			 | ((u32)NV_CLASS_SUB  << 16)
			 | ((u32)NV_CLASS_BASE << 24);
		CHECK_EQ(want, mock_cfg_read(cfg, 0x08, 4), "class+rev dword");
	}

	/* Header type 0, single-function (bit 7 clear) */
	CHECK_EQ(0x00, mock_cfg_read(cfg, 0x0e, 1), "header type single-function");

	/* No capability list: status bit 4 clear, cap pointer 0 */
	CHECK_EQ(0, mock_cfg_read(cfg, 0x06, 2) & 0x0010, "status CAP_LIST clear");
	CHECK_EQ(0x00, mock_cfg_read(cfg, 0x34, 1), "capabilities pointer = 0");

	/* No interrupt pin (no INTx) */
	CHECK_EQ(0x00, mock_cfg_read(cfg, 0x3d, 1), "interrupt pin = 0");

	/* All 6 BARs + expansion ROM unimplemented (read 0) */
	for (i = 0; i < 6; i++)
		CHECK_EQ(0, mock_cfg_read(cfg, 0x10 + i * 4, 4), "BAR unimplemented");
	CHECK_EQ(0, mock_cfg_read(cfg, 0x30, 4), "expansion ROM unimplemented");

	/* BAR sizing probe: write all-ones, must read back 0 (read-only/unimplemented) */
	mock_cfg_write(cfg, 0x10, 4, 0xffffffff);
	CHECK_EQ(0, mock_cfg_read(cfg, 0x10, 4), "BAR0 stays 0 after sizing write");

	/* Identity registers are immutable */
	mock_cfg_write(cfg, 0x00, 2, 0x1234);
	CHECK_EQ(NV_VENDOR_ID, mock_cfg_read(cfg, 0x00, 2), "vendor id immutable");
	mock_cfg_write(cfg, 0x0b, 1, 0xff);
	CHECK_EQ(NV_CLASS_BASE, mock_cfg_read(cfg, 0x0b, 1), "class base immutable");

	/* Command register is writable (so pci_enable_device sticks) */
	mock_cfg_write(cfg, 0x04, 2, 0x0006);
	CHECK_EQ(0x0006, mock_cfg_read(cfg, 0x04, 2), "command register writable");

	/* Out-of-range read returns all-ones */
	CHECK_EQ(0xffffffff, mock_cfg_read(cfg, 0x100, 4), "out-of-range read = ~0");
	CHECK_EQ(0xffff,     mock_cfg_read(cfg, 0xff,  2), "straddle end read = ~0");

	printf("-----------------------------------------------\n");
	if (g_fail == 0) {
		printf("config-space: ALL PASSED (%d checks)\n", g_ok);
		return 0;
	}
	printf("config-space: FAILURES %d (passed %d)\n", g_fail, g_ok);
	return 1;
}
