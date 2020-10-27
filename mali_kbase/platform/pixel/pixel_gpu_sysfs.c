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
#include "pixel_gpu_control.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_dvfs.h"

/* Helper functions */

/**
 * get_level_from_clock() - Helper function to get the level index corresponding to a clock.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 * @clock: The frequency (in kHz) of the GPU Top Level clock to get the level from.
 *
 * Return: The level corresponding to @clock, -1 on failure.
 */
static int get_level_from_clock(struct kbase_device *kbdev, int clock)
{
	struct pixel_context *pc = kbdev->platform_context;
	int i;

	for (i = 0; i < pc->dvfs.table_size; i++)
		if (pc->dvfs.table[i].clk0 == clock)
			return i;

	return -1;
}

/* Custom attributes */

static ssize_t gpu_log_level_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	int ret = 0;

	if (!pc)
		return -ENODEV;

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "LOG_DISABLED %s\n"
			 "LOG_DEBUG %s\n"
			 "LOG_INFO  %s\n"
			 "LOG_WARN  %s\n"
			 "LOG_ERROR %s\n",
			 (pc->gpu_log_level == LOG_DISABLED ? "<" : ""),
			 (pc->gpu_log_level == LOG_DEBUG ? "<" : ""),
			 (pc->gpu_log_level == LOG_INFO ? "<" : ""),
			 (pc->gpu_log_level == LOG_WARN ? "<" : ""),
			 (pc->gpu_log_level == LOG_ERROR ? "<" : ""));

	return ret;

}

static ssize_t gpu_log_level_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;
	enum gpu_log_level log_level;

	if (!pc)
		return -ENODEV;

	if (sysfs_streq(buf, "LOG_DISABLED"))
		log_level = LOG_DISABLED;
	else if (sysfs_streq(buf, "LOG_DEBUG"))
		log_level = LOG_DEBUG;
	else if (sysfs_streq(buf, "LOG_INFO"))
		log_level = LOG_INFO;
	else if (sysfs_streq(buf, "LOG_WARN"))
		log_level = LOG_WARN;
	else if (sysfs_streq(buf, "LOG_ERROR"))
		log_level = LOG_ERROR;
	else
		return -EINVAL;

	pc->gpu_log_level = log_level;

	return count;

}

static ssize_t clock_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	/* We use level_target in case the clock has been set while the GPU was powered down */
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
		"Power status             : %s\n"
		"gpu0 clock (top level)   : %d kHz\n"
		"gpu1 clock (shaders)     : %d kHz\n",

		(gpu_power_status(kbdev) ? "on" : "off"),
		pc->dvfs.table[pc->dvfs.level_target].clk0,
		pc->dvfs.table[pc->dvfs.level_target].clk1);

#ifdef CONFIG_MALI_PIXEL_GPU_QOS

#ifdef CONFIG_MALI_PIXEL_GPU_BTS
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
		"GPU Bus Traffic Shaping  :%s\n",
		(pc->dvfs.qos.bts.enabled ? "on" : "off"));
#endif /* CONFIG_MALI_PIXEL_GPU_BTS */

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
		"QOS status               : %s\n"
		" INT min clock           : %d kHz\n"
		" MIF min clock           : %d kHz\n"
		" CPU cluster 0 min clock : %d kHz\n"
		" CPU cluster 1 min clock : %d kHz\n",

		(pc->dvfs.qos.enabled ? "on" : "off"),
		pc->dvfs.table[pc->dvfs.level_target].qos.int_min,
		pc->dvfs.table[pc->dvfs.level_target].qos.mif_min,
		pc->dvfs.table[pc->dvfs.level_target].qos.cpu0_min,
		pc->dvfs.table[pc->dvfs.level_target].qos.cpu1_min);

	if (pc->dvfs.table[pc->dvfs.level_target].qos.cpu2_max == CPU_FREQ_MAX)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			" CPU cluster 2 max clock : (no limit)\n");
	else
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			" CPU cluster 2 max clock : %d kHz\n",
			pc->dvfs.table[pc->dvfs.level_target].qos.cpu2_max);

#endif /* CONFIG_MALI_PIXEL_GPU_QOS */

#ifdef CONFIG_MALI_PIXEL_GPU_THERMAL

	if (pc->dvfs.tmu.level_limit >= 0 && pc->dvfs.tmu.level_limit < pc->dvfs.table_size)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Thermal level limit:\n"
			" gpu0 clock (top level)   : %d kHz\n"
			" gpu1 clock (shaders)     : %d kHz\n",
			pc->dvfs.table[pc->dvfs.tmu.level_limit].clk0,
			pc->dvfs.table[pc->dvfs.tmu.level_limit].clk1);

#endif /* CONFIG_MALI_PIXEL_GPU_THERMAL */

	return ret;
}

