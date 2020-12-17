#
# (C) COPYRIGHT 2010-2019 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# SPDX-License-Identifier: GPL-2.0
#
#


KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

BUSLOG_PATH_RELATIVE = $(M)/../../../..
KBASE_PATH_RELATIVE = $(M)

KBUILD_OPTIONS += CONFIG_MALI_MIDGARD=m
KBUILD_OPTIONS += MALI_DEVFREQ=n
KBUILD_OPTIONS += CONFIG_MALI_EXPERT=y
KBUILD_OPTIONS += CONFIG_MALI_DMA_BUF_LEGACY_COMPAT=y

# Pixel integration Kbuild options
KBUILD_OPTIONS += CONFIG_MALI_PLATFORM_NAME="pixel"
KBUILD_OPTIONS += CONFIG_MALI_MIDGARD_DVFS=y
KBUILD_OPTIONS += CONFIG_MALI_PIXEL_GPU_QOS=y
KBUILD_OPTIONS += CONFIG_MALI_PIXEL_GPU_QOS=y
KBUILD_OPTIONS += CONFIG_MALI_PIXEL_GPU_THERMAL=y
KBUILD_OPTIONS += CONFIG_MALI_PIXEL_GPU_SECURE_RENDERING=y

KBUILD_OPTIONS += $(KBUILD_EXTRA) # Extra config if any

ifeq ($(CONFIG_MALI_BUSLOG),y)
#Add bus logger symbols
EXTRA_SYMBOLS += $(BUSLOG_PATH_RELATIVE)/drivers/base/bus_logger/Module.symvers
endif

# we get the symbols from modules using KBUILD_EXTRA_SYMBOLS to prevent warnings about unknown functions
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 KCFLAGS=-Wno-error EXTRA_CFLAGS="-I$(M) -I$(M)/../common/include -I$(M)/../../../../tests/include $(SCONS_CFLAGS)" $(SCONS_CONFIGS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules $(KBUILD_OPTIONS) $(@)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
