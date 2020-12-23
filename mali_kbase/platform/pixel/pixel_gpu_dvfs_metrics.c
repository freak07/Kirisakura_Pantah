// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Linux includes */
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#include <trace/events/power.h>

/* SOC includes */
#if IS_ENABLED(CONFIG_CAL_IF)
#include <soc/google/cal-if.h>
#endif

/* Mali core includes */
#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_power_gpu_frequency_trace.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_control.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_dvfs.h"


/**
 * gpu_dvfs_metrics_trace_clock() - Emits trace events corresponding to a change in GPU clocks.
 *
 * @kbdev:    The &struct kbase_device for the GPU.
 * @power_on: Whether the GPU is currently powered on or not.
 */
void gpu_dvfs_metrics_trace_clock(struct kbase_device *kbdev, bool power_on)
{
	struct pixel_context *pc = kbdev->platform_context;
	int proc = raw_smp_processor_id();
	int gpu0 = 0;
	int gpu1 = 0;

	if (power_on) {
		gpu0 = cal_dfs_get_rate(pc->dvfs.gpu0_cal_id);
		gpu1 = cal_dfs_get_rate(pc->dvfs.gpu1_cal_id);
	}

	trace_clock_set_rate("gpu0", gpu0, proc);
	trace_clock_set_rate("gpu1", gpu1, proc);

	trace_gpu_frequency(gpu0, 0);
	trace_gpu_frequency(gpu1, 1);
}

/**
 * gpu_dvfs_metrics_update() - Updates GPU metrics on level or power change.
 *
 * @kbdev:       The &struct kbase_device for the GPU.
 * @next_level:  The level that the GPU is about to move to. Can be the same as the current level.
 * @power_state: The current power state of the GPU. Can be the same as the current power state.
 *
 * This function should be called (1) right after a change in power state of the GPU, or (2) just
 * prior to changing the level of a powered on GPU. It will update the metrics for each of the GPU
 * DVFS level metrics and the power metrics as appropriate.
 *
 * Context: Expects the caller to hold the DVFS lock.
 */
void gpu_dvfs_metrics_update(struct kbase_device *kbdev, int next_level, bool power_state)
{
	struct pixel_context *pc = kbdev->platform_context;
	int level = pc->dvfs.level;

	const u64 prev = pc->dvfs.metrics.last_time;
	u64 curr = ktime_get_ns();

	lockdep_assert_held(&pc->dvfs.lock);

	if (pc->dvfs.metrics.last_power_state) {
		if (power_state) {
			/* Power state was ON and is not changing */
			if (level != next_level) {
				pc->dvfs.table[next_level].metrics.entry_count++;
				pc->dvfs.table[next_level].metrics.time_last_entry = curr;
			}
		} else {
			/* Power status was ON and is turning OFF */
			pc->pm.power_off_metrics.entry_count++;
			pc->pm.power_off_metrics.time_last_entry = curr;
		}

		pc->dvfs.table[level].metrics.time_total += (curr - prev);
		pc->pm.power_on_metrics.time_total += (curr - prev);

	} else {
		if (power_state) {
			/* Power state was OFF and is turning ON */
			pc->pm.power_on_metrics.entry_count++;
			pc->pm.power_on_metrics.time_last_entry = curr;

			if (pc->dvfs.metrics.last_level != next_level) {
				/* Level was changed while the GPU was powered off, and that change
				 * is being reflected now.
				 */
				pc->dvfs.table[next_level].metrics.entry_count++;
				pc->dvfs.table[next_level].metrics.time_last_entry = curr;
			}
		}

		pc->pm.power_off_metrics.time_total += (curr - prev);
	}

	pc->dvfs.metrics.last_power_state = power_state;
	pc->dvfs.metrics.last_time = curr;
	pc->dvfs.metrics.last_level = next_level;
}

/**
 * gpu_dvfs_metrics_init() - Initializes DVFS metrics.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context. Takes and releases the DVFS lock.
 *
 * Return: This function currently always returns 0 for success.
 */
int gpu_dvfs_metrics_init(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	mutex_lock(&pc->dvfs.lock);

	pc->dvfs.metrics.last_time = ktime_get_ns();
	pc->dvfs.metrics.last_power_state = gpu_power_status(kbdev);

	pc->dvfs.table[pc->dvfs.level].metrics.entry_count++;
	pc->dvfs.table[pc->dvfs.level].metrics.time_last_entry =
		pc->dvfs.metrics.last_time;

	mutex_unlock(&pc->dvfs.lock);

	return 0;
}

/**
 * gpu_dvfs_metrics_term() - Terminates DVFS metrics
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Note that this function currently doesn't do anything.
 */
void gpu_dvfs_metrics_term(struct kbase_device *kbdev)
{
}