static ssize_t dvfs_table_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret = 0;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
		" gpu_0   gpu_0   gpu_1   gpu_1  util util hyste- int_clk  mif_clk cpu0_clk cpu1_clk cpu2_clk\n"
		"  clk     vol     clk     vol   min  max  resis    min      min     min      min      limit\n"
		"------- ------- ------- ------- ---- ---- ------ ------- -------- -------- -------- --------\n");

	for (i = pc->dvfs.level_max; i <= pc->dvfs.level_min; i++) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"%7d %7d %7d %7d %4d %4d %6d %7d %8d %8d %8d ",
			pc->dvfs.table[i].clk0,
			pc->dvfs.table[i].vol0,
			pc->dvfs.table[i].clk1,
			pc->dvfs.table[i].vol1,
			pc->dvfs.table[i].util_min,
			pc->dvfs.table[i].util_max,
			pc->dvfs.table[i].hysteresis,
			pc->dvfs.table[i].qos.int_min,
			pc->dvfs.table[i].qos.mif_min,
			pc->dvfs.table[i].qos.cpu0_min,
			pc->dvfs.table[i].qos.cpu1_min);

		if (pc->dvfs.table[i].qos.cpu2_max == CPU_FREQ_MAX)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%8s\n", "none");
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%8d\n",
				pc->dvfs.table[i].qos.cpu2_max);
	}

	return ret;
}

static ssize_t power_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret = 0;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	/* First trigger an update */
	mutex_lock(&pc->dvfs.lock);
	gpu_dvfs_metrics_update(kbdev, pc->dvfs.level, gpu_power_status(kbdev));
	mutex_unlock(&pc->dvfs.lock);

	ret = scnprintf(buf + ret, PAGE_SIZE - ret, "DVFS stats: (times in ms)\n");

	for (i = 0; i < pc->dvfs.table_size; i++) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"%d:\n\ttotal_time = %llu\n\tcount = %d\n\tlast_entry_time = %llu\n",
			pc->dvfs.table[i].clk0,
			pc->dvfs.table[i].metrics.time_total / NSEC_PER_MSEC,
			pc->dvfs.table[i].metrics.entry_count,
			pc->dvfs.table[i].metrics.time_last_entry / NSEC_PER_MSEC);
	}


	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "Summary stats: (times in ms)\n");

	ret += scnprintf(
		buf + ret, PAGE_SIZE - ret,
		"ON:\n\ttotal_time = %llu\n\tcount = %d\n\tlast_entry_time = %llu\n",
		pc->pm.power_on_metrics.time_total / NSEC_PER_MSEC,
		pc->pm.power_on_metrics.entry_count,
		pc->pm.power_on_metrics.time_last_entry / NSEC_PER_MSEC);

	ret += scnprintf(
		buf + ret, PAGE_SIZE - ret,
		"OFF:\n\ttotal_time = %llu\n\tcount = %d\n\tlast_entry_time = %llu\n",
		pc->pm.power_off_metrics.time_total / NSEC_PER_MSEC,
		pc->pm.power_off_metrics.entry_count,
		pc->pm.power_off_metrics.time_last_entry / NSEC_PER_MSEC);

	return ret;
}

static ssize_t tmu_max_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.tmu.level_limit].clk0);
}

DEVICE_ATTR_RW(gpu_log_level);
DEVICE_ATTR_RO(clock_info);
DEVICE_ATTR_RO(dvfs_table);
DEVICE_ATTR_RO(power_stats);
DEVICE_ATTR_RO(tmu_max_freq);


/* devfreq-like attributes */

static ssize_t cur_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	/* We use level_target in case the clock has been set while the GPU was powered down */
	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.level_target].clk0);
}

static ssize_t available_frequencies_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	int i;
	ssize_t ret = 0;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	for (i = 0; i < pc->dvfs.table_size; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%d ", pc->dvfs.table[i].clk0);

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	return ret;
}

static ssize_t max_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.level_max].clk0);
}

static ssize_t min_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.level_min].clk0);
}

static ssize_t scaling_max_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.level_scaling_max].clk0);
}

static ssize_t scaling_max_freq_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int level, ret;
	unsigned int clock;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &clock);
	if (ret)
		return -EINVAL;

	level = get_level_from_clock(kbdev, clock);
	if (level < 0)
		return -EINVAL;

	mutex_lock(&pc->dvfs.lock);
	pc->dvfs.level_scaling_max = level;
	pc->dvfs.level_scaling_min = max(level, pc->dvfs.level_scaling_min);
	gpu_dvfs_update_level_locks(kbdev);
	mutex_unlock(&pc->dvfs.lock);

	return count;
}

static ssize_t scaling_min_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pc->dvfs.table[pc->dvfs.level_scaling_min].clk0);
}

