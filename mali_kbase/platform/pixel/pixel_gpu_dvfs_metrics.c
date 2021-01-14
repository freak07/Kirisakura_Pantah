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
 * gpu_dvfs_metrics_uid_level_change() - Event for updating per-UID states when GPU clocks change
 *
 * @kbdev:       The &struct kbase_device for the GPU.
 * @event_time:  The time of the clock change event in nanoseconds.
 *
 * Called when the operating point is changing so that the per-UID time in state data for in-flight
 * atoms can be updated. Note that this function need only be called when the operating point is
 * changing _and_ the GPU is powered on. This is because no atoms will be in-flight when the GPU is
 * powered down.
 *
 * Context: Called in process context, invokes an IRQ context and takes the per-UID metrics spin
 *          lock.
 */
static void gpu_dvfs_metrics_uid_level_change(struct kbase_device *kbdev, u64 event_time)
{
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_metrics_uid_stats *stats;
	unsigned long flags;
	int i;

	lockdep_assert_held(&pc->dvfs.lock);

	spin_lock_irqsave(&pc->dvfs.metrics.uid_lock, flags);

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; i++) {
		stats = pc->dvfs.metrics.js_uid_stats[i];
		if (stats && stats->period_start != event_time) {
			WARN_ON(stats->period_start == 0);
			stats->tis_stats[pc->dvfs.level].time_total +=
				(event_time - stats->period_start);
			stats->period_start = event_time;
		}
	}

	spin_unlock_irqrestore(&pc->dvfs.metrics.uid_lock, flags);
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
				gpu_dvfs_metrics_uid_level_change(kbdev, curr);
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
 * gpu_dvfs_metrics_job_start() - Notification of when an atom starts on the GPU
 *
 * @atom: The &struct kbase_jd_atom that has just been submitted to the GPU.
 *
 * This function is called when an atom is submitted to the GPU by way of writing to the
 * JSn_HEAD_NEXTn register.
 *
 * Context: May be in IRQ context, assumes that the hwaccess lock is held, and in turn takes and
 *          releases the metrics UID spin lock.
 */
void gpu_dvfs_metrics_job_start(struct kbase_jd_atom *atom)
{
	struct kbase_device *kbdev = atom->kctx->kbdev;
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_metrics_uid_stats *stats = atom->kctx->platform_data;
	int js = atom->slot_nr;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	spin_lock(&pc->dvfs.metrics.uid_lock);

	if (stats->atoms_in_flight == 0) {
		/* This is the start of a new period */
		WARN_ON(stats->period_start != 0);
		stats->period_start = ktime_get_ns();
	}

	stats->atoms_in_flight++;
	pc->dvfs.metrics.js_uid_stats[js] = stats;

	spin_unlock(&pc->dvfs.metrics.uid_lock);
}

/**
 * gpu_dvfs_metrics_job_start() - Notification of when an atom stops running on the GPU
 *
 * @atom: The &struct kbase_jd_atom that has just stopped running on the GPU
 *
 * This function is called when an atom is no longer running on the GPU, either due to successful
 * completion, failure, preemption, or GPU reset.
 *
 * Context: May be in IRQ context, assumes that the hwaccess lock is held, and in turn takes and
 *          releases the metrics UID spin lock.
 */
void gpu_dvfs_metrics_job_end(struct kbase_jd_atom *atom)
{
	struct kbase_device *kbdev = atom->kctx->kbdev;
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_metrics_uid_stats *stats = atom->kctx->platform_data;
	int js = atom->slot_nr;
	u64 curr = ktime_get_ns();

	lockdep_assert_held(&kbdev->hwaccess_lock);
	spin_lock(&pc->dvfs.metrics.uid_lock);

	WARN_ON(stats->period_start == 0);
	WARN_ON(stats->atoms_in_flight == 0);

	stats->atoms_in_flight--;
	stats->tis_stats[pc->dvfs.level].time_total += (curr - stats->period_start);

	if (stats->atoms_in_flight == 0)
		/* This is the end of a period */
		stats->period_start = 0;
	else
		stats->period_start = curr;

	pc->dvfs.metrics.js_uid_stats[js] = NULL;

	spin_unlock(&pc->dvfs.metrics.uid_lock);
}

/**
 * gpu_dvfs_create_uid_stats() - Allocates and initializes a per-UID stats block
 *
 * @pc:  The &struct pixel_context that is requesting the stats block.
 * @uid: The &kuid_t corresponding to the application that will be tracked.
 *
 * Return: Returns a pointer to the per-UID stats block, or an ERRPTR on failure.
 */
static struct gpu_dvfs_metrics_uid_stats *gpu_dvfs_create_uid_stats(struct pixel_context *pc,
	kuid_t uid)
{
	struct gpu_dvfs_metrics_uid_stats *ret;

	ret = kzalloc(sizeof(struct gpu_dvfs_metrics_uid_stats), GFP_KERNEL);
	if (ret == NULL)
		return ERR_PTR(-ENOMEM);

	ret->tis_stats = kzalloc(sizeof(struct gpu_dvfs_opp_metrics) * pc->dvfs.table_size,
		GFP_KERNEL);
	if (ret->tis_stats == NULL) {
		kfree(ret);
		return ERR_PTR(-ENOMEM);
	}

	ret->uid = uid;

	return ret;
}

