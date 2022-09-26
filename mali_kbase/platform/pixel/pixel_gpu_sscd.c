// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>
#include <csf/mali_kbase_csf_trace_buffer.h>
#include <csf/mali_kbase_csf_firmware_cfg.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_sscd.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_control.h"
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
const static struct platform_device sscd_dev_init = { .name = "mali",
						      .driver_override = SSCD_NAME,
						      .id = -1,
						      .dev = {
							      .platform_data = &sscd_pdata,
							      .release = sscd_release,
						      } };
static struct platform_device sscd_dev;

enum
{
	MCU_REGISTERS = 0x1,
	GPU_REGISTERS = 0x2,
	PRIVATE_MEM = 0x3,
	SHARED_MEM = 0x4,
	FW_TRACE = 0x5,
	PM_EVENT_LOG = 0x6,
	POWER_RAIL_LOG = 0x7,
	PDC_STATUS = 0x8,
	KTRACE = 0x9,
	NUM_SEGMENTS
} sscd_segs;

static void get_pm_event_log(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (seg->addr == NULL)
		return;

	if (kbase_pm_copy_event_log(kbdev, seg->addr, seg->size)) {
		dev_warn(kbdev->dev, "pixel: failed to report PM event log");
	}
}

/**
 * struct pixel_fw_trace_metadata - Info about the FW trace log
 *
 * @magic:          Always 'pfwt', helps find the log in memory dumps
 * @trace_address:  The memory address of the FW trace log
 * @trace_length:   Number of used bytes in the trace ring buffer.
 *                  The length will be <= (FW_TRACE_BUF_NR_PAGES << PAGE_SHIFT)
 * @version:        Updated whenever the binary layout changes
 * @_reserved:      Bytes reserved for future use
 **/
struct pixel_fw_trace_metadata {
	char magic[4];
	uint64_t trace_address;
	uint32_t trace_length;
	uint8_t version;
	char _reserved[31];
} __attribute__((packed));
_Static_assert(sizeof(struct pixel_fw_trace_metadata) == 48,
	       "Incorrect pixel_fw_trace_metadata size");

/**
 * struct pixel_fw_trace - The FW trace and associated meta data
 *
 * @meta:      Info about the trace log
 * @trace_log: The actual trace log
 **/
struct pixel_fw_trace {
	struct pixel_fw_trace_metadata meta;
	char trace_log[FW_TRACE_BUF_NR_PAGES << PAGE_SHIFT];
};

static void get_fw_trace(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	struct firmware_trace_buffer *tb;
	struct pixel_fw_trace *fw_trace;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (seg->addr == NULL)
		return;

	fw_trace = seg->addr;

	/* Write the default meta data */
	fw_trace->meta = (struct pixel_fw_trace_metadata) {
		.magic = "pfwt",
		.trace_address = 0,
		.trace_length = 0,
		.version = 1,
	};

	tb = kbase_csf_firmware_get_trace_buffer(kbdev, FIRMWARE_LOG_BUF_NAME);

	if (tb == NULL) {
		dev_err(kbdev->dev, "pixel: failed to open firmware trace buffer");
		return;
	}

	/* Write the trace log */
	fw_trace->meta.trace_address = (uint64_t)tb;
	fw_trace->meta.trace_length = kbase_csf_firmware_trace_buffer_read_data(
		tb, fw_trace->trace_log, sizeof(fw_trace->trace_log));

	return;
}

/**
 * struct pixel_ktrace_metadata - Info about the ktrace log
 *
 * @magic:          Always 'ktra', helps find the log in memory dumps
 * @trace_address:  The memory address of the ktrace log
 * @trace_start:    Start of the ktrace ringbuffer
 * @trace_end:      End of the ktrace ringbuffer
 * @version_major:  Ktrace major version.
 * @version_minor:  Ktrace minor version.
 * @_reserved:      Bytes reserved for future use
 **/