static ssize_t scaling_min_freq_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret, level;
	unsigned int clock;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &clock);
	if (ret)
		return -EINVAL;

	level = get_level_from_clock(kbdev, clock);
	if (level < 0)
		return -EINVAL;

	mutex_lock(&pc->dvfs.lock);
	pc->dvfs.level_scaling_min = level;
	pc->dvfs.level_scaling_max = min(level, pc->dvfs.level_scaling_max);
	gpu_dvfs_update_level_locks(kbdev);
	mutex_unlock(&pc->dvfs.lock);

	return count;
}

static ssize_t time_in_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t ret = 0;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	/* First trigger an update */
	mutex_lock(&pc->dvfs.lock);
	gpu_dvfs_metrics_update(kbdev, pc->dvfs.level, gpu_power_status(kbdev));
	mutex_unlock(&pc->dvfs.lock);

	for (i = pc->dvfs.level_max; i <= pc->dvfs.level_min; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%8d %9d\n", pc->dvfs.table[i].clk0,
			(u32)(pc->dvfs.table[i].metrics.time_total / NSEC_PER_MSEC));

	return ret;
}

static ssize_t available_governors_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	return gpu_dvfs_governor_print_available(buf, PAGE_SIZE);
}

static ssize_t governor_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct kbase_device *kbdev = dev->driver_data;

	return gpu_dvfs_governor_print_curr(kbdev, buf, PAGE_SIZE);
}

static ssize_t governor_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	enum gpu_dvfs_governor_type gov;
	ssize_t ret = count;
	struct kbase_device *kbdev = dev->driver_data;
	struct pixel_context *pc = kbdev->platform_context;

	if (!pc)
		return -ENODEV;

	gov = gpu_dvfs_governor_get_id(buf);

	if (gov == GPU_DVFS_GOVERNOR_INVALID)
		ret = -EINVAL;
	else if (gov != pc->dvfs.governor.curr) {
		mutex_lock(&pc->dvfs.lock);
		if (gpu_dvfs_governor_set_governor(kbdev, gov))
			ret = -EINVAL;
		mutex_unlock(&pc->dvfs.lock);
	}

	return ret;
}


/* Define devfreq-like attributes */
DEVICE_ATTR_RO(available_frequencies);
DEVICE_ATTR_RO(cur_freq);
DEVICE_ATTR_RO(max_freq);
DEVICE_ATTR_RO(min_freq);
DEVICE_ATTR_RW(scaling_max_freq);
DEVICE_ATTR_RW(scaling_min_freq);
DEVICE_ATTR_RO(time_in_state);
DEVICE_ATTR_RO(available_governors);
DEVICE_ATTR_RW(governor);

/* Initialization code */

/*
 * attribs - An array containing all sysfs files for the Pixel GPU sysfs system.
 *
 * This array contains the list of all files that will be set up and removed by the Pixel GPU sysfs
 * system. It allows for more compact initialization and termination code below.
 */
static struct {
	const char *name;
	const struct device_attribute *attr;
} attribs[] = {
	{ "gpu_log_level", &dev_attr_gpu_log_level },
	{ "clock_info", &dev_attr_clock_info },
	{ "dvfs_table", &dev_attr_dvfs_table },
	{ "power_stats", &dev_attr_power_stats },
	{ "tmu_max_freq", &dev_attr_tmu_max_freq },
	{ "available_frequencies", &dev_attr_available_frequencies },
	{ "cur_freq", &dev_attr_cur_freq },
	{ "max_freq", &dev_attr_max_freq },
	{ "min_freq", &dev_attr_min_freq },
	{ "scaling_max_freq", &dev_attr_scaling_max_freq },
	{ "scaling_min_freq", &dev_attr_scaling_min_freq },
	{ "time_in_state", &dev_attr_time_in_state },
	{ "available_governors", &dev_attr_available_governors },
	{ "governor", &dev_attr_governor }
};

/**
 * gpu_sysfs_init() - Initializes the Pixel GPU sysfs system.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: On success, returns 0. -ENOENT if creating a sysfs file results in an error.
 */
int gpu_sysfs_init(struct kbase_device *kbdev)
{
	int i;
	struct device *dev = kbdev->dev;

	for (i = 0; i < ARRAY_SIZE(attribs); i++) {
		if (device_create_file(dev, attribs[i].attr)) {
			GPU_LOG(LOG_ERROR, kbdev, "failed to create sysfs file %s\n",
				attribs[i].name);
			return -ENOENT;
		}
	}

	return 0;
}

/**
 * gpu_sysfs_term() - Terminates the Pixel GPU sysfs system.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 */
void gpu_sysfs_term(struct kbase_device *kbdev)
{
	int i;
	struct device *dev = kbdev->dev;

	for (i = 0; i < ARRAY_SIZE(attribs); i++)
		device_remove_file(dev, attribs[i].attr);
}
