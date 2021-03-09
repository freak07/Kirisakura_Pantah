# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2021 Google LLC
#

# linux build system bootstrap for out-of-tree module

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

KBUILD_OPTIONS += CONFIG_MALI_PRIORITY_CONTROL_MANAGER=m

KBUILD_OPTIONS += $(KBUILD_EXTRA) # Extra config if any

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 EXTRA_CFLAGS="-I$(M) -I$(M)/../common/include" modules $(KBUILD_OPTIONS) $(@)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