/**
 * gpu_dvfs_destroy_uid_stats() - Destroys a previously initializes per-UID stats block
 *
 * @stats:  The &struct gpu_dvfs_metrics_uid_stats that is to be destroyed
 *
 */
static void gpu_dvfs_destroy_uid_stats(struct gpu_dvfs_metrics_uid_stats *stats)
{
	kfree(stats->tis_stats);
	kfree(stats);
}

/* Kernel context callback management */

/**
 * gpu_dvfs_kctx_init() - Called when a kernel context is created
 *
 * @kctx: The &struct kbase_context that is being initialized
 *
 * This function is called when the GPU driver is initializing a new kernel context. This event is
 * used to set up data structures that will be used to track this context's usage of the GPU to
 * enable tracking of GPU usage on a per-UID basis.
 *
 * If data for the calling UID has already been created during the life of the GPU kernel driver,
 * the previously allocated stats structure is used allowing for persistent metrics for that UID.
 * If the UID has not been seen before, a new stats block is created and inserted into the list of
 * per-UID stats such that the list is sorted by UID.
 *
 * Return: Returns 0 on success, or an error code on failure.
 */
int gpu_dvfs_kctx_init(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct pixel_context *pc = kbdev->platform_context;

	struct task_struct *task;
	kuid_t uid;

	struct gpu_dvfs_metrics_uid_stats *entry, *stats;
	unsigned long flags;
	int ret = 0;

	lockdep_assert_held(&kctx->kbdev->kctx_list_lock);

	/* Get UID from task_struct */
	task = get_pid_task(find_get_pid(kctx->kprcs->tgid), PIDTYPE_TGID);
	uid = task->cred->uid;

	spin_lock_irqsave(&pc->dvfs.metrics.uid_lock, flags);

	/*
	 * Search through the UIDs we have encountered previously, and either return an already
	 * created stats block, or create one and insert it such that the linked list is sorted
	 * by UID.
	 */
	stats = NULL;
	list_for_each_entry(entry, &pc->dvfs.metrics.uid_stats_list, uid_list_link) {
		if (uid_eq(entry->uid, uid)) {
			/* Already created */
			stats = entry;
			break;
		} else if (uid_gt(entry->uid, uid)) {
			/* Create and insert in list */
			stats = gpu_dvfs_create_uid_stats(pc, uid);
			if (IS_ERR(stats)) {
				ret = PTR_ERR(stats);
				goto done;
			}

			list_add_tail(&stats->uid_list_link, &entry->uid_list_link);

			break;
		}
	}

	/* Create and append to the end of the list */
	if (stats == NULL) {
		stats = gpu_dvfs_create_uid_stats(pc, uid);
		if (IS_ERR(stats)) {
			ret = PTR_ERR(stats);
			goto done;
		}

		list_add_tail(&stats->uid_list_link, &pc->dvfs.metrics.uid_stats_list);
	}

	stats->active_kctx_count++;

	/* Store a direct link in the kctx */
	kctx->platform_data = stats;

done:
	spin_unlock_irqrestore(&pc->dvfs.metrics.uid_lock, flags);
	return ret;
}

/**
 * gpu_dvfs_kctx_init() - Called when a kernel context is terminated
 *
 * @kctx: The &struct kbase_context that is being terminated
 *
 * Since per-UID stats are retained for as long as the GPU kernel driver is loaded, we don't delete
 * the stats block, we only update that there is one fewer kernel context attached to it.
 */
void gpu_dvfs_kctx_term(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_metrics_uid_stats *stats = kctx->platform_data;
	unsigned long flags;

	spin_lock_irqsave(&pc->dvfs.metrics.uid_lock, flags);
	stats->active_kctx_count--;
	WARN_ON(stats->active_kctx_count < 0);
	spin_unlock_irqrestore(&pc->dvfs.metrics.uid_lock, flags);
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

	spin_lock_init(&pc->dvfs.metrics.uid_lock);

	pc->dvfs.metrics.last_time = ktime_get_ns();
	pc->dvfs.metrics.last_power_state = gpu_power_status(kbdev);

	pc->dvfs.table[pc->dvfs.level].metrics.entry_count++;
	pc->dvfs.table[pc->dvfs.level].metrics.time_last_entry =
		pc->dvfs.metrics.last_time;

	mutex_unlock(&pc->dvfs.lock);

	/* Initialize per-UID metrics */
	INIT_LIST_HEAD(&pc->dvfs.metrics.uid_stats_list);

	return 0;
}

/**
 * gpu_dvfs_metrics_term() - Terminates DVFS metrics
 *
 * @kbdev: The &struct kbase_device for the GPU.
 */
void gpu_dvfs_metrics_term(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	struct gpu_dvfs_metrics_uid_stats *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &pc->dvfs.metrics.uid_stats_list, uid_list_link) {
		list_del(&entry->uid_list_link);
		gpu_dvfs_destroy_uid_stats(entry);
	}

}
