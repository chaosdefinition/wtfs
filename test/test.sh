#!/bin/bash

# test script for wtfs.
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

# directories
test_dir=`dirname $0`
build_dir="$test_dir/../build"

# test takers
mkfs="$build_dir/mkfs.wtfs"
statfs="$build_dir/statfs.wtfs"
module="$build_dir/wtfs.ko"

# test makers
test_mkfs="$test_dir/test_mkfs.sh"
test_statfs="$test_dir/test_statfs.sh"
test_module="$test_dir/test_module.sh"

# do test
#
# $1: test taker name
# $2: test taker path
# $3: test maker path
function do_test {
	if [[ -f "$2" ]] && [[ -f "$3" ]]; then
		printf "testing $1...\n"
		( . "$3" )
		if (( $? != 0 )); then
			printf "$1 failed to pass the test\n\n"
			return 1
		else
			printf "\n"
		fi
	else
		printf "$1 is not ready for the test\n\n"
	fi

	return 0
}

# do tests
result=0
do_test "mkfs.wtfs" "$mkfs" "$test_mkfs"
(( $? != 0 )) && result=1
do_test "statfs.wtfs" "$statfs" "$test_statfs"
(( $? != 0 )) && result=1
do_test "wtfs module" "$module" "$test_module"
(( $? != 0 )) && result=1

if (( result == 0 )); then
	printf "test OK!\n"
else
	exit 1
fi
