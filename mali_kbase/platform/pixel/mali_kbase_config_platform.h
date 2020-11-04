/* SPDX-License-Identifier: GPL-2.0 */

/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

#ifndef _KBASE_CONFIG_PLATFORM_H_
#define _KBASE_CONFIG_PLATFORM_H_

/**
 * Power management configuration
 *
 * Attached value: pointer to @ref kbase_pm_callback_conf
 * Default value: See @ref kbase_pm_callback_conf
 */
#define POWER_MANAGEMENT_CALLBACKS (&pm_callbacks)

/**
 * Platform specific configuration functions
 *
 * Attached value: pointer to @ref kbase_platform_funcs_conf
 * Default value: See @ref kbase_platform_funcs_conf
 */
#define PLATFORM_FUNCS (&platform_funcs)

extern struct kbase_pm_callback_conf pm_callbacks;
extern struct kbase_platform_funcs_conf platform_funcs;

/**
 * Autosuspend delay
 *
 * The delay time (in milliseconds) to be used for autosuspend
 */
#define AUTO_SUSPEND_DELAY (100)

/* Linux includes */
#ifdef CONFIG_MALI_MIDGARD_DVFS
#include <linux/atomic.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#endif /* CONFIG_MALI_MIDGARD_DVFS */

/* SOC level includes */
#if IS_ENABLED(CONFIG_EXYNOS_PD)
#include <soc/google/exynos-pd.h>
#endif
#ifdef CONFIG_MALI_MIDGARD_DVFS
#ifdef CONFIG_MALI_PIXEL_GPU_QOS
#include <soc/google/exynos_pm_qos.h>
#endif /* CONFIG_MALI_MIDGARD_DVFS */
#endif /* CONFIG_MALI_PIXEL_GPU_QOS */

/* Pixel integration includes */
#include "pixel_gpu_debug.h"
#ifdef CONFIG_MALI_MIDGARD_DVFS
#include "pixel_gpu_dvfs.h"
#endif /* CONFIG_MALI_MIDGARD_DVFS */

/* All port specific fields go here */
#define OF_DATA_NUM_MAX 128
#define CPU_FREQ_MAX INT_MAX

#ifdef CONFIG_MALI_MIDGARD_DVFS
/**
 * struct gpu_dvfs_opp_metrics - Metrics data for an operating point.
 *
 * @time_total:      The total amount of time (in ns) that the device was powered on and at this
 *                   operating point.
 * @time_last_entry: The time (in ns) since device boot that this operating point was used.
 * @entry_cont:      The number of times this operating point was used.
 */
struct gpu_dvfs_opp_metrics {
	u64 time_total;
	u64 time_last_entry;
	unsigned int entry_count;
};

/**
 * struct gpu_dvfs_opp - Data for a GPU operating point.
 *
 * @clk0:         The frequency (in kHz) of GPU Top Level clock.
 * @clk1:         The frequency (in kHz) of GPU shader cores.
 *
 * @vol0:         The voltage (in mV) of the GPU Top Level power domain. Obtained via ECT.
 * @vol1:         The voltage (in mV) of the GPU shader cores domain. Obtained via ECT.
 *
 * @util_min:     The minimum threshold of utilization before the governor should consider a lower
 *                operating point.
 * @util_max:     The maximum threshold of utlization before the governor should consider moving to
 *                a higher operating point.
 * @hysteresis:   A measure of how long the governor should keep the GPU at this operating point
 *                before moving to a lower one. For example, in the basic governor, this translates
 *                directly into &hr_timer ticks for the Mali DVFS utilization thread, but other
 *                governors may chose to use this value in different ways.
 *
 * @metrics:      Metrics data for this operating point.
 *
 * @qos.mif_min:  The minimum frequency (in kHz) for the memory interface (MIF).
 * @qos.int_min:  The minimum frequency (in kHz) for the internal memory network (INT).
 * @qos.cpu0_min: The minimum frequency (in kHz) for the little CPU cluster.
 * @qos.cpu1_min: The minimum frequency (in kHz) for the medium CPU cluster.
 * @qos.cpu2_max: The maximum frequency (in kHz) for the big CPU cluster.
 *
 * Unless specified otherwise, all data is obtained from device tree.
 */
