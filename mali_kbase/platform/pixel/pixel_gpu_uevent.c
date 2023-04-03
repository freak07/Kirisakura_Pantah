// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Varad Gautam <varadgautam@google.com>
 */

#include <linux/spinlock.h>
#include "pixel_gpu_uevent.h"

#define GPU_UEVENT_TIMEOUT_MS (30000U) /* 30s */

static struct gpu_uevent_ctx {
    unsigned long last_uevent_ts[GPU_UEVENT_TYPE_MAX];
    spinlock_t lock;
} gpu_uevent_ctx = {
    .last_uevent_ts = {0},
    .lock = __SPIN_LOCK_UNLOCKED(gpu_uevent_ctx.lock)
};

static bool gpu_uevent_check_valid(const struct gpu_uevent *evt)
{
    return false;
}

void pixel_gpu_uevent_send(struct kbase_device *kbdev, const struct gpu_uevent *evt)
{
    enum uevent_env_idx {
        ENV_IDX_TYPE,
        ENV_IDX_INFO,
        ENV_IDX_NULL,
        ENV_IDX_MAX
    };
    char *env[ENV_IDX_MAX] = {0};
    unsigned long flags, current_ts = jiffies;
    bool suppress_uevent = false;

    if (!gpu_uevent_check_valid(evt)) {
        dev_err(kbdev->dev, "unrecognized uevent type=%u info=%u", evt->type, evt->info);
        return;
    }

    env[ENV_IDX_TYPE] = (char *) gpu_uevent_type_str(evt->type);
    env[ENV_IDX_INFO] = (char *) gpu_uevent_info_str(evt->info);
    env[ENV_IDX_NULL] = NULL;

    spin_lock_irqsave(&gpu_uevent_ctx.lock, flags);

    if (time_after(current_ts, gpu_uevent_ctx.last_uevent_ts[evt->type]
            + msecs_to_jiffies(GPU_UEVENT_TIMEOUT_MS))) {
        gpu_uevent_ctx.last_uevent_ts[evt->type] = current_ts;
    } else {
        suppress_uevent = true;
    }

    spin_unlock_irqrestore(&gpu_uevent_ctx.lock, flags);

    if (!suppress_uevent)
        kobject_uevent_env(&kbdev->dev->kobj, KOBJ_CHANGE, env);
}
