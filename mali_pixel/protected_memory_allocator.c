// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Protected memory allocator driver for allocation and release of pages of
 * protected memory for use by Mali GPU device drivers.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/protected_memory_allocator.h>
#include <linux/slab.h>
#include <soc/samsung/exynos-smc.h>

#define MALI_PMA_DMA_HEAP_NAME "vframe-secure"

/**
 * struct mali_pma_dev - Structure for managing a Mali protected memory
 *                       allocator device.
 *
 * @pma_dev: The base protected memory allocator device.
 * @dev: The device for which to allocate protected memory.
 * @dma_heap: The DMA buffer heap from which to allocate protected memory.
 */
struct mali_pma_dev {
	struct protected_memory_allocator_device pma_dev;
	struct device *dev;
	struct dma_heap *dma_heap;
};

/**
 * struct mali_protected_memory_allocation - Structure for tracking a Mali
 *                                           protected memory allocation.
 *
 * @pma: The base protected memory allocation record.
 * @dma_buf: The DMA buffer allocated for the protected memory. A reference to
 *           the DMA buffer is held by this pointer.
 * @dma_attachment: The DMA buffer device attachment.
 * @dma_sg_table: The DMA buffer scatter/gather table.
 */
struct mali_protected_memory_allocation {
	struct protected_memory_allocation pma;
	struct dma_buf* dma_buf;
	struct dma_buf_attachment* dma_attachment;
	struct sg_table* dma_sg_table;
};

static struct protected_memory_allocation *mali_pma_alloc_page(
	struct protected_memory_allocator_device *pma_dev,
	unsigned int order);

static phys_addr_t mali_pma_get_phys_addr(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma);

static void mali_pma_free_page(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma);

static int protected_memory_allocator_probe(struct platform_device *pdev);

static int protected_memory_allocator_remove(struct platform_device *pdev);

/**
 * mali_pma_alloc_page - Allocate protected memory pages
 *
 * @pma_dev: The protected memory allocator the request is being made
 *           through.
 * @order:   How many pages to allocate, as a base-2 logarithm.
 *
 * Return: Pointer to allocated memory, or NULL if allocation failed.
 */
static struct protected_memory_allocation *mali_pma_alloc_page(
	struct protected_memory_allocator_device *pma_dev,
	unsigned int order) {
	struct mali_pma_dev *mali_pma_dev;
	struct protected_memory_allocation* pma = NULL;
	struct mali_protected_memory_allocation *mali_pma;
	struct dma_buf* dma_buf;
	struct dma_buf_attachment* dma_attachment;
	struct sg_table* dma_sg_table;
	size_t alloc_size;
	bool success = false;

	/* Get the Mali protected memory allocator device record. */
	mali_pma_dev = container_of(pma_dev, struct mali_pma_dev, pma_dev);

	/* Allocate a Mali protected memory allocation record. */
	mali_pma = devm_kzalloc(
		mali_pma_dev->dev, sizeof(*mali_pma), GFP_KERNEL);
	if (!mali_pma) {
		dev_err(mali_pma_dev->dev,
			"Failed to allocate a Mali protected memory allocation "
			"record\n");
		goto out;
	}
	pma = &(mali_pma->pma);
	pma->order = order;

	/* Allocate a DMA buffer. */
	alloc_size = 1 << (PAGE_SHIFT + order);
	dma_buf = dma_heap_buffer_alloc(
		mali_pma_dev->dma_heap, alloc_size, O_RDWR, 0);
	if (IS_ERR(dma_buf)) {
		dev_err(mali_pma_dev->dev,
			"Failed to allocate a DMA buffer of size %zu\n",
			alloc_size);
		goto out;
	}
	mali_pma->dma_buf = dma_buf;

	/* Attach the device to the DMA buffer. */
	dma_attachment = dma_buf_attach(dma_buf, mali_pma_dev->dev);
	if (IS_ERR(dma_attachment)) {
		dev_err(mali_pma_dev->dev,
			"Failed to attach the device to the DMA buffer\n");
		goto out;
	}
	mali_pma->dma_attachment = dma_attachment;

	/* Map the DMA buffer into the attached device address space. */
	dma_sg_table =
		dma_buf_map_attachment(dma_attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(dma_sg_table)) {
		dev_err(mali_pma_dev->dev, "Failed to map the DMA buffer\n");
		goto out;
	}
	mali_pma->dma_sg_table = dma_sg_table;
	pma->pa = page_to_phys(sg_page(dma_sg_table->sgl));

	/* Mark the allocation as successful. */
	success = true;

out:
	/* Clean up on error. */
	if (!success) {
		if (pma) {
			mali_pma_free_page(pma_dev, pma);
			pma = NULL;
		}
	}

	return pma;
}

/**
 * mali_pma_get_phys_addr - Get the physical address of the protected memory
 *                          allocation
 *
 * @pma_dev: The protected memory allocator the request is being made
 *           through.
 * @pma:     The protected memory allocation whose physical address
 *           shall be retrieved
 *
 * Return: The physical address of the given allocation.
 */
