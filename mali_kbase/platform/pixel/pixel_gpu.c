// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>

/**
 * gpu_pixel_init() - Initialization entrypoint for the Pixel integration of the
 * Mali GPU.
 *
 * @kbdev: The Mali kbase context in the process of being initialized
 *
 * Return: An errorcode, or 0 on success.
 */
static int gpu_pixel_init(struct kbase_device *kbdev)
{
	return 0;
}

/**
 * gpu_pixel_term() - Termination call to initiate teardown of structures set up
 * for the Pixel integration of the Mali GPU.
 *
 * @kbdev: The Mali kbase context the teardown should occur on
 */
static void gpu_pixel_term(struct kbase_device *kbdev)
{
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &gpu_pixel_init,
	.platform_term_func = &gpu_pixel_term,
};
