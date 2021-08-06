// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

/* Linux includes */
#include <linux/of_device.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#include <linux/pm_domain.h>

/* SOC includes */
#include <soc/google/exynos-pmu-if.h>
#include <soc/google/exynos-pd.h>
#include <soc/google/cal-if.h>

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_control.h"
#include "pixel_gpu_trace.h"

/**
 * gpu_pm_power_on_cores() - Powers on the GPU shader cores.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Powers on the shader cores and issues trace points and events.
 *
 * Context: Process context.
 */
static void gpu_pm_power_on_cores(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	u64 start_ns = ktime_get_ns();

	mutex_lock(&pc->dvfs.lock);
	mutex_lock(&pc->pm.domain->access_lock);

	/* We restore to dvfs.level rather than dvfs.level_target because this is just an on/off
	 * switch and that's the frequency we were at when we turned shaders off; the dvfs system
	 * will change to dvfs.level_target (possibly after re-evaluating it) as part of its normal
	 * operation.
	 */
	cal_dfs_set_rate(pc->dvfs.clks[GPU_DVFS_CLK_SHADERS].cal_id,
			 pc->dvfs.table[pc->dvfs.level].clk[GPU_DVFS_CLK_SHADERS]);

	mutex_unlock(&pc->pm.domain->access_lock);
	mutex_unlock(&pc->dvfs.lock);

	trace_gpu_power_state(ktime_get_ns() - start_ns,
		GPU_POWER_LEVEL_GLOBAL, GPU_POWER_LEVEL_STACKS);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	gpu_dvfs_event_power_on(kbdev);
#endif
}

/**
 * gpu_pm_power_off_cores() - Powers off the GPU shader cores.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Powers off the shader cores and issues trace points and events.
 *
 * Context: Process context.
 */
static void gpu_pm_power_off_cores(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	u64 start_ns = ktime_get_ns();

	mutex_lock(&pc->dvfs.lock);
	mutex_lock(&pc->pm.domain->access_lock);

	/* Setting a frequency of 0 is a backdoor request to ACPM to power off the shader cores. */
	cal_dfs_set_rate(pc->dvfs.clks[GPU_DVFS_CLK_SHADERS].cal_id, 0);

	mutex_unlock(&pc->pm.domain->access_lock);
	mutex_unlock(&pc->dvfs.lock);

	trace_gpu_power_state(ktime_get_ns() - start_ns,
		GPU_POWER_LEVEL_STACKS, GPU_POWER_LEVEL_GLOBAL);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	gpu_dvfs_event_power_off(kbdev);
#endif
}

/**
 * gpu_pm_callback_power_on() - Called when the GPU needs to be powered on.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it identifies that the GPU is about to
 * become active.
 *
 * Since we currently don't power anything off between suspend and resume, this function is
 * a no-op.
 *
 * Return: Returns 0 indicating GPU state has not been lost since the last power off since the most
 *         recent resume.
 */
static int gpu_pm_callback_power_on(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "%s\n", __func__);

	return 0;
}

/**
 * gpu_pm_callback_power_off() - Called when the GPU is idle and may be powered off
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it identifies that the GPU is idle and may
 * be powered off.
 *
 * We currently don't take advantage of this opportunity.
 */
static void gpu_pm_callback_power_off(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "%s\n", __func__);
}

/**
 * gpu_pm_callback_power_resume() - Called when the system is resuming from suspend
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it is notified that the system is resuming
 * from suspend. The driver does not require that the GPU is powered on immediately.
 *
 * Since our current policy is to keep the GPU fully powered on whenever we're not in suspend, we
 * do restore power to both the frontend and shader cores here.
 */
static void gpu_pm_callback_power_resume(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	int ret;

	dev_dbg(kbdev->dev, "%s\n", __func__);

	if ((ret = exynos_pd_power_on(pc->pm.domain)) < 0)
		dev_warn(kbdev->dev, "failed to power on domain: %d\n", ret);

	gpu_pm_power_on_cores(kbdev);

#if IS_ENABLED(CONFIG_GOOGLE_BCL)
	if (pc->pm.bcl_dev)
		google_init_gpu_ratio(pc->pm.bcl_dev);
#endif
}

/**
 * gpu_pm_callback_power_suspend() - Called when the system is going to suspend
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it is notified that the system is about to
 * suspend and the GPU needs to be powered down.
 *
 * We power off both the shader cores (stateless) and the front-end and memory system (stateful).
 * The core Mali driver doesn't guarantee that &gpu_pm_callback_power_off will be called first, so
 * if we ever do something there (e.g. power off shader cores) we'll also need to ensure it is done
 * here.
 */
