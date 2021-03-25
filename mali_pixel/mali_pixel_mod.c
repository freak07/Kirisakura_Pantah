// SPDX-License-Identifier: GPL-2.0

#include "mali_pixel_mod.h"
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pixel platform integration for GPU");
MODULE_AUTHOR("<sidaths@google.com>");
MODULE_VERSION("1.0");

static int __init mali_pixel_init(void)
{
	int ret = 0;

#ifdef CONFIG_MALI_MEMORY_GROUP_MANAGER
	ret = platform_driver_register(&memory_group_manager_driver);
#endif
	if (ret)
		goto fail_mgm;

#ifdef CONFIG_MALI_PRIORITY_CONTROL_MANAGER
	ret = platform_driver_register(&priority_control_manager_driver);
#else
#endif
	if (ret)
		goto fail_pcm;

	goto exit;

fail_pcm:
#ifdef CONFIG_MALI_MEMORY_GROUP_MANAGER
	platform_driver_unregister(&memory_group_manager_driver);
#endif

fail_mgm:
	/* nothing to clean up here */

exit:
	return ret;
}
module_init(mali_pixel_init);

static void __exit mali_pixel_exit(void)
{
#ifdef CONFIG_MALI_PRIORITY_CONTROL_MANAGER
	platform_driver_unregister(&priority_control_manager_driver);
#endif
#ifdef CONFIG_MALI_MEMORY_GROUP_MANAGER
	platform_driver_unregister(&memory_group_manager_driver);
#endif
}
module_exit(mali_pixel_exit);
