// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
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
#if IS_ENABLED(CONFIG_EXYNOS_PMU_IF)
#include <soc/google/exynos-pmu-if.h>
#endif
#if IS_ENABLED(CONFIG_CAL_IF)
#include <soc/google/cal-if.h>
#endif

/* Mali core includes */
#include <mali_kbase.h>

/* Pixel integration includes */
#include "mali_kbase_config_platform.h"
#include "pixel_gpu_debug.h"
#include "pixel_gpu_control.h"
#include "pixel_gpu_trace.h"

/**
 * gpu_power_on() - Powers on a GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context.
 *
 * Return: If the GPU was powered on in this call returns 1. If the GPU was already
 *         powered on, returns 0. Otherwise returns a negative value to indicate
 *         a failure.
 */
static int gpu_power_on(struct kbase_device *kbdev)
{
	int ret = -1;
	struct pixel_context *pc = kbdev->platform_context;
	u64 start_ns = ktime_get_ns();

	ret = exynos_pd_power_on(pc->pm.domain);

	if (WARN_ON(ret < 0)) {
		GPU_LOG(LOG_WARN, kbdev, "Failed to turn the GPU on\n");
		goto done;
	}

done:
	pc->pm.state_lost = false;

	if (ret == 1) {
		trace_gpu_power_state(ktime_get_ns() - start_ns,
			GPU_POWER_LEVEL_GLOBAL, GPU_POWER_LEVEL_STACKS);
#ifdef CONFIG_MALI_MIDGARD_DVFS
		gpu_dvfs_event_power_on(kbdev);
#endif
	}

	return ret;
}

/**
 * gpu_power_off() - Powers off a GPU.
 *
 * @kbdev:      The &struct kbase_device for the GPU.
 * @state_lost: Indicates whether the GPU state will be lost soon after this power off operation.
 *
 * Context: Process context.
 *
 * Return: If the GPU was powered off in this call, returns 1. If the GPU was already
 *         powered off, returns 0. Otherwise returns a negative value to indicate
 *         a failure.
 */
static int gpu_power_off(struct kbase_device *kbdev, bool state_lost)
{
	int ret = -1;
	struct pixel_context *pc = kbdev->platform_context;
	u64 start_ns = ktime_get_ns();

	ret = exynos_pd_power_off(pc->pm.domain);

	if (WARN_ON(ret < 0)) {
		GPU_LOG(LOG_WARN, kbdev, "Failed to turn the GPU off\n");
		goto done;
	}

done:
	if (state_lost)
		pc->pm.state_lost = true;

	if (ret == 1) {
		trace_gpu_power_state(ktime_get_ns() - start_ns,
			GPU_POWER_LEVEL_STACKS, GPU_POWER_LEVEL_GLOBAL);
#ifdef CONFIG_MALI_MIDGARD_DVFS
		gpu_dvfs_event_power_off(kbdev);
#endif
	}

	return ret;
}

/**
 * pm_callback_power_on() - Called when the GPU needs to be powered on.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it identifies that the
 * GPU is about to become active.
 *
 * Since we are using idle hints to power down the GPU in &pm_callback_power_off
 * we will need to power up the GPU when we receive this callback.
 *
 * Return: If GPU state has been lost, 1 is returned. Otherwise 0 is returned.
 */
static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int error;
	struct pixel_context *pc = kbdev->platform_context;
	int ret = (pc->pm.state_lost ? 1 : 0);

	GPU_LOG(LOG_DEBUG, kbdev, "%s\n", __func__);

	error = pm_runtime_get_sync(kbdev->dev);

	/* pm_runtime_get_sync() returns 1 if the GPU was already active, i.e. powered on. In this
	 * case, we must not have lost state since if the GPU has been on, then the GPU state (which
	 * is retained for as long as the AP doesn't suspend) should not have been lost.
	 */
	WARN_ON(error == 1 && pc->pm.state_lost == true);

	gpu_power_on(kbdev);

	return ret;
}

/**
 * pm_callback_power_off() - Called when the GPU needs to be powered off.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it identifies that the
 * GPU is idle and may be powered off.
 *
 * We take this opportunity to power down the GPU to allow for intra-frame
 * power downs that save power.
 */
