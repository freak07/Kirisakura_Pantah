// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC.
 */

#ifndef _PIXEL_GPU_TRACE_H_
#define _PIXEL_GPU_TRACE_H_

enum gpu_power_state {
	/* Mali GPUs have a hierarchy of power domains, which must be powered up
	 * in order and powered down in reverse order. Individual architectures
	 * and implementations may not allow each domain to be powered up or
	 * down independently of the others.
	 *
	 * The power state can thus be defined as the highest-level domain that
	 * is currently powered on.
	 *
	 * GLOBAL: The frontend (JM, CSF), including registers.
	 * COREGROUP: The L2 and AXI interface, Tiler, and MMU.
	 * STACKS: The shader cores.
	 */
	GPU_POWER_LEVEL_OFF		    = 0,
	GPU_POWER_LEVEL_GLOBAL		= 1,
	GPU_POWER_LEVEL_COREGROUP	= 2,
	GPU_POWER_LEVEL_STACKS		= 3,
};

#endif /* _PIXEL_GPU_TRACE_H_ */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mali

#if !defined(_TRACE_PIXEL_GPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PIXEL_GPU_H

#include <linux/tracepoint.h>

#define GPU_POWER_STATE_SYMBOLIC_STRINGS \
	{GPU_POWER_LEVEL_STACKS,	"STACKS"}, \
	{GPU_POWER_LEVEL_COREGROUP,	"COREGROUP"}, \
	{GPU_POWER_LEVEL_GLOBAL,	"GLOBAL"}, \
	{GPU_POWER_LEVEL_OFF,		"OFF"}

TRACE_EVENT(gpu_power_state,
	TP_PROTO(u64 change_ns, int from, int to),
	TP_ARGS(change_ns, from, to),
	TP_STRUCT__entry(
		__field(u64, change_ns)
		__field(int, from_state)
		__field(int, to_state)
	),
	TP_fast_assign(
		__entry->change_ns	= change_ns;
		__entry->from_state	= from;
		__entry->to_state	= to;
	),
	TP_printk("from=%s to=%s ns=%llu",
		__print_symbolic(__entry->from_state, GPU_POWER_STATE_SYMBOLIC_STRINGS),
		__print_symbolic(__entry->to_state, GPU_POWER_STATE_SYMBOLIC_STRINGS),
		__entry->change_ns
	)
);

TRACE_EVENT(gpu_util,
	TP_PROTO(int gpu_util),
	TP_ARGS(gpu_util),
	TP_STRUCT__entry(
		__field(int, gpu_util)
	),
	TP_fast_assign(
		__entry->gpu_util = gpu_util;
	),
	TP_printk("gpu_util=%d",
		__entry->gpu_util
	)
);

#endif /* _TRACE_PIXEL_GPU_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef  TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pixel_gpu_trace
#include <trace/define_trace.h>
