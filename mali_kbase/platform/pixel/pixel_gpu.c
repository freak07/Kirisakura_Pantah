// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_control.h"

/**
 * gpu_pixel_init() - Initializes the Pixel integration for the Mali GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: On success, returns 0. On failure an error code is returned.
 */
static int gpu_pixel_init(struct kbase_device *kbdev)
{
	int ret;

	struct pixel_context *pc;

	pc = kzalloc(sizeof(struct pixel_context), GFP_KERNEL);
	if (pc == NULL) {
		GPU_LOG(LOG_ERROR, kbdev, "failed to alloc platform context struct\n");
		ret = -ENOMEM;
		goto done;
	}

	kbdev->platform_context = pc;
	pc->kbdev = kbdev;

	ret = gpu_power_init(kbdev);
	if (ret) {
		GPU_LOG(LOG_ERROR, kbdev, "power init failed\n");
		goto done;
	}

	ret = 0;
done:
	return ret;
}

/**
 * gpu_pixel_term() - Terminates the Pixel integration for the Mali GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 */
static void gpu_pixel_term(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	gpu_power_term(kbdev);

	kbdev->platform_context = NULL;
	kfree(pc);
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &gpu_pixel_init,
	.platform_term_func = &gpu_pixel_term,
};