struct gpu_dvfs_opp {
	/* Clocks */
	unsigned int clk0;
	unsigned int clk1;

	/* Voltages */
	unsigned int vol0;
	unsigned int vol1;

	int util_min;
	int util_max;
	int hysteresis;

	/* Metrics */
	struct gpu_dvfs_opp_metrics metrics;

	/* QOS values */
	struct {
		int mif_min;
		int int_min;
		int cpu0_min;
		int cpu1_min;
		int cpu2_max;
	} qos;
};
#endif /* CONFIG_MALI_MIDGARD_DVFS */

/**
 * struct pixel_context - Pixel GPU context
 *
 * @kbdev:                      The &struct kbase_device for the GPU.
 *
 * @gpu_log_level: Stores the log level which can be used as a default
 *
 * @pm.state_lost:              Stores whether GPU state has been lost or not.
 * @pm.domain:                  The power domain the GPU is in.
 * @pm.status_reg_offset:       Register offset to the G3D status in the PMU. Set via DT.
 * @pm.status_local_power_mask: Mask to extract power status of the GPU. Set via DT.
 * @pm.autosuspend_delay:       Delay (in ms) before PM runtime should trigger auto suspend.
 *
 * @tz_protection_enabled:      Storing the secure rendering state of the GPU. Access to this is
 *                              controlled by the HW access lock for the GPU associated with @kbdev.
 *
 * @dvfs.lock:                  &struct mutex used to control access to DVFS levels.
 *
 * @dvfs.control_wq:            Workqueue for processing DVFS utilization metrics.
 * @dvfs.control_work:          &struct work_struct storing link to Pixel GPU code to convert
 *                              incoming utilization data from the Mali driver into DVFS changes on
 *                              the GPU.
 * @dvfs.util:                  Stores incoming utilization metrics from the Mali driver.
 * @dvfs.clockdown_wq:          Delayed workqueue for clocking down the GPU after it has been idle
 *                              for a period of time.
 * @dvfs.clockdown_work:        &struct delayed_work_struct storing link to Pixel GPU code to set
 *                              the GPU to its minimum throughput level.
 * @dvfs.clockdown_hysteresis:  The time (in ms) the GPU can remained powered off before being set
 *                              to the minimum throughput level. Set via DT.
 *
 * @dvfs.gpu0_cal_id:           ID for the GPU Top Level clock domain. Set via DT.
 * @dvfs.gpu1_cal_id:           ID for the GPU shader stack clock domain. Set via DT.
 *
 * @dvfs.table:                 Pointer to the DVFS table which is an array of &struct gpu_dvfs_opp
 * @dvfs.table_size:            Number of levels in in @dvfs.table.
 * @dvfs.level:                 The current last active level run on the GPU.
 * @dvfs.level_start:           The level at which the GPU powers on at boot. Determined via cal-if.
 * @dvfs.level_target:          The level at which the GPU should run at next power on.
 * @dvfs.level_max:             The maximum throughput level available on the GPU. Set via DT.
 * @dvfs.level_min:             The minimum throughput level available of the GPU. Set via DT.
 * @dvfs.level_scaling_max:     The maximum throughput level the GPU can run at. Set via sysfs.
 * @dvfs.level_scaling_min:     The minimum throughput level the GPU can run at. Set via sysfs.
 *
 * @dvfs.metrics_last_time:        The last time (in ns) since device boot that the DVFS metric
 *                                 logic was run.
 * @dvfs.metrics_last_power_state: The GPU's power state when the DVFS metric logic was last run.
 * @dvfs.metrics_last_level:       The GPU's level when the DVFS metric logic was last run.
 *
 * @dvfs.governor.curr:  The currently enabled DVFS governor.
 * @dvfs.governor.delay: Governor specific variable. The basic governor uses this to store the
 *                       remaining ticks before a lower throughput level will be set.
 *
 * @dvfs.qos.enabled:       Stores whether QOS requests have been set.
 * @dvfs.qos.level_last:    The level for which QOS requests were made. Negative if no QOS is set.
 * @dvfs.qos.int_min:       QOS request structure for setting minimum INT clock
 * @dvfs.qos.mif_min:       QOS request structure for setting minimum MIF clock
 * @dvfs.qos.cpu0_min:      QOS request structure for setting minimum CPU cluster 0 (little) clock
 * @dvfs.qos.cpu1_min:      QOS request structure for setting minimum CPU cluster 1 (medium) clock
 * @dvfs.qos.cpu2_max:      QOS request structure for setting maximum CPU cluster 2 (big) clock
 *
 * @dvfs.qos.bts.enabled:   Stores whether Bus Traffic Shaping is currently enabled
 * @dvfs.qos.bts.threshold: The DVFS level at which Bus Traffic Shaping will be enabled. Set via DT.
 * @dvfs.qos.bts.scenario:  The index of the Bus Traffic Shaping scenario to be used. Set via DT.
 */
