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

/* SOC level includes */
#if IS_ENABLED(CONFIG_EXYNOS_PD)
#include <soc/google/exynos-pd.h>
#endif

/**
 * struct pixel_context - Pixel GPU context
 *
 * @kbdev:                      The &struct kbase_device for the GPU.
 *
 * @pm.state_lost:              Stores whether GPU state has been lost or not.
 * @pm.domain:                  The power domain the GPU is in.
 * @pm.status_reg_offset:       Register offset to the G3D status in the PMU. Set via DT.
 * @pm.status_local_power_mask: Mask to extract power status of the GPU. Set via DT.
 * @pm.autosuspend_delay:       Delay (in ms) before PM runtime should trigger auto suspend.
 */
struct pixel_context {
	struct kbase_device *kbdev;

	struct {
		bool state_lost;
		struct exynos_pm_domain *domain;
		unsigned int status_reg_offset;
		unsigned int status_local_power_mask;
		unsigned int autosuspend_delay;
	} pm;
};

#endif /* _KBASE_CONFIG_PLATFORM_H_ */
