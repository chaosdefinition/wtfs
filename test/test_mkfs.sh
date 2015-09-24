#!/bin/bash

# test script for mkfs.wtfs.
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
		return 0
		;;
	1 )
		printf "a bug found in $2\n"
		;;
	2 )
		printf "execution of mkfs failed in $2\n"
		;;
	3 )
		printf "execution of a command failed in $2\n"
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
	unset tests
}

################################################################################
# following are test functions
# see function 'what' for explanation of return value

# test the option 'f', 'fast'
function test_fast {
	"$mkfs" -f "$wtfs_img" > /dev/null
	if (( $? != 0 )); then
		return 1
	fi

	return 0
}

# test the option 'q', 'quiet'
function test_quiet {
	local stdout=`tempfile`

	"$mkfs" -fq "$wtfs_img" > "$stdout"
	if (( $? != 0 )); then
		rm -rf "$stdout"
		return 2
	elif [[ -n `cat "$stdout"` ]]; then
		rm -rf "$stdout"
		return 1
	fi

	rm -rf "$stdout"
	return 0
}

# test the option 'F', 'force'
# udisks2 and gvfs-bin are required to mount and unmount disk image without sudo
function test_force {
	local loop_dev=""
	local setup_loop="udisksctl loop-setup --file=$wtfs_img"
	local mount_img=""
	local unmount_img=""
	local grep_loop='grep -Po /dev/loop\d+'

	# skip the test if command 'udisksctl' and 'gvfs-mount' are missing
	if ! which udisksctl gvfs-mount > /dev/null; then
		return 4
	fi

	# first make an ext4 image and do mount
	mkfs.ext4 -FFq "$wtfs_img" 2> /dev/null
	loop_dev=`$setup_loop | $grep_loop`
	if (( $? != 0 )); then
		return 3
	fi
	mount_img="gvfs-mount -d $loop_dev"
	unmount_img="udisksctl unmount --block-device $loop_dev"
	$mount_img > /dev/null
	if (( $? != 0 )); then
		return 3
	fi

	# then do mkfs without '-F' again, there should be an error
	"$mkfs" -fq "$wtfs_img" 2> /dev/null
	if (( $? == 0 )); then
		$unmount_img > /dev/null
		return 1
	fi

	# then do mkfs with '-F', there should be no error
	"$mkfs" -fqF "$wtfs_img"
	if (( $? != 0 )); then
		$unmount_img > /dev/null
		return 1
	fi

	$unmount_img > /dev/null
	return 0
}

# test the option 'i', 'imaps'
function test_imaps {
	# normal case
	"$mkfs" -fq -i1 "$wtfs_img"
	if (( $? != 0 )); then
		return 2
	fi

	# too many imaps
	"$mkfs" -fq -i100 "$wtfs_img" 2> /dev/null
	if (( $? == 0 )); then
		return 1
	fi

	# invalid imap number
	"$mkfs" -fq -i-2 "$wtfs_img" 2> /dev/null
	if (( $? == 0 )); then
		return 1
	fi

	return 0
}

# test the option 'L', 'label'
function test_label {
	local label=""
	local label2=""

	# normal case
	label="This is a label"
	"$mkfs" -fq -L "$label" "$wtfs_img"
	if (( $? != 0 )); then
		return 2
	fi
	label2=`tail -c+4192 "$wtfs_img" | head -c32`
	if [[ "$label" != "$label2" ]]; then
		return 1
	fi

	# label too long
	label="This is a very very very very very very long label"
	"$mkfs" -fq -L "$label" "$wtfs_img" 2> /dev/null
	if (( $? == 0 )); then
		printf `tail -c+4193 "$wtfs_img" | head -c32`
		return 1
	fi

	return 0
}

# test the option 'U', 'uuid'
# uuid is required to generate random UUID and unparse binary UUID
function test_uuid {
	local uuid=""
	local uuid2=""
	local unparse_uuid="uuid -d -FBIN -"
	local grep_uuid='grep -Po [\dA-Fa-f]{8}(-[\dA-Fa-f]{4}){3}-[\dA-Fa-f]{12}'

	# skip this test if command 'uuid' is missing
	if ! which uuid > /dev/null; then
		return 4
	fi

	# normal case
	uuid=`uuid -v4`
	"$mkfs" -fq -U "$uuid" "$wtfs_img"
	if (( $? != 0 )); then
		return 2
	fi
	uuid2=`tail -c+4225 "$wtfs_img" | head -c16 | $unparse_uuid | $grep_uuid`
	if [[ "$uuid" != "$uuid2" ]]; then
		printf "$uuid\n"
		printf "$uuid2\n"
		return 1
	fi

	# invalid UUID
	uuid="12345678-90ab-cdef-ghij-klmnopqrstuv"
	"$mkfs" -fq -U "$uuid" "$wtfs_img" 2> /dev/null
	if (( $? == 0 )); then
		return 1
	fi

	return 0
}

# test the option 'V', 'version'
function test_version {
	# no need
	return 4
}

# test the option 'h', 'help'
function test_help {
	# no need
	return 4
}

################################################################################
# following is the execution of the test

# the script must be called by test.sh, so check if the necessary variables
# defined in test.sh are empty or not
if [[ -z "$mkfs" ]] || [[ -z "$test_mkfs" ]] || [[ -z "$test_dir" ]]; then
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

tests=(
	test_fast test_quiet test_force
	test_imaps test_label test_uuid
	test_version test_help
)
for part in ${tests[@]}; do
	"$part"
	what $? "$part"
	if (( $? != 0 )); then
		clear_spot
		return 1
	fi
done

clear_spot
return 0