static void pm_callback_power_off(struct kbase_device *kbdev)
{
	GPU_LOG(LOG_DEBUG, kbdev, "%s\n", __func__);

	if (gpu_power_off(kbdev, false)) {
		/* If the GPU was just powered off, we update the run-time power management
		 * counters.
		 */
		pm_runtime_mark_last_busy(kbdev->dev);
		pm_runtime_put_autosuspend(kbdev->dev);
	}
}

/**
 * pm_callback_power_suspend() - Called when the system is going to suspend
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is called by the core Mali driver when it is notified that the system is
 * about to suspend and the GPU needs to be powered down.
 *
 * The GPU comprises 3 power domains:
 *
 *   1. the Job Manager,
 *   2. the top level (aka core group) cpmprising the GPU's tiler, MMU and L2 cache subsystem, and
 *   3. the shader cores.
 *
 * GPU state is stored in the first power domain, the Job Manager. The GPU is wired such that the
 * Job Manager is powered as long as the SOC does not go into suspend. All calls to power the GPU
 * on and off in this file only affect the 2nd and 3rd power domains above and so do not affect
 * GPU state retention.
 *
 * This callback is called when the SOC is about to suspend which will result in GPU state being
 * lost. As such, we need to power down the GPU just as is done in &pm_callback_power_off, but also
 * record that state will be lost. Logging the GPU state in this way enables an optimization where
 * GPU state is only reconstructed if necessary when the GPU is powered on by &pm_callback_power_on.
 * This saves CPU cycles and reduces power on latency.
 *
 * As the core Mali driver doesn't guarantee that &pm_callback_power_off will be called as well,
 * all operations made in that function are made in this callback too.
 */
static void pm_callback_power_suspend(struct kbase_device *kbdev)
{
	GPU_LOG(LOG_DEBUG, kbdev, "%s\n", __func__);

	if (gpu_power_off(kbdev, true)) {
		/* If the GPU was just powered off, we update the run-time power management
		 * counters.
		 */
		pm_runtime_mark_last_busy(kbdev->dev);
		pm_runtime_put_autosuspend(kbdev->dev);
	}
}

#ifdef KBASE_PM_RUNTIME

/**
 * pm_callback_power_runtime_init() - Initialize runtime power management.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is made by the core Mali driver at the point where runtime
 * power management is being initialized early on in the probe of the Mali device.
 * We use it to set the autosuspend delay time in ms that we require for our
 * integration.
 *
 * Return: Returns 0 on success, or an error code on failure.
 */
static int pm_callback_power_runtime_init(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;

	GPU_LOG(LOG_DEBUG, kbdev, "%s\n", __func__);

	pm_runtime_set_autosuspend_delay(kbdev->dev, pc->pm.autosuspend_delay);
	pm_runtime_use_autosuspend(kbdev->dev);

	pm_runtime_set_active(kbdev->dev);
	pm_runtime_enable(kbdev->dev);

	if (!pm_runtime_enabled(kbdev->dev)) {
		GPU_LOG(LOG_WARN, kbdev, "pm_runtime not enabled\n");
		return -ENOSYS;
	}

	return 0;
}

/**
 * kbase_device_runtime_term() - Initialize runtime power management.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * This callback is made via the core Mali driver at the point where runtime
 * power management needs to be de-initialized. Currently this only happens if
 * the device probe fails at a point after which runtime power management has
 * been initialized.
 */
static void pm_callback_power_runtime_term(struct kbase_device *kbdev)
{
	GPU_LOG(LOG_DEBUG, kbdev, "%s\n", __func__);
	pm_runtime_disable(kbdev->dev);
}

#endif /* KBASE_PM_RUNTIME */

/*
 * Callbacks linking power management code in the core Mali driver with code in
 * The Pixel integration. For more information on the fields below, see the
 * documentation for each function assigned below, and &struct kbase_pm_callback_conf.
 *
 * Currently we power down the GPU when the core Mali driver indicates that the
 * GPU is idle. This is indicated by the core Mali driver via &power_off_callback
 * and actioned in this integration via &pm_callback_power_off. Similarly, the
 * GPU is powered on in the mirror callback &power_on_callback and actioned by
 * &pm_callback_power_on.
 *
 * We also provide a callback for &power_suspend_callback since this call is made
 * when the system is going to suspend which will result in the GPU state being lost.
 * We need to log this so that when the GPU comes on again we can indidcate to the
 * core Mali driver that the GPU state needs to be reconstructed. See the documentation
 * for &pm_callback_power_suspend for more information.
 *
 * Since all power operations are handled in the most aggressive manner, the more
 * relaxed power management operations are not needed. As such, &power_resume_callback,
 * &power_runtime_off_callback and &power_runtime_on_callback are all set to NULL.
 * Should any additional action be required during these events (for example, disabling
 * clocks but not powering down the GPU) these callbacks should point to functions
 * that perform those actions.
 *
 * We set &power_runtime_idle_callback to be NULL as the default operations done
 * by the core Mali driver are what we would do anyway.
 *
 * Finally, we set &soft_reset_callback to NULL as we do not need to perform a custom
 * soft reset, and can rely on this being handled in the default way by the core
 * Mali driver.
 */
