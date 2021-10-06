// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_sscd.h"
#include <linux/platform_data/sscoredump.h>
#include <linux/platform_device.h>

/***************************************************************************************************
 * This feature is a WIP, and is pending Firmware + core KMD support for:                          *
 *        - Dumping FW private memory                                                              *
 *        - Suspending the MCU                                                                     *
 *        - Dumping MCU registers                                                                  *
 **************************************************************************************************/

static void sscd_release(struct device *dev)
{
	(void)dev;
}

static struct sscd_platform_data sscd_pdata;
static struct platform_device sscd_dev = { .name = "mali",
					   .driver_override = SSCD_NAME,
					   .id = -1,
					   .dev = {
						   .platform_data = &sscd_pdata,
						   .release = sscd_release,
					   } };

enum
{
	MCU_REGISTERS = 0x1,
	GPU_REGISTERS = 0x2,
	PRIVATE_MEM = 0x3,
	SHARED_MEM = 0x4,
	NUM_SEGMENTS
} sscd_segs;

/*
 * Stub pending FW support
 */
static void get_fw_private_memory(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	(void)kbdev;
	(void)seg;
}
/*
 * Stub pending FW support
 */
static void get_fw_shared_memory(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	(void)kbdev;
	(void)seg;
}
/*
 * Stub pending FW support
 */
static void get_fw_registers(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	(void)kbdev;
	(void)seg;
}

/*
 * Stub pending FW support
 */
static void get_gpu_registers(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	(void)kbdev;
	(void)seg;
}
/*
 * Stub pending FW support
 */
static void flush_caches(struct kbase_device *kbdev)
{
	(void)kbdev;
}
/*
 * Stub pending FW support
 */
static void suspend_mcu(struct kbase_device *kbdev)
{
	(void)kbdev;
}

static int gpu_fw_tracing_init(struct kbase_device *kbdev)
{
	(void)kbdev;
	return 0;
}

static void gpu_fw_tracing_term(struct kbase_device *kbdev)
{
	(void)kbdev;
}

/**
 * gpu_sscd_dump() - Initiates and reports a subsystem core-dump of the GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @reason: A null terminated string containing a dump reason
 *
 * Context: Process context.
 */
void gpu_sscd_dump(struct kbase_device *kbdev, const char* reason)
{
	struct sscd_segment segs[NUM_SEGMENTS];
	struct sscd_platform_data *pdata = dev_get_platdata(&sscd_dev.dev);
	unsigned long flags;

	dev_info(kbdev->dev, "Mali subsystem core dump in progress");
	/* No point in proceeding if we can't report the dumped data */
	if (!pdata->sscd_report) {
		dev_warn(kbdev->dev, "Failed to report core dump, sscd_report was NULL");
		return;
	}

	/* Zero init everything for safety */
	memset(segs, 0, sizeof(segs));

	/* We don't want anything messing with the HW while we dump */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Suspend the MCU to prevent it from overwriting the data we want to dump */
	suspend_mcu(kbdev);

	/* Flush the cache so our memory page reads contain up to date values */
	flush_caches(kbdev);

	/* Read out the updated FW private memory pages */
	get_fw_private_memory(kbdev, &segs[PRIVATE_MEM]);

	/* Read out the updated memory shared between host and firmware */
	get_fw_shared_memory(kbdev, &segs[SHARED_MEM]);

	get_fw_registers(kbdev, &segs[MCU_REGISTERS]);
	get_gpu_registers(kbdev, &segs[GPU_REGISTERS]);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Report the core dump and generate an ELF header for it */
	pdata->sscd_report(&sscd_dev, segs, NUM_SEGMENTS, SSCD_FLAGS_ELFARM64HDR, reason);
}

/**
 * gpu_sscd_init() - Registers the SSCD platform device and inits a firmware trace buffer.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context.
 *
 * Return: On success, returns 0 otherwise returns an error code.
 */
int gpu_sscd_init(struct kbase_device *kbdev)
{
	int ret;

	ret = gpu_fw_tracing_init(kbdev);
	if (ret != 0)
		goto err;

	ret = platform_device_register(&sscd_dev);
	if (ret != 0)
		goto err;

err:
	return ret;
}

/**
 * gpu_sscd_term() - Unregisters the SSCD platform device and suspends FW tracing.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context.
 */
void gpu_sscd_term(struct kbase_device *kbdev)
{
	platform_device_unregister(&sscd_dev);
	gpu_fw_tracing_term(kbdev);
}