static void gpu_pm_callback_power_suspend(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	int ret;

	dev_dbg(kbdev->dev, "%s\n", __func__);

	gpu_pm_power_off_cores(kbdev);

	if ((ret = exynos_pd_power_off(pc->pm.domain)) < 0)
		dev_warn(kbdev->dev, "failed to power off domain: %d\n", ret);
}

/*
 * struct pm_callbacks - Callbacks for linking to core Mali KMD power management
 *
 * Callbacks linking power management code in the core Mali driver with code in The Pixel
 * integration. For more information on the fields below, see the documentation for each function
 * assigned below, and &struct kbase_pm_callback_conf.
 *
 * Currently we keep the entire GPU powered except during system suspend. A future change will
 * power off the shader cores when the GPU is idle (in response to &power_off_callback), and power
 * them on again just before the GPU becomes active (in response to &power_on_callback).
 *
 * The callback for &power_suspend_callback turns off the frontend (and, currently, the shader
 * cores) just before the system enters suspend. That process is reversed during resume in
 * &power_resume_callback.
 *
 * We're not currently using runtime PM, so we leave all of those callbacks NULL. Similarly, we
 * don't need custom soft reset handling beyond what the core driver does, so that callback is
 * also NULL.
 */
struct kbase_pm_callback_conf pm_callbacks = {
	.power_off_callback = gpu_pm_callback_power_off,
	.power_on_callback = gpu_pm_callback_power_on,
	.power_suspend_callback = gpu_pm_callback_power_suspend,
	.power_resume_callback = gpu_pm_callback_power_resume,
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_off_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_idle_callback = NULL,
	.soft_reset_callback = NULL,
};

/**
 * gpu_pm_get_power_state() - Returns the current power state of the GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context. Takes and releases the power domain access lock.
 *
 * Return: Returns true if the GPU is powered on, false if not.
 */
bool gpu_pm_get_power_state(struct kbase_device *kbdev)
{
	bool ret;
	unsigned int val = 0;
	struct pixel_context *pc = kbdev->platform_context;

	mutex_lock(&pc->pm.domain->access_lock);
	exynos_pmu_read(pc->pm.status_reg_offset, &val);
	ret = ((val & pc->pm.status_local_power_mask) == pc->pm.status_local_power_mask);
	mutex_unlock(&pc->pm.domain->access_lock);

	return ret;
}


/**
 * gpu_pm_init() - Initializes power management control for a GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: An error code, or 0 on success.
 */
int gpu_pm_init(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	struct device_node *np = kbdev->dev->of_node;
	const char *g3d_power_domain_name;
	int ret = 0;

	if (of_property_read_u32(np, "gpu_pm_autosuspend_delay", &pc->pm.autosuspend_delay)) {
		pc->pm.autosuspend_delay = AUTO_SUSPEND_DELAY;
		dev_info(kbdev->dev, "autosuspend delay not set in DT, using default of %dms\n",
			AUTO_SUSPEND_DELAY);
	}

	if (of_property_read_u32(np, "gpu_pmu_status_reg_offset", &pc->pm.status_reg_offset)) {
		dev_err(kbdev->dev, "PMU status register offset not set in DT\n");
		ret = -EINVAL;
		goto error;
	}

	if (of_property_read_u32(np, "gpu_pmu_status_local_pwr_mask",
		&pc->pm.status_local_power_mask)) {
		dev_err(kbdev->dev, "PMU status register power mask not set in DT\n");
		ret = -EINVAL;
		goto error;
	}

	if (of_property_read_string(np, "g3d_genpd_name", &g3d_power_domain_name)) {
		dev_err(kbdev->dev, "GPU power domain name not set in DT\n");
		ret = -EINVAL;
		goto error;
	}

	pc->pm.domain = exynos_pd_lookup_name(g3d_power_domain_name);
	if (pc->pm.domain == NULL) {
		dev_err(kbdev->dev, "Failed to find GPU power domain '%s'\n",
			g3d_power_domain_name);
		return -ENODEV;
	}
	exynos_pd_power_on(pc->pm.domain);

#if IS_ENABLED(CONFIG_GOOGLE_BCL)
	pc->pm.bcl_dev = google_retrieve_bcl_handle();
#endif

	return 0;

error:
	gpu_pm_term(kbdev);
	return ret;
}

/**
 * gpu_pm_term() - Terminates power control for a GPU
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This function is called from the error-handling path of &gpu_pm_init, so must handle a
 * partially-initialized device.
 */
void gpu_pm_term(struct kbase_device *kbdev)
{
}