struct pixel_ktrace_metadata {
	char magic[4];
	uint64_t trace_address;
	uint32_t trace_start;
	uint32_t trace_end;
	uint8_t version_major;
	uint8_t version_minor;
	char _reserved[28];
} __attribute__((packed));
_Static_assert(sizeof(struct pixel_ktrace_metadata) == 50,
	       "Incorrect pixel_ktrace_metadata size");

struct pixel_ktrace {
	struct pixel_ktrace_metadata meta;
#if KBASE_KTRACE_TARGET_RBUF
	struct kbase_ktrace_msg trace_log[KBASE_KTRACE_SIZE];
#endif
};
static void get_ktrace(struct kbase_device *kbdev,
			  struct sscd_segment *seg)
{
	struct pixel_ktrace *ktrace = seg->addr;
#if KBASE_KTRACE_TARGET_RBUF
	unsigned long flags;
	u32 entries_copied = 0;
#endif

	if (seg->addr == NULL)
		return;

	ktrace->meta = (struct pixel_ktrace_metadata) { .magic = "ktra" };
#if KBASE_KTRACE_TARGET_RBUF
	lockdep_assert_held(&kbdev->hwaccess_lock);
	spin_lock_irqsave(&kbdev->ktrace.lock, flags);
	ktrace->meta.trace_address = (uint64_t)kbdev->ktrace.rbuf;
	ktrace->meta.trace_start = kbdev->ktrace.first_out;
	ktrace->meta.trace_end = kbdev->ktrace.next_in;
	ktrace->meta.version_major = KBASE_KTRACE_VERSION_MAJOR;
	ktrace->meta.version_minor = KBASE_KTRACE_VERSION_MINOR;

	entries_copied = kbasep_ktrace_copy(kbdev, seg->addr, KBASE_KTRACE_SIZE);
	if (entries_copied != KBASE_KTRACE_SIZE)
		dev_warn(kbdev->dev, "only copied %i of %i ktrace entries",
			entries_copied, KBASE_KTRACE_SIZE);
	spin_unlock_irqrestore(&kbdev->ktrace.lock, flags);

	KBASE_KTRACE_RBUF_DUMP(kbdev);
#else
	dev_warn(kbdev->dev, "ktrace information not present");
#endif
}

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

static void get_rail_state_log(struct kbase_device *kbdev, struct sscd_segment *seg)
{
	lockdep_assert_held(&((struct pixel_context*)kbdev->platform_context)->pm.lock);

	seg->addr = gpu_pm_get_rail_state_log(kbdev);
	seg->size = gpu_pm_get_rail_state_log_size(kbdev);
}

static void get_pdc_state(struct kbase_device *kbdev, struct pixel_gpu_pdc_status *pdc_status,
			  struct sscd_segment *seg)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (pdc_status == NULL) {
		dev_err(kbdev->dev, "pixel: failed to read PDC status, no storage");
		return;
	}
	gpu_debug_read_pdc_status(kbdev, pdc_status);
	seg->addr = pdc_status;
	seg->size = sizeof(*pdc_status);
}

static int segments_init(struct kbase_device *kbdev, struct sscd_segment* segments)
{
	/* Zero init everything for safety */
	memset(segments, 0, sizeof(struct sscd_segment) * NUM_SEGMENTS);

	segments[PM_EVENT_LOG].size = kbase_pm_max_event_log_size(kbdev);
	segments[PM_EVENT_LOG].addr = kzalloc(segments[PM_EVENT_LOG].size, GFP_KERNEL);

	if (!segments[PM_EVENT_LOG].addr) {
		segments[PM_EVENT_LOG].size = 0;
		dev_err(kbdev->dev, "pixel: failed to allocate for PM event log");
		return -ENOMEM;
	}

	segments[FW_TRACE].size = sizeof(struct pixel_fw_trace);
	segments[FW_TRACE].addr = kzalloc(sizeof(struct pixel_fw_trace), GFP_KERNEL);

	if (segments[FW_TRACE].addr == NULL) {
		segments[FW_TRACE].size = 0;
		dev_err(kbdev->dev, "pixel: failed to allocate for firmware trace description");
		return -ENOMEM;
	}

	segments[KTRACE].size = sizeof(struct pixel_ktrace);
	segments[KTRACE].addr = kzalloc(sizeof(struct pixel_ktrace), GFP_KERNEL);
	if (segments[KTRACE].addr == NULL) {
		segments[KTRACE].size = 0;
		dev_err(kbdev->dev, "pixel: failed to allocate for ktrace buffer");
		return -ENOMEM;
	}

	return 0;
}

