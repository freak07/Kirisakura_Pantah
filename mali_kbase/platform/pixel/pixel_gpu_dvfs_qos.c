// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Linux includes */
#include <linux/of.h>

/* SOC includes */
#ifdef CONFIG_MALI_PIXEL_GPU_BTS
#include <soc/google/bts.h>
#endif

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_control.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_dvfs.h"


/**
 * gpu_dvfs_qos_set() - Issue QOS requests for a GPU DVFS level.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @level: The DVFS level from which to retrieve QOS values from.
 *
 * Context: Process context. Expects caller to hold the DVFS lock.
 */
void gpu_dvfs_qos_set(struct kbase_device *kbdev, int level)
{
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_opp opp = pc->dvfs.table[level];

	lockdep_assert_held(&pc->dvfs.lock);

	if (pc->dvfs.qos.level_last != level) {

		GPU_LOG(LOG_DEBUG, kbdev,
			"QOS int_min:  %d\n"
			"QOS mif_min:  %d\n"
			"QOS cpu0_min: %d\n"
			"QOS cpu1_min: %d\n"
			"QOS cpu2_max: %d\n",
			opp.qos.int_min, opp.qos.mif_min, opp.qos.cpu0_min,
			opp.qos.cpu1_min, opp.qos.cpu2_max);

		exynos_pm_qos_update_request(&pc->dvfs.qos.int_min,  opp.qos.int_min);
		exynos_pm_qos_update_request(&pc->dvfs.qos.mif_min,  opp.qos.mif_min);
		exynos_pm_qos_update_request(&pc->dvfs.qos.cpu0_min, opp.qos.cpu0_min);
		exynos_pm_qos_update_request(&pc->dvfs.qos.cpu1_min, opp.qos.cpu1_min);
		exynos_pm_qos_update_request(&pc->dvfs.qos.cpu2_max, opp.qos.cpu2_max);

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
		if (level <= pc->dvfs.qos.bts.threshold && !pc->dvfs.qos.bts.enabled) {
			bts_add_scenario(pc->dvfs.qos.bts.scenario);
			pc->dvfs.qos.bts.enabled = true;
		} else if (level > pc->dvfs.qos.bts.threshold && pc->dvfs.qos.bts.enabled) {
			bts_del_scenario(pc->dvfs.qos.bts.scenario);
			pc->dvfs.qos.bts.enabled = false;
		}
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */

		pc->dvfs.qos.level_last = level;
		pc->dvfs.qos.enabled = true;
	}
}

/**
 * gpu_dvfs_qos_reset() - Clears QOS requests.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context. Expects caller to hold the DVFS lock.
 */
void gpu_dvfs_qos_reset(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	lockdep_assert_held(&pc->dvfs.lock);

	exynos_pm_qos_update_request(&pc->dvfs.qos.int_min,  0);
	exynos_pm_qos_update_request(&pc->dvfs.qos.mif_min,  0);
	exynos_pm_qos_update_request(&pc->dvfs.qos.cpu0_min, 0);
	exynos_pm_qos_update_request(&pc->dvfs.qos.cpu1_min, 0);
	exynos_pm_qos_update_request(&pc->dvfs.qos.cpu2_max, PM_QOS_CLUSTER2_FREQ_MAX_DEFAULT_VALUE);

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
	if (pc->dvfs.qos.bts.enabled) {
		bts_del_scenario(pc->dvfs.qos.bts.scenario);
		pc->dvfs.qos.bts.enabled = false;
	}
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */

	pc->dvfs.qos.level_last = -1;
	pc->dvfs.qos.enabled = false;
}

/**
 * gpu_dvfs_qos_init() - Initializes the Pixel GPU DVFS QOS subsystem.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: On success, returns 0. -EINVAL on error.
 */
int gpu_dvfs_qos_init(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	int ret;

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
	struct device_node *np = kbdev->dev->of_node;
	const char *bts_scenario_name;

	pc->dvfs.qos.bts.enabled = false;

	if (of_property_read_string(np, "gpu_dvfs_qos_bts_scenario", &bts_scenario_name)) {
		GPU_LOG(LOG_ERROR, kbdev, "GPU QOS BTS scenario not specified in DT\n");
		ret = -EINVAL;
		goto done;
	}

	pc->dvfs.qos.bts.scenario = bts_get_scenindex(bts_scenario_name);
	if (!pc->dvfs.qos.bts.scenario) {
		GPU_LOG(LOG_ERROR, kbdev, "invalid GPU QOS BTS scenario specified in DT\n");
		ret = -EINVAL;
		goto done;
	}

	if (of_property_read_u32(np, "gpu_dvfs_qos_bts_level", &pc->dvfs.qos.bts.threshold)) {
		GPU_LOG(LOG_ERROR, kbdev, "GPU QOS BTS threshold not specified in DT\n");
		ret = -EINVAL;
		goto done;
	}
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */

	exynos_pm_qos_add_request(&pc->dvfs.qos.int_min,  PM_QOS_DEVICE_THROUGHPUT, 0);
	exynos_pm_qos_add_request(&pc->dvfs.qos.mif_min,  PM_QOS_BUS_THROUGHPUT,    0);
	exynos_pm_qos_add_request(&pc->dvfs.qos.cpu0_min, PM_QOS_CLUSTER0_FREQ_MIN, 0);
	exynos_pm_qos_add_request(&pc->dvfs.qos.cpu1_min, PM_QOS_CLUSTER1_FREQ_MIN, 0);
	exynos_pm_qos_add_request(&pc->dvfs.qos.cpu2_max, PM_QOS_CLUSTER2_FREQ_MAX,
		PM_QOS_CLUSTER2_FREQ_MAX_DEFAULT_VALUE);

	pc->dvfs.qos.level_last = -1;
	pc->dvfs.qos.enabled = false;

	GPU_LOG(LOG_DEBUG, kbdev, "GPU QOS initialized\n");
	ret = 0;

done:
	return ret;
}

/**
 * gpu_dvfs_qos_term() - Terminates the Pixel GPU DVFS QOS subsystem.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 */
void gpu_dvfs_qos_term(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	exynos_pm_qos_remove_request(&pc->dvfs.qos.int_min);
	exynos_pm_qos_remove_request(&pc->dvfs.qos.mif_min);
	exynos_pm_qos_remove_request(&pc->dvfs.qos.cpu0_min);
	exynos_pm_qos_remove_request(&pc->dvfs.qos.cpu1_min);
	exynos_pm_qos_remove_request(&pc->dvfs.qos.cpu2_max);

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
	if (pc->dvfs.qos.bts.enabled) {
		bts_del_scenario(pc->dvfs.qos.bts.scenario);
		pc->dvfs.qos.bts.enabled = false;
	}
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */
}
