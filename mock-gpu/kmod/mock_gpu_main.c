// SPDX-License-Identifier: GPL-2.0
/*
 * mock_gpu_main.c - A mock NVIDIA GPU as a Linux kernel module.
 *
 * Exposes, on the host where it is loaded:
 *   1. One or more fake PCI devices (NVIDIA vendor/device IDs, 3D-controller
 *      class) visible under /sys/bus/pci/devices/ so Node Feature Discovery
 *      detects an NVIDIA GPU.  [optional, module param enable_pci]
 *   2. Char devices /dev/nvidia0..N-1, /dev/nvidiactl, /dev/nvidia-uvm so that
 *      libnvidia-container / the device-plugin find the expected device nodes.
 *   3. /proc/driver/nvidia/version so libnvidia-container detects a "driver".
 *
 * The device is intentionally INERT: no BARs, no capabilities, no interrupts,
 * and a forced driver_override so NO real driver (e.g. nouveau) ever binds to
 * it. It cannot perform any GPU work. See docs/PLAN.md.
 *
 * SAFETY: the fake-PCI path manipulates the live PCI subsystem and is the
 * highest-risk part of this module. It is gated behind enable_pci and degrades
 * gracefully (the module still loads if PCI setup fails). Validate on a
 * snapshotted/disposable host first.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/pci.h>

#include "mock_pci_cfg.h"

/* ---- build-time identity (injected from versions.mk via ccflags) --------- */
#ifndef NV_VENDOR_ID
#define NV_VENDOR_ID      0x10de
#endif
#ifndef NV_DEVICE_ID
#define NV_DEVICE_ID      0x1eb8
#endif
#ifndef NV_SUBSYS_VENDOR
#define NV_SUBSYS_VENDOR  0x10de
#endif
#ifndef NV_SUBSYS_DEVICE
#define NV_SUBSYS_DEVICE  0x12a2
#endif
#ifndef NV_REVISION
#define NV_REVISION       0xa1
#endif
#ifndef NV_CLASS_PROG
#define NV_CLASS_PROG     0x00
#endif
#ifndef NV_CLASS_SUB
#define NV_CLASS_SUB      0x02
#endif
#ifndef NV_CLASS_BASE
#define NV_CLASS_BASE     0x03
#endif
#ifndef NV_DRIVER_VERSION
#define NV_DRIVER_VERSION "535.104.05"
#endif
#ifndef MOCK_GPU_COUNT
#define MOCK_GPU_COUNT    1
#endif
#ifndef MOCK_ENABLE_PCI
#define MOCK_ENABLE_PCI   1
#endif
#ifndef MOCK_PCI_DOMAIN
#define MOCK_PCI_DOMAIN   0x1000
#endif
#ifndef MOCK_PCI_BUSNR
#define MOCK_PCI_BUSNR    0
#endif

#define MOCK_MAX_GPUS     16
#define MOCK_MINORS       256
#define MOCK_MINOR_UVM    254
#define MOCK_MINOR_CTL    255
/* A driver name no real driver has => the fake device stays permanently unbound. */
#define MOCK_DRIVER_OVERRIDE "mock_gpu_noauto"
#define MOCK_ARCH_STR     "x86_64"

/* ---- module parameters --------------------------------------------------- */
static int mock_count = MOCK_GPU_COUNT;
module_param_named(count, mock_count, int, 0444);
MODULE_PARM_DESC(count, "number of mock GPUs to expose (1.." __stringify(MOCK_MAX_GPUS) ")");

static int mock_enable_pci = MOCK_ENABLE_PCI;
module_param_named(enable_pci, mock_enable_pci, int, 0444);
MODULE_PARM_DESC(enable_pci, "1=create fake PCI device(s), 0=skip (use NodeFeatureRule gate)");

static int mock_pci_domain = MOCK_PCI_DOMAIN;
module_param_named(pci_domain, mock_pci_domain, int, 0444);
MODULE_PARM_DESC(pci_domain, "synthetic PCI domain number for the fake bus");

