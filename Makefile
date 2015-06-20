# Makefile for wtfs.
#
# Copyright (c) 2015 Chaos Shen
#
# This file is part of wtfs, What the fxck filesystem.  You may take
# the letter 'f' from, at your option, either 'fxck' or 'filesystem'.
#
# wtfs is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# wtfs is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with wtfs.  If not, see <http://www.gnu.org/licenses/>.

CC := cc
DEBUG_CFLAGS := -DDEBUG -g
RM := rm -rf

obj-m := wtfs.o
wtfs-objs := super.o inode.o file.o dir.o helper.o

all: programs module

programs: mkfs.wtfs statfs.wtfs

mkfs.wtfs: mkfs.wtfs.c
	$(CC) $(CFLAGS) -o mkfs.wtfs mkfs.wtfs.c

statfs.wtfs: statfs.wtfs.c
	$(CC) $(CFLAGS) -o statfs.wtfs statfs.wtfs.c

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

debug:
	make programs CFLAGS="$(DEBUG_CFLAGS)"
	make module CONFIG_DEBUG_INFO=1 KCFLAGS="$(DEBUG_CFLAGS)"

clean: clean_programs clean_module

clean_programs:
	$(RM) mkfs.wtfs statfs.wtfs

clean_module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

.PHONY: mkfs.wtfs statfs.wtfs
