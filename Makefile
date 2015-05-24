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

obj-m := wtfs.o
wtfs-objs := super.o inode.o file.o dir.o helper.o

CFLAGS_super.o := -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
