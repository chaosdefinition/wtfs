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
#include <string.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>

#include "wtfs.h"

static int read_boot_block(int fd);
static int read_super_block(int fd);
static int read_inode_table(int fd);
static int read_block_bitmap(int fd);
static int read_inode_bitmap(int fd);
static int read_root_dir(int fd);

int main(int argc, char * const * argv)
{
	int fd = -1;
	const char * usage = "Usage: statfs.wtfs device\n\n";

	if (argc != 2) {
		fprintf(stderr, "%s", usage);
		goto error;
	}

	/* open device file */
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		perror("mkfs.wtfs: cannot open device");
		goto error;
	}

	if (read_boot_block(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the bootloader block\n");
		goto error;
	}
	if (read_super_block(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the super block\n");
		goto error;
	}
	if (read_inode_table(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the inode table\n");
		goto error;
	}
	if (read_block_bitmap(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the block bitmap\n");
		goto error;
	}
	if (read_inode_bitmap(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the inode bitmap\n");
		goto error;
	}
	if (read_root_dir(fd) < 0) {
		fprintf(stderr, "statfs.wtfs: unable to read the root directory\n");
		goto error;
	}

	close(fd);
	return 0;

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

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		return -EIO;
	}

	if (wtfs64_to_cpu(sb.magic) != WTFS_MAGIC) {
		printf("magic number mismatch: 0x%llx\n", wtfs64_to_cpu(sb.magic));
		return 0;
	}

	printf("wtfs on this device\n");
	printf("version:                %llu\n", wtfs64_to_cpu(sb.version));
	printf("magic number:           0x%llx\n", wtfs64_to_cpu(sb.magic));
	printf("block size:             %llu\n", wtfs64_to_cpu(sb.block_size));
	printf("total blocks:           %llu\n", wtfs64_to_cpu(sb.block_count));
	printf("first inode table:      %llu\n", wtfs64_to_cpu(sb.inode_table_first));
	printf("total inode tables:     %llu\n", wtfs64_to_cpu(sb.inode_table_count));
	printf("first block bitmap:     %llu\n", wtfs64_to_cpu(sb.block_bitmap_first));
	printf("total block bitmaps:    %llu\n", wtfs64_to_cpu(sb.block_bitmap_count));
	printf("first inode bitmap:     %llu\n", wtfs64_to_cpu(sb.inode_bitmap_first));
	printf("total inode bitmaps:    %llu\n", wtfs64_to_cpu(sb.inode_bitmap_count));
	printf("total inodes:           %llu\n", wtfs64_to_cpu(sb.inode_count));
	printf("free blocks:            %llu\n\n", wtfs64_to_cpu(sb.free_block_count));

	return 0;
}

static int read_inode_table(int fd)
{
	struct wtfs_data_block bitmap;
	unsigned long long next = WTFS_RB_INODE_TABLE;
	int i = 0;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			return -EIO;
		}

		if (next == WTFS_RB_INODE_TABLE) {
			printf("inode table\n%llu", next);
		} else {
			printf("->%llu", next);
		}
		next = wtfs64_to_cpu(bitmap.next);
		++i;
	}
	printf("\ntotal %d tables\n\n", i);
	return 0;
}

static int read_block_bitmap(int fd)
{
	struct wtfs_data_block bitmap;
	unsigned long long next = WTFS_RB_BLOCK_BITMAP;
	int i = 0;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			return -EIO;
		}

		if (next == WTFS_RB_BLOCK_BITMAP) {
			printf("block bitmap\n%llu", next);
		} else {
			printf("->%llu", next);
		}
		next = wtfs64_to_cpu(bitmap.next);
		++i;
	}
	printf("\ntotal %d bitmaps\n\n", i);
	return 0;
}

static int read_inode_bitmap(int fd)
{
	/* do nothing */
	return 0;
}

static int read_root_dir(int fd)
{
	struct wtfs_data_block root_dir;
	int i;
	unsigned long long next = WTFS_DB_FIRST;

	while (next != 0) {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &root_dir, sizeof(root_dir)) != sizeof(root_dir)) {
			return -EIO;
		}

		if (next == WTFS_DB_FIRST) {
			printf("root directory\n");
		}

		i = 0;
		while (root_dir.entries[i].inode_no != 0) {
			printf("%llu\t\t%s\n", root_dir.entries[i].inode_no,
				root_dir.entries[i].filename);
			++i;
		}
		next = wtfs64_to_cpu(root_dir.next);
	}
	printf("\n");

	return 0;
}
