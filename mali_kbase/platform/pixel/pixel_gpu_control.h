/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

#ifndef _PIXEL_GPU_CONTROL_H_
#define _PIXEL_GPU_CONTROL_H_

/* Power management */
bool gpu_power_status(struct kbase_device *kbdev);
int gpu_power_init(struct kbase_device *kbdev);
void gpu_power_term(struct kbase_device *kbdev);

#endif /* _PIXEL_GPU_CONTROL_H_ */
