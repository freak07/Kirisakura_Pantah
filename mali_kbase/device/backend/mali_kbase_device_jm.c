/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

#include "../mali_kbase_device_internal.h"
#include "../mali_kbase_device.h"

#include <mali_kbase_config_defaults.h>
#include <mali_kbase_hwaccess_backend.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_reset_gpu.h>

#ifdef CONFIG_MALI_NO_MALI
#include <mali_kbase_model_linux.h>
#endif

static const struct kbase_device_init dev_init[] = {
#ifdef CONFIG_MALI_NO_MALI
	{kbase_gpu_device_create, kbase_gpu_device_destroy,
			"Dummy model initialization failed"},
#else
	{assign_irqs, NULL,
			"IRQ search failed"},
	{registers_map, registers_unmap,
			"Register map failed"},
#endif
	{power_control_init, power_control_term,
			"Power control initialization failed"},
	{kbase_device_io_history_init, kbase_device_io_history_term,
			"Register access history initialization failed"},
	{kbase_backend_early_init, kbase_backend_early_term,
			"Early backend initialization failed"},
	{kbase_device_populate_max_freq, NULL,
			"Populating max frequency failed"},
	{kbase_device_misc_init, kbase_device_misc_term,
			"Miscellaneous device initialization failed"},
	{kbase_ctx_sched_init, kbase_ctx_sched_term,
			"Context scheduler initialization failed"},
	{kbase_mem_init, kbase_mem_term,
			"Memory subsystem initialization failed"},
	{kbase_device_coherency_init, NULL,
			"Device coherency init failed"},
	{kbase_protected_mode_init, kbase_protected_mode_term,
			"Protected mode subsystem initialization failed"},
	{kbase_device_list_init, kbase_device_list_term,
			"Device list setup failed"},
	{kbasep_js_devdata_init, kbasep_js_devdata_term,
			"Job JS devdata initialization failed"},
	{kbase_device_timeline_init, kbase_device_timeline_term,
			"Timeline stream initialization failed"},
	{kbase_device_hwcnt_backend_gpu_init,
			kbase_device_hwcnt_backend_gpu_term,
			"GPU hwcnt backend creation failed"},
	{kbase_device_hwcnt_context_init, kbase_device_hwcnt_context_term,
			"GPU hwcnt context initialization failed"},
	{kbase_device_hwcnt_virtualizer_init,
			kbase_device_hwcnt_virtualizer_term,
			"GPU hwcnt virtualizer initialization failed"},
	{kbase_device_vinstr_init, kbase_device_vinstr_term,
			"Virtual instrumentation initialization failed"},
	{kbase_backend_late_init, kbase_backend_late_term,
			"Late backend initialization failed"},
#ifdef MALI_KBASE_BUILD
	{kbase_debug_job_fault_dev_init, kbase_debug_job_fault_dev_term,
			"Job fault debug initialization failed"},
	{kbase_device_debugfs_init, kbase_device_debugfs_term,
			"DebugFS initialization failed"},
	/* Sysfs init needs to happen before registering the device with
	 * misc_register(), otherwise it causes a race condition between
	 * registering the device and a uevent event being generated for
	 * userspace, causing udev rules to run which might expect certain
	 * sysfs attributes present. As a result of the race condition
	 * we avoid, some Mali sysfs entries may have appeared to udev
	 * to not exist.
	 * For more information, see
	 * https://www.kernel.org/doc/Documentation/driver-model/device.txt, the
	 * paragraph that starts with "Word of warning", currently the
	 * second-last paragraph.
	 */
	{kbase_sysfs_init, kbase_sysfs_term, "SysFS group creation failed"},
	{kbase_device_misc_register, kbase_device_misc_deregister,
			"Misc device registration failed"},
#ifdef CONFIG_MALI_BUSLOG
	{buslog_init, buslog_term, "Bus log client registration failed"},
#endif
	{kbase_gpuprops_populate_user_buffer, kbase_gpuprops_free_user_buffer,
			"GPU property population failed"},
#endif
};

static void kbase_device_term_partial(struct kbase_device *kbdev,
		unsigned int i)
{
	while (i-- > 0) {
		if (dev_init[i].term)
			dev_init[i].term(kbdev);
	}
}

void kbase_device_term(struct kbase_device *kbdev)
{
	kbase_device_term_partial(kbdev, ARRAY_SIZE(dev_init));
	kbasep_js_devdata_halt(kbdev);
	kbase_mem_halt(kbdev);
}

int kbase_device_init(struct kbase_device *kbdev)
{
	int err = 0;
	unsigned int i = 0;

	kbase_device_id_init(kbdev);
	kbase_disjoint_init(kbdev);

	for (i = 0; i < ARRAY_SIZE(dev_init); i++) {
		err = dev_init[i].init(kbdev);
		if (err) {
			dev_err(kbdev->dev, "%s error = %d\n",
						dev_init[i].err_mes, err);
			kbase_device_term_partial(kbdev, i);
			break;
		}
	}

	return err;
}