struct kbase_pm_callback_conf pm_callbacks = {
	.power_off_callback = pm_callback_power_off,
	.power_on_callback = pm_callback_power_on,
	.power_suspend_callback = pm_callback_power_suspend,
	.power_resume_callback = NULL,
#ifdef KBASE_PM_RUNTIME
	.power_runtime_init_callback = pm_callback_power_runtime_init,
	.power_runtime_term_callback = pm_callback_power_runtime_term,
	.power_runtime_off_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_idle_callback = NULL,
#else /* KBASE_PM_RUNTIME */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_off_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_idle_callback = NULL,
#endif /* KBASE_PM_RUNTIME */
	.soft_reset_callback = NULL
};

/**
 * gpu_get_pm_domain() - Find the GPU's power domain.
 *
 * @g3d_genpd_name: A string containing the name of the power domain
 *
 * Searches through the available power domains in device tree for one that
 * matched @g3d_genpd_name and returns if if found.
 *
 * Return: A pointer to the power domain if found, NULL if not found.
 */
static struct exynos_pm_domain *gpu_get_pm_domain(const char *g3d_genpd_name)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct exynos_pm_domain *pd;

	for_each_compatible_node(np, NULL, "samsung,exynos-pd") {
		if (of_device_is_available(np)) {
			pdev = of_find_device_by_node(np);
			pd = (struct exynos_pm_domain *)platform_get_drvdata(pdev);
			if (strcmp(g3d_genpd_name, (const char *)(pd->genpd.name)) == 0)
				return pd;
		}
	}

	return NULL;
}

/**
 * gpu_power_status() - Returns the current power status of a GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Context: Process context. Takes and releases the power domain access lock.
 *
 * Return: Returns true if the GPU is powered on, false if not.
 */
bool gpu_power_status(struct kbase_device *kbdev)
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
 * gpu_power_init() - Initializes power control for a GPU.
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Return: An error code, or 0 on success.
 */
int gpu_power_init(struct kbase_device *kbdev)
{
	struct pixel_context *pc = kbdev->platform_context;
	struct device_node *np = kbdev->dev->of_node;
	const char *g3d_power_domain_name;

	if (of_property_read_u32(np, "gpu_pm_autosuspend_delay", &pc->pm.autosuspend_delay)) {
		pc->pm.autosuspend_delay = AUTO_SUSPEND_DELAY;
		GPU_LOG(LOG_INFO, kbdev, "autosuspend delay not set in DT, using default of %dms\n",
			AUTO_SUSPEND_DELAY);
	}

	if (of_property_read_u32(np, "gpu_pmu_status_reg_offset", &pc->pm.status_reg_offset)) {
		GPU_LOG(LOG_ERROR, kbdev, "PMU status register offset not set in DT\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "gpu_pmu_status_local_pwr_mask",
		&pc->pm.status_local_power_mask)) {
		GPU_LOG(LOG_ERROR, kbdev, "PMU status register power mask not set in DT\n");
		return -EINVAL;
	}

	if (of_property_read_string(np, "g3d_genpd_name", &g3d_power_domain_name)) {
		GPU_LOG(LOG_ERROR, kbdev, "GPU power domain name not set in DT\n");
		return -EINVAL;
	}

	pc->pm.domain = gpu_get_pm_domain(g3d_power_domain_name);
	if (pc->pm.domain == NULL)
		return -ENODEV;

	return 0;
}

/**
 * gpu_power_term() - Terminates power control for a GPU
 *
 * @kbdev: The &struct kbase_device for the GPU.
 *
 * Note that this function currently doesn't do anything.
 */
void gpu_power_term(struct kbase_device *kbdev)
{
}
