/*
 * statfs.wtfs.c - statfs for wtfs.
 *
 * Copyright (c) 2015 Chaos Shen
 *
 * This file is part of wtfs, What the fxck filesystem.  You may take
 * the letter 'f' from, at your option, either 'fxck' or 'filesystem'.
 *
 * wtfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * wtfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wtfs.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>

#include "wtfs.h"

#define BUF_SIZE 4096

static int read_boot_block(int fd);
static int read_super_block(int fd);
static int read_inode_table(int fd);
static int read_block_bitmap(int fd);
static int read_inode_bitmap(int fd);
static int read_root_dir(int fd);

int main(int argc, char * const * argv)
{
	int fd = -1;
	char err_msg[BUF_SIZE], * tmp = NULL;
	const char * usage = "Usage: statfs.wtfs <DEVICE>\n\n";

	if (argc != 2) {
		fprintf(stderr, "%s", usage);
		goto error;
	}

	/* open device file */
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: cannot open '%s'",
			argv[0], argv[1]);
		perror(err_msg);
		goto error;
	}

	/* read blocks */
	if (read_boot_block(fd) < 0) {
		tmp = "bootloader block";
		goto out;
	}
	if (read_super_block(fd) < 0) {
		tmp = "super block";
		goto out;
	}
	if (read_inode_table(fd) < 0) {
		tmp = "inode table";
		goto out;
	}
	if (read_block_bitmap(fd) < 0) {
		tmp = "block bitmap";
		goto out;
	}
	if (read_inode_bitmap(fd) < 0) {
		tmp = "inode bitmap";
		goto out;
	}
	if (read_root_dir(fd) < 0) {
		tmp = "root directory";
		goto out;
	}

	close(fd);
	return 0;

out:
	fprintf(stderr, "%s: unable to read %s\n", argv[0], tmp);

error:
	if (fd >= 0) {
		close(fd);
	}
	return 1;
}

static int read_boot_block(int fd)
{
	/* do nothing */
	return 0;
}

static int read_super_block(int fd)
{
	struct wtfs_super_block sb;
	uint64_t version;

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		return -EIO;
	}

	if (wtfs64_to_cpu(sb.magic) != WTFS_MAGIC) {
		return -EPERM;
	}

	version = wtfs64_to_cpu(sb.version);
	printf("wtfs on this device\n");
	printf("%-24s%lu.%lu.%lu\n", "version:",
		WTFS_VERSION_MAJOR(version),
		WTFS_VERSION_MINOR(version),
		WTFS_VERSION_PATCH(version));
	printf("%-24s0x%llx\n", "magic number:",
		wtfs64_to_cpu(sb.magic));
	printf("%-24s%llu\n", "block size:",
		wtfs64_to_cpu(sb.block_size));
	printf("%-24s%llu\n", "total blocks:",
		wtfs64_to_cpu(sb.block_count));
	printf("%-24s%llu\n", "first inode table:",
		wtfs64_to_cpu(sb.inode_table_first));
	printf("%-24s%llu\n", "total inode tables:",
		wtfs64_to_cpu(sb.inode_table_count));
	printf("%-24s%llu\n", "first block bitmap:",
		wtfs64_to_cpu(sb.block_bitmap_first));
	printf("%-24s%llu\n", "total block bitmaps:",
		wtfs64_to_cpu(sb.block_bitmap_count));
	printf("%-24s%llu\n", "first inode bitmap:",
		wtfs64_to_cpu(sb.inode_bitmap_first));
	printf("%-24s%llu\n", "total inode bitmaps:",
		wtfs64_to_cpu(sb.inode_bitmap_count));
	printf("%-24s%llu\n", "total inodes:",
		wtfs64_to_cpu(sb.inode_count));
	printf("%-24s%llu\n", "free blocks:",
		wtfs64_to_cpu(sb.free_block_count));
	printf("\n");

	return 0;
}

static int read_inode_table(int fd)
{
	struct wtfs_inode_table inode_table;
	uint64_t next = WTFS_RB_INODE_TABLE;
	int i = 0;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &inode_table, sizeof(inode_table)) !=
			sizeof(inode_table)) {
			return -EIO;
		}

		if (next == WTFS_RB_INODE_TABLE) {
			printf("inode table\n%lu", next);
		} else {
			printf("->%lu", next);
		}
		next = wtfs64_to_cpu(inode_table.next);
		++i;
	}
	printf("\ntotal %d tables\n\n", i);
	return 0;
}

static int read_block_bitmap(int fd)
{
	struct wtfs_bitmap bitmap;
	uint64_t next = WTFS_RB_BLOCK_BITMAP;
	int i = 0;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			return -EIO;
		}

		if (next == WTFS_RB_BLOCK_BITMAP) {
			printf("block bitmap\n%lu", next);
		} else {
			printf("->%lu", next);
		}
		next = wtfs64_to_cpu(bitmap.next);
		++i;
	}
	printf("\ntotal %d bitmaps\n\n", i);
	return 0;
}

static int read_inode_bitmap(int fd)
{
	struct wtfs_bitmap bitmap;
	int i;

	lseek(fd, WTFS_RB_INODE_BITMAP * WTFS_BLOCK_SIZE, SEEK_SET);
	if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		return -EIO;
	}

	printf("inode bitmap\n");
	for (i = 0; i < WTFS_BITMAP_SIZE; ++i) {
		printf("%02x", bitmap.data[i]);
	}
	printf("\n\n");

	return 0;
}

static int read_root_dir(int fd)
{
	struct wtfs_dir_block root_blk;
	int i;
	uint64_t next = WTFS_DB_FIRST, inode_no;
	char * filename = NULL;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &root_blk, sizeof(root_blk)) != sizeof(root_blk)) {
			return -EIO;
		}

		if (next == WTFS_DB_FIRST) {
			printf("root directory\n");
		}

		for (i = 0; i < WTFS_DENTRY_COUNT_PER_BLOCK; ++i) {
			inode_no = wtfs64_to_cpu(root_blk.entries[i].inode_no);
			filename = root_blk.entries[i].filename;
			if (inode_no != 0) {
				printf("%lu  %s\n", inode_no, filename);
			}
		}
		next = wtfs64_to_cpu(root_blk.next);
	}
	printf("\n");

	return 0;
}