static void segments_term(struct kbase_device *kbdev, struct sscd_segment* segments)
{
	(void)kbdev;

	kfree(segments[FW_TRACE].addr);
	kfree(segments[PM_EVENT_LOG].addr);
	kfree(segments[KTRACE].addr);
	/* Null out the pointers */
	memset(segments, 0, sizeof(struct sscd_segment) * NUM_SEGMENTS);
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
	struct pixel_context *pc = kbdev->platform_context;
	int ec = 0;
	unsigned long flags;
	struct pixel_gpu_pdc_status pdc_status;

	dev_info(kbdev->dev, "pixel: mali subsystem core dump in progress");
	/* No point in proceeding if we can't report the dumped data */
	if (!pdata->sscd_report) {
		dev_warn(kbdev->dev, "pixel: failed to report core dump, sscd_report was NULL");
		return;
	}

	ec = segments_init(kbdev, segs);
	if (ec != 0) {
		dev_err(kbdev->dev,
			"pixel: failed to init core dump segments (%d), partial dump in progress", ec);
	}

	/* We don't want anything messing with the HW while we dump */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Read the FW view of GPU PDC state, we get this early */
	get_pdc_state(kbdev, &pdc_status, &segs[PDC_STATUS]);

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

	get_fw_trace(kbdev, &segs[FW_TRACE]);

	get_pm_event_log(kbdev, &segs[PM_EVENT_LOG]);

	get_ktrace(kbdev, &segs[KTRACE]);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Acquire the pm lock to prevent modifications to the rail state log */
	mutex_lock(&pc->pm.lock);

	get_rail_state_log(kbdev, &segs[POWER_RAIL_LOG]);

	/* Report the core dump and generate an ELF header for it */
	pdata->sscd_report(&sscd_dev, segs, NUM_SEGMENTS, SSCD_FLAGS_ELFARM64HDR, reason);

	/* Must be held until the dump completes, as the log is referenced rather than copied */
	mutex_unlock(&pc->pm.lock);

	segments_term(kbdev, segs);
}

/**
 * gpu_sscd_fw_log_init() - Set's the FW log verbosity.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @level: The log verbosity.
 *
 * Context: Process context.
 *
 * Return: On success returns 0, otherwise returns an error code.
 */
int gpu_sscd_fw_log_init(struct kbase_device *kbdev, u32 level)
{
	u32 addr;
	int ec = kbase_csf_firmware_cfg_find_config_address(kbdev, "Log verbosity", &addr);

	if (!ec) {
		/* Update the FW log verbosity in FW memory */
		kbase_csf_update_firmware_memory(kbdev, addr, level);
	}

	return ec;
}

/**
 * gpu_sscd_init() - Registers the SSCD platform device.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context.
 *
 * Return: On success, returns 0 otherwise returns an error code.
 */
int gpu_sscd_init(struct kbase_device *kbdev)
{
	sscd_dev = sscd_dev_init;
	return platform_device_register(&sscd_dev);
}

/**
 * gpu_sscd_term() - Unregisters the SSCD platform device.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context.
 */
void gpu_sscd_term(struct kbase_device *kbdev)
{
	platform_device_unregister(&sscd_dev);
}
