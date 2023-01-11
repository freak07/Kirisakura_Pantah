// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>

/* UAPI includes */
#include <uapi/gpu/arm/midgard/platform/pixel/pixel_gpu_common_slc.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_slc.h"


/**
 * struct gpu_slc_liveness_update_info - Buffer info, and live ranges
 *
 * @buffer_va:         Array of buffer base virtual addresses
 * @buffer_sizes:      Array of buffer sizes
 * @live_ranges:       Array of &struct kbase_pixel_gpu_slc_liveness_mark denoting live ranges for
 *                     each buffer
 * @live_ranges_count: Number of elements in the live ranges buffer
 */
struct gpu_slc_liveness_update_info {
	u64* buffer_va;
	u64* buffer_sizes;
	struct kbase_pixel_gpu_slc_liveness_mark* live_ranges;
	u64 live_ranges_count;
};

/**
 * gpu_slc_liveness_update - Respond to a liveness update by trying to put the new buffers into free
 *                           SLC space, and resizing the partition to meet demand.
 *
 * @kctx:   The &struct kbase_context corresponding to a user space context which sent the liveness
 *          update
 * @info:   See struct gpu_slc_liveness_update_info
 */
static void gpu_slc_liveness_update(struct kbase_context* kctx,
                                    struct gpu_slc_liveness_update_info* info)
{
	struct kbase_device* kbdev = kctx->kbdev;
	dev_dbg(kbdev->dev, "pixel: buffer liveness update received");
	(void)info;
}

/**
 * gpu_pixel_handle_buffer_liveness_update_ioctl() - See gpu_slc_liveness_update
 *
 * @kctx:   The &struct kbase_context corresponding to a user space context which sent the liveness
 *          update
 * @update: See struct kbase_ioctl_buffer_liveness_update
 *
 * Context: Process context. Takes and releases the GPU power domain lock. Expects the caller to
 *          hold the DVFS lock.
 */
int gpu_pixel_handle_buffer_liveness_update_ioctl(struct kbase_context* kctx,
                                                  struct kbase_ioctl_buffer_liveness_update* update)
{
	int err = 0;
	struct gpu_slc_liveness_update_info info;
	u64* buff;

	/* Compute the sizes of the user space arrays that we need to copy */
	u64 const buffer_info_size = sizeof(u64) * update->buffer_count;
	u64 const live_ranges_size =
	    sizeof(struct kbase_pixel_gpu_slc_liveness_mark) * update->live_ranges_count;

	/* Nothing to do */
	if (!buffer_info_size || !live_ranges_size)
		goto done;

	/* Guard against nullptr */
	if (!update->live_ranges_address || !update->buffer_va_address || !update->buffer_sizes_address)
		goto done;

	/* Allocate the memory we require to copy from user space */
	buff = kmalloc(buffer_info_size * 2 + live_ranges_size, GFP_KERNEL);
	if (buff == NULL) {
		dev_err(kctx->kbdev->dev, "pixel: failed to allocate buffer for liveness update");
		err = -ENOMEM;
		goto done;
	}

	/* Set up the info struct by pointing into the allocation. All 8 byte aligned */
	info = (struct gpu_slc_liveness_update_info){
	    .buffer_va = buff,
	    .buffer_sizes = buff + update->buffer_count,
	    .live_ranges = (struct kbase_pixel_gpu_slc_liveness_mark*)(buff + update->buffer_count * 2),
	    .live_ranges_count = update->live_ranges_count,
	};

	/* Copy the data from user space */
	err =
	    copy_from_user(info.live_ranges, u64_to_user_ptr(update->live_ranges_address), live_ranges_size);
	if (err) {
		dev_err(kctx->kbdev->dev, "pixel: failed to copy live ranges");
		err = -EFAULT;
		goto done;
	}

	err = copy_from_user(
	    info.buffer_sizes, u64_to_user_ptr(update->buffer_sizes_address), buffer_info_size);
	if (err) {
		dev_err(kctx->kbdev->dev, "pixel: failed to copy buffer sizes");
		err = -EFAULT;
		goto done;
	}

	err = copy_from_user(info.buffer_va, u64_to_user_ptr(update->buffer_va_address), buffer_info_size);
	if (err) {
		dev_err(kctx->kbdev->dev, "pixel: failed to copy buffer addresses");
		err = -EFAULT;
		goto done;
	}

	/* Execute an slc update */
	gpu_slc_liveness_update(kctx, &info);

done:
	kfree(buff);

	return err;
}

/**
 * gpu_slc_kctx_init() - Called when a kernel context is created
 *
 * @kctx: The &struct kbase_context that is being initialized
 *
 * This function is called when the GPU driver is initializing a new kernel context. This event is
 * used to set up data structures that will be used to track this context's usage of the SLC.
 *
 * Return: Returns 0 on success, or an error code on failure.
 */
int gpu_slc_kctx_init(struct kbase_context *kctx)
{
	(void)kctx;
	return 0;
}

/**
 * gpu_slc_kctx_term() - Called when a kernel context is terminated
 *
 * @kctx: The &struct kbase_context that is being terminated
 *
 */
void gpu_slc_kctx_term(struct kbase_context *kctx)
{
	(void)kctx;
}


/**
 * gpu_slc_init - Initialize the SLC partition for the GPU
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: On success, returns 0. On failure an error code is returned.
 */
int gpu_slc_init(struct kbase_device *kbdev)
{
	(void)kbdev;
	return 0;
}

/**
 * gpu_slc_term() - Terminates the Pixel GPU SLC partition.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 */
void gpu_slc_term(struct kbase_device *kbdev)
{
	(void)kbdev;
}