static int mock_pci_busnr = MOCK_PCI_BUSNR;
module_param_named(pci_busnr, mock_pci_busnr, int, 0444);
MODULE_PARM_DESC(pci_busnr, "PCI bus number within the synthetic domain");

/* ---- char devices -------------------------------------------------------- */
static dev_t        mock_devt_base;
static struct cdev  mock_cdev;
static struct class *mock_class;

static int mock_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mock_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mock_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* The mock has no real RM ioctl ABI; consumers (mock NVML) never call here. */
	return -ENOTTY;
}

static const struct file_operations mock_fops = {
	.owner          = THIS_MODULE,
	.open           = mock_dev_open,
	.release        = mock_dev_release,
	.unlocked_ioctl = mock_dev_ioctl,
	.llseek         = no_llseek,
};

/* Expose nodes as 0666 (mimic real /dev/nvidia*); consumers run as root anyway. */
static char *mock_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

/* device_destroy() is a no-op for a devt with no registered device, so calling
 * this on a partially-created set during error unwind is safe. */
static void mock_chrdev_destroy_devices(void)
{
	int i;

	if (IS_ERR_OR_NULL(mock_class))
		return;
	for (i = 0; i < MOCK_MAX_GPUS; i++)
		device_destroy(mock_class, MKDEV(MAJOR(mock_devt_base), i));
	device_destroy(mock_class, MKDEV(MAJOR(mock_devt_base), MOCK_MINOR_UVM));
	device_destroy(mock_class, MKDEV(MAJOR(mock_devt_base), MOCK_MINOR_CTL));
}

static int mock_chrdev_setup(void)
{
	int i, err;
	struct device *d;

	err = alloc_chrdev_region(&mock_devt_base, 0, MOCK_MINORS, "nvidia_mock");
	if (err) {
		pr_err("mock_gpu: alloc_chrdev_region failed: %d\n", err);
		return err;
	}

	cdev_init(&mock_cdev, &mock_fops);
	mock_cdev.owner = THIS_MODULE;
	err = cdev_add(&mock_cdev, mock_devt_base, MOCK_MINORS);
	if (err) {
		pr_err("mock_gpu: cdev_add failed: %d\n", err);
		goto err_region;
	}

	mock_class = class_create("nvidia_mock");
	if (IS_ERR(mock_class)) {
		err = PTR_ERR(mock_class);
		pr_err("mock_gpu: class_create failed: %d\n", err);
		goto err_cdev;
	}
	mock_class->devnode = mock_devnode;

	for (i = 0; i < mock_count; i++) {
		d = device_create(mock_class, NULL,
				  MKDEV(MAJOR(mock_devt_base), i), NULL,
				  "nvidia%d", i);
		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			pr_err("mock_gpu: device_create nvidia%d failed: %d\n", i, err);
			goto err_devices;
		}
	}

	d = device_create(mock_class, NULL,
			  MKDEV(MAJOR(mock_devt_base), MOCK_MINOR_CTL), NULL,
			  "nvidiactl");
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		pr_err("mock_gpu: device_create nvidiactl failed: %d\n", err);
		goto err_devices;
	}

	d = device_create(mock_class, NULL,
			  MKDEV(MAJOR(mock_devt_base), MOCK_MINOR_UVM), NULL,
			  "nvidia-uvm");
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		pr_err("mock_gpu: device_create nvidia-uvm failed: %d\n", err);
		goto err_devices;
	}

	pr_info("mock_gpu: created char devices (major %d): nvidia0..%d, nvidiactl, nvidia-uvm\n",
		MAJOR(mock_devt_base), mock_count - 1);
	return 0;

err_devices:
	mock_chrdev_destroy_devices();
	class_destroy(mock_class);
	mock_class = NULL;
err_cdev:
	cdev_del(&mock_cdev);
err_region:
	unregister_chrdev_region(mock_devt_base, MOCK_MINORS);
	return err;
}

