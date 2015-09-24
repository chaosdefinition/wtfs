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

############################## variable area ###################################
# commands
CC := cc
RM := rm -rf
MAKE := make
ECHO := echo

# directories
SRC := src
INCLUDE := include
BUILD := build
TEST := test

# CFLAGS
BASE_CFLAGS := -I$(PWD)/$(INCLUDE) -Wall
DEBUG_CFLAGS := -DDEBUG -g $(BASE_CFLAGS)
RELEASE_CFLAGS := -O2 $(BASE_CFLAGS)

ifndef CFLAGS
	CFLAGS := $(RELEASE_CFLAGS)
endif

ifndef KCFLAGS
	KCFLAGS := $(RELEASE_CFLAGS)
endif

# kernel version
ifndef KV
	KV := $(shell uname -r)
endif

############################## target area #####################################
# default is release
all: release

# userspace programs
programs: mkfs.wtfs statfs.wtfs

mkfs.wtfs:
	@$(ECHO) "  CC      $(PWD)/$(BUILD)/mkfs.wtfs"
	@$(CC) $(CFLAGS) -o "$(BUILD)/mkfs.wtfs" "$(SRC)/mkfs.wtfs.c" -luuid -lmount

statfs.wtfs:
	@$(ECHO) "  CC      $(PWD)/$(BUILD)/statfs.wtfs"
	@$(CC) $(CFLAGS) -o "$(BUILD)/statfs.wtfs" "$(SRC)/statfs.wtfs.c" -luuid

# kernel module
module:
	@$(MAKE) -C /lib/modules/$(KV)/build M="$(PWD)/$(BUILD)" KCFLAGS="$(KCFLAGS)" modules

# debug and release
debug:
	@$(MAKE) programs CFLAGS="$(DEBUG_CFLAGS)"
	@$(MAKE) module KCFLAGS="$(DEBUG_CFLAGS)"

release: programs module

# tests
test:
	@bash "$(TEST)/test.sh"

# clean
clean: clean_programs clean_module

clean_programs:
	@$(ECHO) "  CLEAN   $(PWD)/$(BUILD)/mkfs.wtfs"
	@$(ECHO) "  CLEAN   $(PWD)/$(BUILD)/statfs.wtfs"
	@$(RM) $(BUILD)/*.wtfs

clean_module:
	@$(MAKE) -C /lib/modules/$(KV)/build M="$(PWD)/$(BUILD)" clean
	@$(MAKE) -C /lib/modules/$(KV)/build M="$(PWD)" clean

# always make these three targets
.PHONY: mkfs.wtfs statfs.wtfs test
