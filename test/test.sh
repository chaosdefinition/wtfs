#!/bin/bash

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

# $1: test taker name
# $2: test taker path
# $3: test maker path
function do_test {
	if [[ -f "$2" ]] && [[ -f "$3" ]]; then
		. "$3"
		if [[ $? -ne 0 ]]; then
			printf "$1 failed to pass the test\n"
			exit 1
		fi
	else
		printf "$1 is not ready for the test\n"
	fi
}

# do tests
do_test "mkfs.wtfs" "$mkfs" "$test_mkfs"
do_test "statfs.wtfs" "$statfs" "$test_statfs"
do_test "wtfs module" "$module" "$test_module"

printf "test OK!\n"
exit 0
