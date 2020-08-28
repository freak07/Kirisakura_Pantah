// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Linux includes */
#include <linux/pm_qos.h>

/* SOC includes */
#include <soc/google/tmu.h>

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_control.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_dvfs.h"

/**
 * gpu_tmu_get_num_levels() - Returns the number of DVFS OPPs
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: The number of DVFS operating points.
 */
int gpu_tmu_get_num_levels(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	return pc->dvfs.table_size;
}

/**
 * gpu_tmu_get_freqs_for_level() - Returns the frequencies for a DVFS OPP
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @level: The level of the DVFS OPP table to query.
 * @clk0:  Pointer to write the gpu0 clock into. Set to NULL if not required.
 * @clk1:  Pointer to write the gpu1 clock into. Set to NULL if not required.
 *
 * Return: If an invalid level is provided, returns -1, otherwise 0. Values
 *         returned in &clk0 and &clk1 are in kHZ.
 */
int gpu_tmu_get_freqs_for_level(struct kbase_device *kbdev, int level, int *clk0, int *clk1)
{
	struct pixel_context *pc = kbdev->platform_context;

	if (level < 0 || level >= pc->dvfs.table_size)
		return -1;

	if (clk0)
		*clk0 = pc->dvfs.table[level].clk0;

	if (clk1)
		*clk1 = pc->dvfs.table[level].clk1;

	return 0;
}

/**
 * gpu_tmu_get_vols_for_level() - Returns the frequencies for a DVFS OPP
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @level: The level of the DVFS OPP table to query.
 * @vol0:  Pointer to write the gpu0 voltage into. Set to NULL if not required.
 * @vol1:  Pointer to write the gpu1 voltage into. Set to NULL if not required.
 *
 * Return: If an invalid level is provided, returns -1, otherwise 0. Values
 *         returned in &vol0 and &vol1 are in mV.
 */
int gpu_tmu_get_vols_for_level(struct kbase_device *kbdev, int level, int *vol0, int *vol1)
{
	struct pixel_context *pc = kbdev->platform_context;

	if (level < 0 || level >= pc->dvfs.table_size)
		return -1;

	if (vol0)
		*vol0 = pc->dvfs.table[level].vol0;

	if (vol1)
		*vol1 = pc->dvfs.table[level].vol1;

	return 0;
}

/**
 * gpu_tmu_get_cur_level() - Returns current DVFS OPP level
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context. Takes and releases the DVFS lock.
 *
 * Return: The current DVFS operating point level.
 */
int gpu_tmu_get_cur_level(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	int level;

	mutex_lock(&pc->dvfs.lock);
	level = pc->dvfs.level;
	mutex_unlock(&pc->dvfs.lock);

	return level;
}

/**
 * gpu_tmu_get_cur_util() - Returns the utilization of the GPU
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: The utilization level of the GPU. This is an integer percentage.
 */
int gpu_tmu_get_cur_util(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	int util = 0;

	if (gpu_power_status(kbdev))
		util = atomic_read(&pc->dvfs.util);

	return util;
}

/**
 * get_level_from_tmu_data() - Translates GPU cooling data to a target DVFS level
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @data:  Integer value passed by the GPU cooling driver.
 *
 * Return: The target DVFS operating point level indicated by the GPU cooling
 *         driver.
 *
 * This function is written to work with data known to be provided by the GPU
 * cooling device on GS101 which is a target OPP level. This function simply
 * validates that this is a valid level.
 */
static int get_level_from_tmu_data(struct kbase_device *kbdev, int data)
{
	struct pixel_context *pc = kbdev->platform_context;

	if (data >= 0 && data < pc->dvfs.table_size)
		return data;

	return -1;
}

/**
 * struct gpu_tmu_notification_data - data to store TMU data for GPU driver
 *
 * @gpu_drv_data: Pointer to GPU driver data.
 * @data:         Payload of this event.
 */
struct gpu_tmu_notification_data {
	void *gpu_drv_data;
	int data;
};

/**
 * gpu_tmu_notifier() - Processes incoming TMU notifications.
 *
 * @notifier: The &struct notifier_block. Currently unused.
 * @event:    Event id.
 * @v:        Notification block struct.
 *
 * Context: Process context. Takes and releases the DVFS lock.
 *
 * Return: NOTIFY_OK on a valid event. NOTIFY_BAD if the notification data is
 *         invalid and the GPU driver intends to veto the action.
 */
static int gpu_tmu_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	struct gpu_tmu_notification_data *nd = v;
	struct kbase_device *kbdev = nd->gpu_drv_data;
	struct pixel_context *pc = kbdev->platform_context;
	int level;

	switch (event) {
	case GPU_COLD:
		GPU_LOG(LOG_DEBUG, kbdev, "%s: GPU_COLD event received\n", __func__);
		level = pc->dvfs.level_max;
		break;
	case GPU_NORMAL:
		GPU_LOG(LOG_DEBUG, kbdev, "%s: GPU_NORMAL event received\n", __func__);
		level = pc->dvfs.level_max;
		break;
	case GPU_THROTTLING:
		level = get_level_from_tmu_data(kbdev, nd->data);
		if (level < 0) {
			GPU_LOG(LOG_WARN, kbdev,
				"%s: GPU_THROTTLING event received with invalid level: %d\n",
				__func__, nd->data);
			return NOTIFY_BAD;
		}
		GPU_LOG(LOG_INFO, kbdev,
			"%s: GPU_THROTTLING event received, limiting clocks to level %d\n",
			__func__, nd->data);
		break;
	default:
		GPU_LOG(LOG_WARN, kbdev, "%s: Unexpected TMU event received\n", __func__);
		goto done;
	}

	/* Update the TMU lock level */
	mutex_lock(&pc->dvfs.lock);
	pc->dvfs.level_tmu_max = level;
	gpu_dvfs_update_level_locks(kbdev);
	mutex_unlock(&pc->dvfs.lock);

done:
	return NOTIFY_OK;
}

static struct notifier_block gpu_tmu_nb = {
	.notifier_call = gpu_tmu_notifier,
};

/**
 * gpu_tmu_init() - Initializes the Pixel TMU handling subsystem
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: Currently always returns 0.
 */
int gpu_tmu_init(struct kbase_device *kbdev)
{
	exynos_gpu_add_notifier(&gpu_tmu_nb);
	return 0;
}

/**
 * gpu_tmu_term() - Terminates the Pixel GPU TMU handling subsystem.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Note that this function currently doesn't do anything.
 */
void gpu_tmu_term(struct kbase_device *kbdev)
{
}

