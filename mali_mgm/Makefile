#
# (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

# linux build system bootstrap for out-of-tree module

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

KBUILD_OPTIONS += CONFIG_MALI_MEMORY_GROUP_MANAGER=m

KBUILD_OPTIONS += $(KBUILD_EXTRA) # Extra config if any

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 EXTRA_CFLAGS="-I$(M) -I$(M)/../common/include" modules $(KBUILD_OPTIONS) $(@)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
