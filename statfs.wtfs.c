/*
 * statfs.wtfs.c - statfs for wtfs.
 *
 * Copyright (C) 2015 Chaos Shen
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>
#include <uuid/uuid.h>

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
	int ret;
	char err_msg[BUF_SIZE], buf[BUF_SIZE], * part = NULL;
	struct stat stat;
	const char * usage = "Usage: statfs.wtfs <FILE>\n"
			     "FILE can be a block device containing a wtfs "
			     "instance, or any file within a\n"
			     "wtfs instance\n";

	if (argc != 2) {
		fprintf(stderr, "%s", usage);
		goto error;
	}

	/* open and stat input file */
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: cannot open '%s'",
			argv[0], argv[1]);
		perror(err_msg);
		goto error;
	}
	if (fstat(fd, &stat) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: unable to stat '%s'",
			argv[0], argv[1]);
		perror(err_msg);
		goto error;
	}

	switch (stat.st_mode & S_IFMT) {
	/* block device containing a wtfs instance */
	case S_IFBLK:
		break;

	/* file within a wtfs instance */
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		snprintf(buf, BUF_SIZE, "/dev/block/%u:%u",
			major(stat.st_dev), minor(stat.st_dev));
		close(fd);
		if ((fd = open(buf, O_RDONLY)) < 0) {
			snprintf(err_msg, BUF_SIZE, "%s: cannot open '%s'",
				buf, argv[1]);
			perror(err_msg);
			goto error;
		}
		break;

	default:
		fprintf(stderr, "%s: no wtfs instance found\n", argv[0]);
		goto error;
	}

	/* read blocks */
	if ((ret = read_boot_block(fd)) < 0) {
		part = "bootloader block";
		goto out;
	}
	if ((ret = read_super_block(fd)) < 0) {
		if (ret == -EPERM) {
			fprintf(stderr, "%s: no wtfs instance found\n",
				argv[0]);
			goto error;
		} else {
			part = "super block";
			goto out;
		}
	}
	if ((ret = read_inode_table(fd)) < 0) {
		part = "inode table";
		goto out;
	}
	if ((ret = read_block_bitmap(fd)) < 0) {
		part = "block bitmap";
		goto out;
	}
	if ((ret = read_inode_bitmap(fd)) < 0) {
		part = "inode bitmap";
		goto out;
	}
	if ((ret = read_root_dir(fd)) < 0) {
		part = "root directory";
		goto out;
	}

	close(fd);
	return 0;

out:
	fprintf(stderr, "%s: unable to read %s\n", argv[0], part);

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
	char uuid_buffer[36 + 1];

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
	if (strnlen(sb.label, WTFS_LABEL_MAX) != 0) {
		printf("%-24s%s\n", "label:", sb.label);
	}
	if (!uuid_is_null(sb.uuid)) {
		uuid_unparse(sb.uuid, uuid_buffer);
		uuid_buffer[36] = '\0';
		printf("%-24s%s\n", "UUID:", uuid_buffer);
	}
	printf("\n");

	return 0;
}

static int read_inode_table(int fd)
{
	/* do nothing */
	return 0;
}

static int read_block_bitmap(int fd)
{
	/* do nothing */
	return 0;
}

static int read_inode_bitmap(int fd)
{
	/* do nothing */
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