static phys_addr_t mali_pma_get_phys_addr(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma) {
	return pma->pa;
}

/**
 * pma_free_page - Free a page of memory
 *
 * @pma_dev: The protected memory allocator the request is being made
 *           through.
 * @pma:     The protected memory allocation to free.
 */
static void mali_pma_free_page(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma) {
	struct mali_pma_dev *mali_pma_dev;
	struct mali_protected_memory_allocation *mali_pma;

	/*
	 * Get the Mali protected memory allocator device record and allocation
	 * record.
	 */
	mali_pma_dev = container_of(pma_dev, struct mali_pma_dev, pma_dev);
	mali_pma =
		container_of(pma, struct mali_protected_memory_allocation, pma);

	/* Free the Mali protected memory allocation. */
	if (mali_pma->dma_sg_table) {
		dma_buf_unmap_attachment(
			mali_pma->dma_attachment,
		 	mali_pma->dma_sg_table, DMA_BIDIRECTIONAL);
	}
	if (mali_pma->dma_attachment) {
		dma_buf_detach(mali_pma->dma_buf, mali_pma->dma_attachment);
	}
	if (mali_pma->dma_buf) {
		dma_buf_put(mali_pma->dma_buf);
	}
	devm_kfree(mali_pma_dev->dev, mali_pma);
}

/**
 * protected_memory_allocator_probe - Probe the protected memory allocator
 *                                    device
 *
 * @pdev: The platform device to probe.
 */
static int protected_memory_allocator_probe(struct platform_device *pdev)
{
	struct mali_pma_dev *mali_pma_dev;
	struct protected_memory_allocator_device *pma_dev;
	int ret = 0;

	/* Create a Mali protected memory allocator device record. */
	mali_pma_dev = kzalloc(sizeof(*mali_pma_dev), GFP_KERNEL);
	if (!mali_pma_dev) {
		dev_err(&(pdev->dev),
			"Failed to create a Mali protected memory allocator "
			"device record\n");
		ret = -ENOMEM;
		goto out;
	}
	pma_dev = &(mali_pma_dev->pma_dev);
	platform_set_drvdata(pdev, pma_dev);

	/* Configure the Mali protected memory allocator. */
	mali_pma_dev->dev = &(pdev->dev);
	pma_dev->owner = THIS_MODULE;
	pma_dev->ops.pma_alloc_page = mali_pma_alloc_page;
	pma_dev->ops.pma_get_phys_addr = mali_pma_get_phys_addr;
	pma_dev->ops.pma_free_page = mali_pma_free_page;

	/* Get the DMA buffer heap. */
	mali_pma_dev->dma_heap = dma_heap_find(MALI_PMA_DMA_HEAP_NAME);
	if (!mali_pma_dev->dma_heap) {
		dev_err(&(pdev->dev),
			"Failed to find \"%s\" DMA buffer heap\n",
                        MALI_PMA_DMA_HEAP_NAME);
		ret = -ENODEV;
		goto out;
	}

	/* Enable protected mode for the GPU. */
	ret = exynos_smc(
		SMC_PROTECTION_SET, 0, PROT_G3D, SMC_PROTECTION_ENABLE);
	if (ret) {
		dev_err(&(pdev->dev),
			"Failed to enable protected mode for the GPU\n");
		goto out;
	}

	/* Log that the protected memory allocator was successfully probed. */
	dev_info(&(pdev->dev),
		"Protected memory allocator probed successfully\n");

out:
	/* Clean up on error. */
	if (ret) {
		protected_memory_allocator_remove(pdev);
	}

	return ret;
}

/**
 * protected_memory_allocator_remove - Remove the protected memory allocator
 *                                     device
 *
 * @pdev: The protected memory allocator platform device to remove.
 */
static int protected_memory_allocator_remove(struct platform_device *pdev)
{
	struct protected_memory_allocator_device *pma_dev;
	struct mali_pma_dev *mali_pma_dev;
	int ret;

	/* Get the Mali protected memory allocator device record. */
	pma_dev = platform_get_drvdata(pdev);
	if (!pma_dev) {
		return 0;
	}
	mali_pma_dev = container_of(pma_dev, struct mali_pma_dev, pma_dev);

	/* Disable protected mode for the GPU. */
	ret = exynos_smc(
		SMC_PROTECTION_SET, 0, PROT_G3D, SMC_PROTECTION_DISABLE);
	if (ret) {
		dev_warn(&(pdev->dev),
			"Failed to disable protected mode for the GPU\n");
	}

	/* Release the DMA buffer heap. */
	if (mali_pma_dev->dma_heap) {
		dma_heap_put(mali_pma_dev->dma_heap);
	}

	/* Free the Mali protected memory allocator device record. */
	kfree(mali_pma_dev);

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

