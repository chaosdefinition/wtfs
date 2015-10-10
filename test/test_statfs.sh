#!/bin/bash

# test script for statfs.wtfs.
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

# convert a hex integer to decimal
#
# $1: hex integer without '0x' or '0X'
function hex_to_dec {
	if ! which bc > /dev/null; then
		printf $(( 0x$1 ))
	else
		printf "ibase=16; ${1^^}\n" | BC_LINE_LENGTH=0 bc
	fi
}

# read a little endian binary integer from a file
#
# $1: filename
# $2: 0-based offset
# $3: length
function read_integer {
	local hex=""
	local i=$(( $3 - 1 ))

	for (( ; i >= 0; --i )); do
		hex="$hex`tail -c+$(( $2 + i + 1 )) $1 | head -c1 | xxd -ps`"
	done
	hex_to_dec "$hex"
}

# explain what happened in the test
#
# $1: return value of the test
# $2: part of the test
function what {
	case $1 in
	0 )
		printf "passed $2\n"
		return 0
		;;
	4 )
		printf "skip $2\n"
		(( ++skipped ))
		return 0
		;;
	1 )
		printf "a bug found in $2\n"
		;;
	* )
		printf "unknown error $1 occurred in $2\n"
		;;
	esac
	return 1
}

# clear the spot
function clear_spot {
	if [[ -n "$wtfs_img" ]]; then
		rm -rf "$wtfs_img"
		unset wtfs_img
	fi
	if [[ -n "$stdout" ]]; then
		rm -rf "$stdout"
		unset stdout
	fi
	unset label
	unset uuid
	unset tests
	unset skipped
}

################################################################################
# following are test functions
# see function 'what' for explanation of return value

# test version in output
function test_version {
	local grep_version='grep -Po (?<=version:\s{16})\d+\.\d+\.\d+'
	local grep_major='grep -Po (?<=\s)\d+(?=\.)'
	local grep_minor='grep -Po (?<=\.)\d+(?=\.)'
	local grep_patch='grep -Po (?<=\.)\d+$'

	local version_str=`cat "$stdout" | $grep_version`
	local major=`printf "$version_str" | $grep_major`
	local minor=`printf "$version_str" | $grep_minor`
	local patch=`printf "$version_str" | $grep_patch`
	local output_version=$(( major << 8 | minor ))
	version=`read_integer "$wtfs_img" 4096 8`
	if (( output_version != version )); then
		printf "$version\n"
		printf "$output_versionn\n"
		return 1
	fi

	return 0
}

# test magic number in output
function test_magic {
	local grep_magic='grep -Po (?<=magic\snumber:\s{11}0x).*'

	local output_magic=`cat "$stdout" | $grep_magic`
	local magic=`read_integer "$wtfs_img" 4104 8`
	output_magic=`hex_to_dec "$output_magic"`
	if (( output_magic != magic )); then
		printf "$magic\n"
		printf "$output_magic\n"
		return 1
	fi

	return 0
}

# internal helper function
#
# $1: regex to match the number
# $2: read_integer offset
# $3: read_integer length
function __test_equal_int {
	local grep_number="grep -Po $1"

	local expected=`read_integer "$wtfs_img" $2 $3`
	local output=`cat "$stdout" | $grep_number`
	if (( output != expected )); then
		printf "$expected\n"
		printf "$output\n"
		return 1
	fi

	return 0
}

# test block size in output
function test_blk_size {
	__test_equal_int '(?<=block\ssize:\s{13})\d+' 4112 8
	return $?
}

# test total blocks in output
function test_total_blks {
	__test_equal_int '(?<=total\sblocks:\s{11})\d+' 4120 8
	return $?
}

# test itable stuffs in output
function test_itables {
	__test_equal_int '(?<=first\sinode\stable:\s{6})\d+' 4128 8 || return $?
	__test_equal_int '(?<=total\sinode\stables:\s{5})\d+' 4136 8
	return $?
}

# test bmap stuffs in output
function test_bmaps {
	__test_equal_int '(?<=first\sblock\sbitmap:\s{5})\d+' 4144 8 || return $?
	__test_equal_int '(?<=total\sblock\sbitmaps:\s{4})\d+' 4152 8
	return $?
}

# test imap stuffs in output
function test_imaps {
	__test_equal_int '(?<=first\sinode\sbitmap:\s{5})\d+' 4160 8 || return $?
	__test_equal_int '(?<=total\sinode\sbitmaps:\s{4})\d+' 4168 8
	return $?
}

# test total inodes in output
function test_total_inodes {
	__test_equal_int '(?<=total\sinodes:\s{11})\d+' 4176 8
	return $?
}

# test free blocks in output
function test_free_blks {
	__test_equal_int '(?<=free\sblocks:\s{12})\d+' 4184 8
	return $?
}

# test label in output
function test_label {
	local grep_label='grep -Pzo (?<=label:\s{18})(.|\n)*(?=\nUUID:\s{19})'

	# label is supported since v0.3.0
	if (( version < 3 )); then
		return 4
	fi

	local output_label=`cat "$stdout" | $grep_label`
	if [[ "$output_label" != "$label" ]]; then
		printf "$label\n"
		printf "$output_label\n"
		return 1
	fi
	return 0
}

# test UUID in output
function test_uuid {
	local grep_uuid='grep -Po (?<=UUID:\s{19})[\dA-Fa-f-]*'

	# UUID is supported since v0.3.0
	if (( version < 3 )); then
		return 4
	fi

	local output_uuid=`cat "$stdout" | $grep_uuid`
	if [[ "$output_uuid" != "$uuid" ]]; then
		printf "$uuid\n"
		printf "$output_uuid\n"
		return 1
	fi
	return 0
}

# test root directory in output
function test_root_dir {
	# too complex, temporarily skip it
	return 4
}

################################################################################
# following is the execution of the test

# the script must be called by test.sh, so check if the necessary variables
# defined in test.sh are empty or not
if [[ -z "$statfs" ]] || [[ -z "$test_statfs" ]] || [[ -z "$test_dir" ]]; then
	return 1
fi

# now let's do test, first create a file of 100 MB
wtfs_img=`tempfile`
dd if=/dev/zero of="$wtfs_img" bs=1024 count=100000 2> /dev/null
if (( $? != 0 )); then
	printf "unable to create disk image file\n"
	clear_spot
	return 1
fi

# do format on the file
label="test_statfs"
uuid="8c859f2f-c4c6-4d2d-8ed7-86ce9b3864a3"
version=""
"$mkfs" -fq -L "$label" -U "$uuid" "$wtfs_img" 2> /dev/null

# grab statfs output
stdout=`tempfile`
"$statfs" "$wtfs_img" > "$stdout"
if (( $? != 0 )); then
	printf "execution of statfs failed\n"
	clear_spot
	return 1
fi

tests=(
	test_version test_magic test_blk_size test_total_blks
	test_itables test_bmaps	test_imaps test_total_inodes test_free_blks
	test_label test_uuid test_root_dir
)
skipped=0
for part in ${tests[@]}; do
	"$part"
	what $? "$part" || { clear_spot; return 1; }
done

# skipping more than half of all test parts is also regarded as failure
if (( skipped * 2 >= ${#tests} )); then
	printf "too many parts of the test skipped\n"
	clear_spot
	return 1
fi

clear_spot
return 0
