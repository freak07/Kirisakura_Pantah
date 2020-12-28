/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

#ifndef _PIXEL_GPU_DVFS_H_
#define _PIXEL_GPU_DVFS_H_

/* Governor */

/**
 * typedef gpu_dvfs_governor_logic_fn - Determines the next level based on utilization.
 *
 * @kbdev: The &struct kbase_device of the GPU.
 * @util:  The integer utilization percentage the GPU is running at.
 *
 * This function is not expected to take any clock limits into consideration when
 * recommending the next level.
 *
 * Context: Expects the DVFS lock to be held by the caller.
 *
 * Return: The index of the next recommended level.
 */
typedef int (*gpu_dvfs_governor_logic_fn)(struct kbase_device *kbdev, int util);

/**
 * enum gpu_dvfs_governor_type - Pixel GPU DVFS governor.
 *
 * This enum stores the list of available DVFS governors for the GPU. High-level.
 * documentation for each governor should be provided here.
 */
enum gpu_dvfs_governor_type {
	/**
	 * @GPU_DVFS_GOVERNOR_BASIC: A very simple GPU DVFS governor.
	 *
	 * The basic governor uses incoming GPU utilization data to determine
	 * whether the GPU should change levels.
	 *
	 * If the GPU's utilization is higher than the level's maximum threshold
	 * it will recommend a move to a higher throughput level.
	 *
	 * If the GPU's utilization is lower than the level's minimum threshold,
	 * and remains lower for a number of ticks set by the level's hysteresis
	 * value, then it will recommend a move to a lower throughput level.
	 */
	GPU_DVFS_GOVERNOR_BASIC = 0,
	/* Insert new governors here */
	GPU_DVFS_GOVERNOR_COUNT,
	GPU_DVFS_GOVERNOR_INVALID,
};

/**
 * struct gpu_dvfs_governor_info - Data for a Pixel GPU DVFS governor.
 *
 * @id:       A unique, numerical identifier for the governor.
 * @name:     A human readable name for the governor.
 * @evaluate: A function pointer to the governor's evaluate function. See
 *            &gpu_dvfs_governor_logic_fn.
 */
struct gpu_dvfs_governor_info {
	enum gpu_dvfs_governor_type id;
	const char *name;
	gpu_dvfs_governor_logic_fn evaluate;
};

int gpu_dvfs_governor_get_next_level(struct kbase_device *kbdev, int util);
int gpu_dvfs_governor_set_governor(struct kbase_device *kbdev, enum gpu_dvfs_governor_type gov);
enum gpu_dvfs_governor_type gpu_dvfs_governor_get_id(const char *name);
ssize_t gpu_dvfs_governor_print_available(char *buf, ssize_t size);
ssize_t gpu_dvfs_governor_print_curr(struct kbase_device *kbdev, char *buf, ssize_t size);
int gpu_dvfs_governor_init(struct kbase_device *kbdev);
void gpu_dvfs_governor_term(struct kbase_device *kbdev);

/* Metrics */

/**
 * struct gpu_dvfs_metrics_uid_stats - Stores time in state data for a UID
 *
 * @uid_list_link:     Node into list of per-UID stats.
 * @active_kctx_count: Count of active kernel contexts operating under this UID.
 * @uid:               The UID for this stats block.
 * @atoms_in_flight:   The number of atoms currently executing on the GPU from this UID.
 * @period_start:      The time (in nanoseconds) that the current active period for this UID began.
 * tis_stats:          &struct gpu_dvfs_opp_metrics block storing time in state data for this UID
 */
struct gpu_dvfs_metrics_uid_stats {
	struct list_head uid_list_link;
	int active_kctx_count;
	kuid_t uid;
	int atoms_in_flight;
	u64 period_start;
	struct gpu_dvfs_opp_metrics *tis_stats;
};

void gpu_dvfs_metrics_trace_clock(struct kbase_device *kbdev, bool power_on);
void gpu_dvfs_metrics_update(struct kbase_device *kbdev, int next_level, bool power_state);
void gpu_dvfs_metrics_job_start(struct kbase_jd_atom *atom);
void gpu_dvfs_metrics_job_end(struct kbase_jd_atom *atom);
int gpu_dvfs_metrics_init(struct kbase_device *kbdev);
void gpu_dvfs_metrics_term(struct kbase_device *kbdev);

/* QOS */

#ifdef CONFIG_MALI_PIXEL_GPU_QOS
void gpu_dvfs_qos_set(struct kbase_device *kbdev, int level);
void gpu_dvfs_qos_reset(struct kbase_device *kbdev);
int gpu_dvfs_qos_init(struct kbase_device *kbdev);
void gpu_dvfs_qos_term(struct kbase_device *kbdev);
#endif /* CONFIG_MALI_PIXEL_GPU_QOS */

/* Thermal */

#ifdef CONFIG_MALI_PIXEL_GPU_THERMAL
int gpu_tmu_init(struct kbase_device *kbdev);
void gpu_tmu_term(struct kbase_device *kbdev);
#endif /* CONFIG_MALI_PIXEL_GPU_THERMAL*/

/* Common */

void gpu_dvfs_update_level_locks(struct kbase_device *kbdev);

#endif /* _PIXEL_GPU_DVFS_H_ */
