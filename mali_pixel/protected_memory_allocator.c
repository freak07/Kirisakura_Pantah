// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Protected memory allocator driver for allocation and release of pages of
 * protected memory for use by Mali GPU device drivers.
 */

#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static int protected_memory_allocator_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Protected memory allocator not implemented\n");
	return -ENODEV;
}

static int protected_memory_allocator_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id protected_memory_allocator_dt_ids[] = {
	{ .compatible = "arm,protected-memory-allocator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, protected_memory_allocator_dt_ids);

struct platform_driver protected_memory_allocator_driver = {
	.probe = protected_memory_allocator_probe,
	.remove = protected_memory_allocator_remove,
	.driver = {
		.name = "mali-pma",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(protected_memory_allocator_dt_ids),
		.suppress_bind_attrs = true,
	}
};

