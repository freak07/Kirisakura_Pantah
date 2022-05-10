/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

#ifndef _PIXEL_GPU_DEBUG_H_
#define _PIXEL_GPU_DEBUG_H_

/* This is currently only supported for Odin */
#define PIXEL_MALI_SC_COUNT 0x7

/**
 * struct pixel_gpu_pdc_status_bits - PDC status layout
 *
 * @_reserved_0:   Undocumented
 * @pwrup:         Power up request
 * @pwrup_ack      Power up request acknowledged by PDC
 * @reset_n        Reset request
 * @reset_ack_n    Reset request acknowledged by PDC
 * @isolate_n      Physical isolation enable request
 * @isolate_ack_n  Physical isolation enable request has been acknowledged by PDC
 * @clken          Clock enable request
 * @clken_ack      Clock enable request acknowledged from internal gating
 * @power_is_on    PDC thinks power domain is fully on
 * @power_is_off   PDC thinks power domain is fully off
 * @_reserved_1    Undocumented
 **/
struct pixel_gpu_pdc_status_bits {
	uint32_t _reserved_0 : 7;
	uint32_t pwrup : 1;
	uint32_t pwrup_ack : 1;
	uint32_t reset_n : 1;
	uint32_t reset_ack_n : 1;
	uint32_t isolate_n : 1;
	uint32_t isolate_ack_n : 1;
	uint32_t clken : 1;
	uint32_t clken_ack : 1;
	uint32_t power_is_on : 1;
	uint32_t power_is_off : 1;
	uint32_t _reserved_1 : 15;
};
_Static_assert(sizeof(struct pixel_gpu_pdc_status_bits) == sizeof(uint32_t),
	       "Incorrect pixel_gpu_pdc_status_bits size");

/**
 * struct pixel_gpu_pdc_status_metadata - Info about the PDC status format
 *
 * @magic:          Always 'pdcs', helps find the log in memory dumps
 * @version:        Updated whenever the binary layout changes
 * @_reserved:      Bytes reserved for future use
 **/
struct pixel_gpu_pdc_status_metadata {
	char magic[4];
	uint8_t version;
	char _reserved[11];
} __attribute__((packed));
_Static_assert(sizeof(struct pixel_gpu_pdc_status_metadata) == 16,
	       "Incorrect pixel_gpu_pdc_status_metadata size");

/**
 * struct pixel_gpu_pdc_status - FW view of PDC state
 *
 * @meta:         Info about the status format
 * @core_group:   Core group PDC state
 * @shader_cores: Shader core PDC state
 **/
struct pixel_gpu_pdc_status {
	struct pixel_gpu_pdc_status_metadata meta;
	union {
		struct pixel_gpu_pdc_status_bits core_group_bits;
		uint32_t core_group;
	};
	union {
		struct pixel_gpu_pdc_status_bits shader_cores_bits[PIXEL_MALI_SC_COUNT];
		uint32_t shader_cores[PIXEL_MALI_SC_COUNT];
	};
} __attribute__((packed));

#if MALI_USE_CSF
void gpu_debug_read_pdc_status(struct kbase_device *kbdev, struct pixel_gpu_pdc_status *status);
#else
static void __maybe_unused gpu_debug_read_pdc_status(struct kbase_device *kbdev,
						     struct pixel_gpu_pdc_status *status)
{
	(void)kbdev, (void)status;
}
#endif

#endif /* _PIXEL_GPU_DEBUG_H_ */
