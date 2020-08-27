/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC.
 *
 * Author: Sidath Senanayake <sidaths@google.com>
 */

#ifndef _PIXEL_GPU_DEBUG_H_
#define _PIXEL_GPU_DEBUG_H_

/**
 * enum gpu_log_level - Verbosity level of a GPU log entry. Ordered in
 * level of verbosity.
 */
enum gpu_log_level {
	LOG_START = 0,
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR,
	LOG_END,
};

/**
 * GPU_LOG() - Register a GPU log entry at a specified level of verbosity.
 *
 * @level: The verbosity of this log message.
 * @kbdev: The &struct kbase_device for the GPU.
 * @msg  : A printf syle string to be logged.
 * @...  : Token values for @msg.
 */
#define GPU_LOG(level, kbdev, msg, args...) \
do { \
	if (level >= LOG_WARN) { \
		switch (level) { \
		case LOG_DEBUG: \
			dev_dbg(kbdev->dev, "pixel: " msg, ## args); \
			break; \
		case LOG_INFO: \
			dev_info(kbdev->dev, "pixel: " msg, ## args); \
			break; \
		case LOG_WARN: \
			dev_warn(kbdev->dev, "pixel: " msg, ## args); \
			break; \
		case LOG_ERROR: \
			dev_err(kbdev->dev, "pixel: " msg, ## args); \
			break; \
		default: \
			dev_warn(kbdev->dev, "pixel: Invalid log level"); \
		} \
	} \
} while (0)

#endif /* _PIXEL_GPU_DEBUG_H_ */
