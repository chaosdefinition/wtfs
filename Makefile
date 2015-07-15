# Makefile for wtfs.
#
# Copyright (C) 2015 Chaos Shen
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
RELEASE_CFLAGS := -O2
RM := rm -rf

obj-m := wtfs.o
wtfs-objs := super.o inode.o file.o dir.o helper.o

all: release

programs: mkfs.wtfs statfs.wtfs

mkfs.wtfs: mkfs.wtfs.c
	$(CC) $(CFLAGS) -o mkfs.wtfs mkfs.wtfs.c -luuid

statfs.wtfs: statfs.wtfs.c
	$(CC) $(CFLAGS) -o statfs.wtfs statfs.wtfs.c -luuid

module:
	make -C /lib/modules/$(KV)/build M=$(PWD) modules

debug:
	make programs CFLAGS="$(DEBUG_CFLAGS)"
	make module CONFIG_DEBUG_INFO=1 KCFLAGS="$(DEBUG_CFLAGS)" \
		KV=$(shell uname -r)

release:
	make programs CFLAGS="$(RELEASE_CFLAGS)"
	make module KCFLAGS="$(RELEASE_CFLAGS)" KV=$(shell uname -r)

clean:
	make clean_programs
	make clean_module KV=$(shell uname -r)

clean_programs:
	$(RM) mkfs.wtfs statfs.wtfs

clean_module:
	make -C /lib/modules/$(KV)/build M=$(PWD) clean

.PHONY: mkfs.wtfs statfs.wtfs
