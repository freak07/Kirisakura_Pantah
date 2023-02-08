// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#ifndef _UAPI_PIXEL_MEMORY_GROUP_MANAGER_H_
#define _UAPI_PIXEL_MEMORY_GROUP_MANAGER_H_

/**
 * enum pixel_mgm_group_id - Symbolic names for used memory groups
 */
enum pixel_mgm_group_id
{
	/* The Mali driver requires that allocations made on one of the groups
	 * are not treated specially.
	 */
	MGM_RESERVED_GROUP_ID = 0,

	/* Group for memory that should be cached in the system level cache. */
	MGM_SLC_GROUP_ID = 1,

	/* Imported memory is handled by the allocator of the memory, and the Mali
	 * DDK will request a group_id for such memory via mgm_get_import_memory_id().
	 * We specify which group we want to use for this here.
	 */
	MGM_IMPORTED_MEMORY_GROUP_ID = (MEMORY_GROUP_MANAGER_NR_GROUPS - 1),
};

#endif /* _UAPI_PIXEL_MEMORY_GROUP_MANAGER_H_ */