struct pixel_context {
	struct kbase_device *kbdev;

	enum gpu_log_level gpu_log_level;
	struct {
		bool state_lost;
		struct exynos_pm_domain *domain;
		unsigned int status_reg_offset;
		unsigned int status_local_power_mask;
		unsigned int autosuspend_delay;
#ifdef CONFIG_MALI_MIDGARD_DVFS
		struct gpu_dvfs_opp_metrics power_off_metrics;
		struct gpu_dvfs_opp_metrics power_on_metrics;
#endif /* CONFIG_MALI_MIDGARD_DVFS */
	} pm;

#ifdef CONFIG_MALI_PIXEL_GPU_SECURE_RENDERING
	bool tz_protection_enabled;
#endif /* CONFIG_MALI_PIXEL_GPU_SECURE_RENDERING */

#ifdef CONFIG_MALI_MIDGARD_DVFS
	struct {
		struct mutex lock;

		struct workqueue_struct *control_wq;
		struct work_struct control_work;
		atomic_t util;

		struct workqueue_struct *clockdown_wq;
		struct delayed_work clockdown_work;
		unsigned int clockdown_hysteresis;

		int gpu0_cal_id;
		int gpu1_cal_id;

		struct gpu_dvfs_opp *table;
		int table_size;
		int level;
		int level_start;
		int level_target;
		int level_max;
		int level_min;
		int level_scaling_max;
		int level_scaling_min;

		u64 metrics_last_time;
		bool metrics_last_power_state;
		int metrics_last_level;

		struct {
			enum gpu_dvfs_governor_type curr;
			int delay;
		} governor;

#ifdef CONFIG_MALI_PIXEL_GPU_QOS
		struct {
			bool enabled;
			int level_last;
			struct exynos_pm_qos_request int_min;
			struct exynos_pm_qos_request mif_min;
			struct exynos_pm_qos_request cpu0_min;
			struct exynos_pm_qos_request cpu1_min;
			struct exynos_pm_qos_request cpu2_max;

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
			struct {
				bool enabled;
				int threshold;
				unsigned int scenario;
			} bts;
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */
		} qos;
#endif /* CONFIG_MALI_PIXEL_GPU_QOS */

#ifdef CONFIG_MALI_PIXEL_GPU_THERMAL
		struct {
			struct thermal_cooling_device *cdev;
			int level_limit;
		} tmu;
#endif /* CONFIG_MALI_PIXEL_GPU_THERMAL */
	} dvfs;
#endif /* CONFIG_MALI_MIDGARD_DVFS */
};

#endif /* _KBASE_CONFIG_PLATFORM_H_ */
