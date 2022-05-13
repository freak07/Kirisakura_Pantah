// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

/* Mali core includes */
#include <mali_kbase.h>
#include <device/mali_kbase_device.h>

/* Pixel integration includes */
#include "pixel_gpu_debug.h"

#define GPU_DBG_LO               0x00000FE8
#define PIXEL_CG_PDC_ADDR        0x000760DB
#define PIXEL_SC_PDC_ADDR        0x000740DB
#define GPU_DBG_ACTIVE_BIT         (1 << 31)
#define GPU_DBG_ACTIVE_MAX_LOOPS    1000000
#define GPU_DBG_INVALID                (~0U)

static bool gpu_debug_check_dbg_active(struct kbase_device *kbdev)
{
	int i = 0;
	u32 val;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Wait for the active bit to drop, indicating the DBG command completed */
	do {
		val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS));
	} while ((val & GPU_DBG_ACTIVE_BIT) && i++ < GPU_DBG_ACTIVE_MAX_LOOPS);

	if (i == GPU_DBG_ACTIVE_MAX_LOOPS) {
		dev_err(kbdev->dev, "Timed out waiting for GPU DBG command to complete");
		return false;
	}
	return true;
}

static u32 gpu_debug_read_pdc(struct kbase_device *kbdev, u32 pdc_offset)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Write the debug command */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), pdc_offset);
	/* Wait for the debug command to complete */
	if (!gpu_debug_check_dbg_active(kbdev))
		return GPU_DBG_INVALID;

	/* Read the result */
	return kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_DBG_LO));
}

void gpu_debug_read_pdc_status(struct kbase_device *kbdev, struct pixel_gpu_pdc_status *status)
{
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	status->meta = (struct pixel_gpu_pdc_status_metadata) {
		.magic = "pdcs",
		.version = 1,
	};

	/* If there's no external power we skip the register read/writes,
	 * We know all the PDC states will be 0 in this case
	 */
	if (kbdev->pm.backend.gpu_powered == 0) {
		status->core_group = 0;
		memset(status->shader_cores, 0, sizeof(status->shader_cores));
		return;
	}

	status->core_group = gpu_debug_read_pdc(kbdev, PIXEL_CG_PDC_ADDR);
	for (i = 0; i < PIXEL_MALI_SC_COUNT; i++) {
		status->shader_cores[i] = gpu_debug_read_pdc(kbdev, PIXEL_SC_PDC_ADDR + i);
	}
}