static void mock_chrdev_teardown(void)
{
	mock_chrdev_destroy_devices();
	if (!IS_ERR_OR_NULL(mock_class))
		class_destroy(mock_class);
	mock_class = NULL;
	cdev_del(&mock_cdev);
	unregister_chrdev_region(mock_devt_base, MOCK_MINORS);
}

/* ---- /proc/driver/nvidia/version ----------------------------------------- */
static struct proc_dir_entry *mock_proc_nvidia;

static int mock_version_show(struct seq_file *m, void *v)
{
	seq_printf(m,
		   "NVRM version: NVIDIA UNIX Open Kernel Module for %s  %s  Release Build  (mock@mock-gpu)\n",
		   MOCK_ARCH_STR, NV_DRIVER_VERSION);
	seq_puts(m, "GCC version:  gcc version (mock build)\n");
	return 0;
}

static int mock_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, mock_version_show, NULL);
}

static const struct proc_ops mock_version_pops = {
	.proc_open    = mock_version_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int mock_proc_setup(void)
{
	struct proc_dir_entry *ver;

	mock_proc_nvidia = proc_mkdir("driver/nvidia", NULL);
	if (!mock_proc_nvidia) {
		pr_err("mock_gpu: proc_mkdir(driver/nvidia) failed (already present?)\n");
		return -ENOMEM;
	}
	ver = proc_create("version", 0444, mock_proc_nvidia, &mock_version_pops);
	if (!ver) {
		pr_err("mock_gpu: proc_create(version) failed\n");
		proc_remove(mock_proc_nvidia);
		mock_proc_nvidia = NULL;
		return -ENOMEM;
	}
	return 0;
}

static void mock_proc_teardown(void)
{
	/* proc_remove() removes the directory and all children (incl. version). */
	if (mock_proc_nvidia) {
		proc_remove(mock_proc_nvidia);
		mock_proc_nvidia = NULL;
	}
}

/* ---- fake PCI device(s) -------------------------------------------------- */
static struct mock_pci_ids mock_ids;
static u8 mock_cfg[MOCK_MAX_GPUS][MOCK_CFG_SPACE_SIZE];
static struct pci_sysdata mock_sysdata;
static struct pci_bus *mock_bus;

static int mock_pci_read(struct pci_bus *bus, unsigned int devfn,
			 int where, int size, u32 *val)
{
	unsigned int slot = PCI_SLOT(devfn);

	if (PCI_FUNC(devfn) != 0 || slot >= (unsigned int)mock_count) {
		/* Absent function: all-ones is the PCI "no device" convention. */
		*val = (size == 1) ? 0xff : (size == 2) ? 0xffff : 0xffffffff;
		return PCIBIOS_SUCCESSFUL;
	}
	*val = mock_cfg_read(mock_cfg[slot], where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int mock_pci_write(struct pci_bus *bus, unsigned int devfn,
			  int where, int size, u32 val)
{
	unsigned int slot = PCI_SLOT(devfn);

	if (PCI_FUNC(devfn) != 0 || slot >= (unsigned int)mock_count)
		return PCIBIOS_SUCCESSFUL;
	mock_cfg_write(mock_cfg[slot], where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mock_pci_ops = {
	.read  = mock_pci_read,
	.write = mock_pci_write,
};

static int mock_pci_setup(void)
{
	LIST_HEAD(resources);
	int slot;

	mock_sysdata.domain = mock_pci_domain;
	mock_sysdata.node   = NUMA_NO_NODE;

	pci_add_resource(&resources, &ioport_resource);
	pci_add_resource(&resources, &iomem_resource);

	mock_bus = pci_create_root_bus(NULL, mock_pci_busnr, &mock_pci_ops,
				       &mock_sysdata, &resources);
	if (!mock_bus) {
		/* On failure WE own the resource list and must free it. */
		pci_free_resource_list(&resources);
		pr_warn("mock_gpu: pci_create_root_bus(domain=0x%x bus=%d) failed\n",
			mock_pci_domain, mock_pci_busnr);
		return -ENODEV;
	}
	/* On success the bus took ownership of the resource list entries. */

	pci_scan_child_bus(mock_bus);

	/*
	 * Pin driver_override to a non-existent driver name on every fabricated
	 * device BEFORE pci_bus_add_devices() runs driver matching. This forces
	 * the devices to stay permanently UNBOUND, so nothing (e.g. nouveau)
	 * ever touches their non-existent BARs. The string is kstrdup'd because
	 * the device core kfree()s driver_override on release.
	 */
	for (slot = 0; slot < mock_count; slot++) {
		struct pci_dev *pdev = pci_get_slot(mock_bus, PCI_DEVFN(slot, 0));

		if (pdev) {
			pdev->driver_override = kstrdup(MOCK_DRIVER_OVERRIDE, GFP_KERNEL);
			pci_dev_put(pdev);
		} else {
			pr_warn("mock_gpu: expected fake device at slot %d not found\n", slot);
		}
	}

	pci_bus_add_devices(mock_bus);

	pr_info("mock_gpu: created %d fake PCI device(s) [%04x:%04x] in domain 0x%x bus %d\n",
		mock_count, NV_VENDOR_ID, NV_DEVICE_ID, mock_pci_domain, mock_pci_busnr);
	return 0;
}

static void mock_pci_teardown(void)
{
	if (!mock_bus)
		return;
	pci_lock_rescan_remove();
	pci_stop_root_bus(mock_bus);
	pci_remove_root_bus(mock_bus);
	pci_unlock_rescan_remove();
	mock_bus = NULL;
}

/* ---- module init / exit -------------------------------------------------- */
static int __init mock_gpu_init(void)
{
	int i, err;

	if (mock_count < 1)
		mock_count = 1;
	if (mock_count > MOCK_MAX_GPUS) {
		pr_warn("mock_gpu: count %d clamped to %d\n", mock_count, MOCK_MAX_GPUS);
		mock_count = MOCK_MAX_GPUS;
	}

	mock_ids.vendor        = NV_VENDOR_ID;
	mock_ids.device        = NV_DEVICE_ID;
	mock_ids.subsys_vendor = NV_SUBSYS_VENDOR;
	mock_ids.subsys_device = NV_SUBSYS_DEVICE;
	mock_ids.revision      = NV_REVISION;
	mock_ids.class_prog    = NV_CLASS_PROG;
	mock_ids.class_sub     = NV_CLASS_SUB;
	mock_ids.class_base    = NV_CLASS_BASE;
	for (i = 0; i < mock_count; i++)
		mock_cfg_init(mock_cfg[i], &mock_ids);

	err = mock_chrdev_setup();
	if (err)
		return err;

	err = mock_proc_setup();
	if (err)
		goto err_chrdev;

	if (mock_enable_pci) {
		if (mock_pci_setup())
			pr_warn("mock_gpu: fake PCI not created; use NodeFeatureRule for the NFD gate\n");
	} else {
		pr_info("mock_gpu: enable_pci=0; skipping fake PCI (NodeFeatureRule gate expected)\n");
	}

	pr_info("mock_gpu: loaded (count=%d, driver=%s, product=%s)\n",
		mock_count, NV_DRIVER_VERSION, "mock NVIDIA GPU");
	return 0;

err_chrdev:
	mock_chrdev_teardown();
	return err;
}

static void __exit mock_gpu_exit(void)
{
	mock_pci_teardown();
	mock_proc_teardown();
	mock_chrdev_teardown();
	pr_info("mock_gpu: unloaded\n");
}

module_init(mock_gpu_init);
module_exit(mock_gpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mock-gpu");
MODULE_DESCRIPTION("Mock NVIDIA GPU device (fake PCI + char devices) for GPU Operator testing");
MODULE_VERSION("0.1.0");
