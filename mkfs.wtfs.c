/*
 * mkfs.wtfs.c - mkfs for wtfs.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>

#include "wtfs.h"

#define BUF_SIZE 4096

static int write_boot_block(int fd);
static int write_super_block(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps);
static int write_inode_table(int fd, uint64_t inode_tables);
static int write_block_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps);
static int write_inode_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps);
static int write_root_dir(int fd);
static void do_deep_format(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps, int quiet);

int main(int argc, char * const * argv)
{
	/* used in argument parse */
	int opt;

	/* flags */
	int quick = 0, quiet = 0;

	/* file descriptor */
	int fd = -1;

	/* bytes and blocks of the device */
	uint64_t bytes, blocks;

	/* inode tables */
	uint64_t inode_tables;

	/* block bitmaps */
	uint64_t blk_bitmaps;

	/* inode bitmaps */
	uint64_t inode_bitmaps;

	char err_msg[BUF_SIZE], * tmp = NULL;

	struct stat stat;

	const char * usage = "Usage: mkfs.wtfs [OPTIONS] <DEVICE>\n"
			     "Options:\n"
			     "  -f                    quick format\n"
			     "  -q                    quiet mode\n"
			     "  -V                    show version and exit\n"
			     "  -h                    show this message and exit\n"
			     "\n";

	/* parse arguments */
	while ((opt = getopt(argc, argv, "fqVh")) != -1) {
		switch (opt) {
		case 'f':
			quick = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'V':
			printf("\n%s version %d.%d.%d\n\n", argv[0],
				WTFS_VERSION_MAJOR(WTFS_VERSION),
				WTFS_VERSION_MINOR(WTFS_VERSION),
				WTFS_VERSION_PATCH(WTFS_VERSION));
			return 0;

		case 'h':
			printf("%s", usage);
			return 0;

		default:
			fprintf(stderr, "%s: illegal option -- %c\n",
				argv[0], opt);
			printf("%s", usage);
			goto error;
		}
	}

	/* no device name */
	if (optind >= argc) {
		printf("%s", usage);
		goto error;
	}

	/* open device file */
	if ((fd = open(argv[optind], O_RDWR)) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: cannot open '%s'",
			argv[0], argv[optind]);
		perror(err_msg);
		goto error;
	}

	/* get device file stat */
	if (fstat(fd, &stat) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: unable to stat '%s'",
			argv[0], argv[optind]);
		perror(err_msg);
		goto error;
	}

	switch (stat.st_mode & S_IFMT) {
	/* block device */
	case S_IFBLK:
		/* get device size */
		if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
			snprintf(err_msg, BUF_SIZE,
				"%s: unable to get the size of '%s'",
				argv[0], argv[optind]);
			perror(err_msg);
			goto error;
		}
		break;

	/* regular file */
	case S_IFREG:
		bytes = stat.st_size;
		break;

	default:
		fprintf(stderr,
			"%s: only block device and regular file supported\n",
			argv[0]);
		goto error;
	}

	/* do calculation */
	blocks = bytes / WTFS_BLOCK_SIZE;
	inode_tables = WTFS_BITMAP_SIZE * 8 / WTFS_INODE_COUNT_PER_TABLE + 1;
	blk_bitmaps = blocks / (WTFS_BITMAP_SIZE * 8);
	inode_bitmaps = 1;
	if (blocks < inode_tables + blk_bitmaps + inode_bitmaps + 3) {
		snprintf(err_msg, BUF_SIZE, "%s: volume too small", argv[0]);
		perror(err_msg);
		goto error;
	}
	if (blocks % (WTFS_BITMAP_SIZE * 8) != 0) {
		++blk_bitmaps;
	}

	/* do format */
	if (write_boot_block(fd) < 0) {
		tmp = "bootloader block";
		goto out;
	}
	if (write_super_block(fd, blocks, inode_tables, blk_bitmaps,
			inode_bitmaps) < 0) {
		tmp = "super block";
		goto out;
	}
	if (write_inode_table(fd, inode_tables) < 0) {
		tmp = "inode table";
		goto out;
	}
	if (write_block_bitmap(fd, inode_tables, blk_bitmaps) < 0) {
		tmp = "block bitmap";
		goto out;
	}
	if (write_inode_bitmap(fd, inode_tables, blk_bitmaps,
			inode_bitmaps) < 0) {
		tmp = "inode bitmap";
		goto out;
	}
	if (write_root_dir(fd) < 0) {
		tmp = "root directory";
		goto out;
	}
	if (quick) {
		if (!quiet) {
			printf("quick format completed\n");
		}
	} else {
		do_deep_format(fd, blocks, inode_tables, blk_bitmaps,
			inode_bitmaps, quiet);
	}

	close(fd);
	return 0;

out:
	fprintf(stderr, "%s: write %s failed\n", argv[0], tmp);

error:
	if (fd >= 0) {
		close(fd);
	}
	return 1;
}

static int write_boot_block(int fd)
{
	struct wtfs_data_block block;

	memset(&block, 0, sizeof(block));
	lseek(fd, WTFS_RB_BOOT * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &block, sizeof(block)) != sizeof(block)) {
		return -EIO;
	} else {
		return 0;
	}
}

static int write_super_block(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps)
{
	struct wtfs_super_block sb = {
		.version = cpu_to_wtfs64(WTFS_VERSION),
		.magic = cpu_to_wtfs64(WTFS_MAGIC),
		.block_size = cpu_to_wtfs64(WTFS_BLOCK_SIZE),
		.block_count = cpu_to_wtfs64(blocks),
		.inode_table_first = cpu_to_wtfs64(WTFS_RB_INODE_TABLE),
		.inode_table_count = cpu_to_wtfs64(inode_tables),
		.block_bitmap_first = cpu_to_wtfs64(WTFS_RB_BLOCK_BITMAP),
		.block_bitmap_count = cpu_to_wtfs64(blk_bitmaps),
		.inode_bitmap_first = cpu_to_wtfs64(WTFS_RB_INODE_BITMAP),
		.inode_bitmap_count = cpu_to_wtfs64(inode_bitmaps),
		.inode_count = cpu_to_wtfs64(1),
		.free_block_count = cpu_to_wtfs64(blocks - inode_tables -
			blk_bitmaps - inode_bitmaps - 3),
	};

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		return -EIO;
	} else {
		return 0;
	}

	return 0;
}

/*
 * there are at most 4088 * 8 inodes, which requires only 520 inode tables,
 * so we pre-build the whole inode table for the device
 */
static int write_inode_table(int fd, uint64_t inode_tables)
{
	/* buffer to write */
	struct wtfs_inode_table inode_table;

	/* inode for root dir */
	struct wtfs_inode inode = {
		.inode_no = cpu_to_wtfs64(WTFS_ROOT_INO),
		.dir_entry_count = cpu_to_wtfs64(2),
		.block_count = cpu_to_wtfs64(1),
		.first_block = cpu_to_wtfs64(WTFS_DB_FIRST),
		.atime = cpu_to_wtfs64(time(NULL)),
		.ctime = cpu_to_wtfs64(time(NULL)),
		.mtime = cpu_to_wtfs64(time(NULL)),
		.mode = cpu_to_wtfs32(S_IFDIR | 0755),
		.uid = cpu_to_wtfs16(getuid()),
		.gid = cpu_to_wtfs16(getgid()),
	};

	/* block number index array */
	uint64_t * index = NULL;
	uint64_t i;
	int ret = -EINVAL;

	/* construct index */
	index = (uint64_t *)calloc(inode_tables + 1, sizeof(uint64_t));
	if (index == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	index[0] = WTFS_RB_INODE_TABLE;
	for (i = 1; i < inode_tables; ++i) {
		index[i] = WTFS_DB_FIRST + i;
	}

	/* write 1st inode table */
	memset(&inode_table, 0, sizeof(inode_table));
	inode_table.inodes[0] = inode;
	inode_table.next = wtfs64_to_cpu(index[1]);
	lseek(fd, index[0] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &inode_table, sizeof(inode_table)) !=
		sizeof(inode_table)) {
		ret = -EIO;
		goto error;
	}

	/* write remaining inode tables */
	memset(&inode_table, 0, sizeof(inode_table));
	for (i = 1; i < inode_tables; ++i) {
		inode_table.next = wtfs64_to_cpu(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &inode_table, sizeof(inode_table)) !=
			sizeof(inode_table)) {
			ret = -EIO;
			goto error;
		}
	}

	free(index);
	return 0;

error:
	if (index != NULL) {
		free(index);
	}
	return ret;
}

/*
 * since the device size is fixed,
 * we pre-build the whole block bitmap for the device
 */
static int write_block_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps)
{
	/* full bytes fill 0xff */
	uint64_t full_bytes = (blk_bitmaps + inode_tables + 4) / 8;

	/* half byte fill (1 << half_byte) - 1 */
	uint64_t half_byte = (blk_bitmaps + inode_tables + 4) % 8;

	/* last full bitmap */
	uint64_t full = full_bytes / WTFS_BITMAP_SIZE;

	/* full bytes in half-full bitmap */
	uint64_t half_full = full_bytes % WTFS_BITMAP_SIZE;

	/* block number index array */
	uint64_t * index;
	uint64_t i, j;
	struct wtfs_bitmap bitmap;
	int ret = -EINVAL;

	/* construct index */
	index = (uint64_t *)calloc(blk_bitmaps + 1, sizeof(uint64_t));
	if (index == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	index[0] = WTFS_RB_BLOCK_BITMAP;
	for (i = 1; i < blk_bitmaps; ++i) {
		index[i] = WTFS_DB_FIRST + inode_tables - 1 + i;
	}

	/* write full block bitmaps */
	memset(&bitmap, 0xff, sizeof(bitmap));
	for (i = 0; i < full; ++i) {
		bitmap.next = cpu_to_wtfs64(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			ret = -EIO;
			goto error;
		}
	}

	/* write half-full block bitmap */
	memset(&bitmap, 0, sizeof(bitmap));
	for (j = 0; j < half_full; ++j) {
		bitmap.data[j] = 0xff;
	}
	bitmap.data[j] = (1 << half_byte) - 1;
	bitmap.next = cpu_to_wtfs64(index[i + 1]);
	lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		ret = -EIO;
		goto error;
	}
	++i;

	/* write empty bitmaps */
	memset(&bitmap, 0, sizeof(bitmap));
	for (; i < blk_bitmaps; ++i) {
		bitmap.next = cpu_to_wtfs64(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			ret = -EIO;
			goto error;
		}
	}

	free(index);
	return 0;

error:
	if (index != NULL) {
		free(index);
	}
	return ret;
}

static int write_inode_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps)
{
	struct wtfs_bitmap bitmap = {
		.data = { 0x03 },
	};

	lseek(fd, WTFS_RB_INODE_BITMAP * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		return -EIO;
	} else {
		return 0;
	}
}

static int write_root_dir(int fd)
{
	struct wtfs_dir_block root_blk;
	struct wtfs_dentry dot = {
		.inode_no = WTFS_ROOT_INO,
		.filename = ".",
	};
	struct wtfs_dentry dotdot = {
		.inode_no = WTFS_ROOT_INO,
		.filename = "..",
	};

	root_blk.entries[0] = dot;
	root_blk.entries[1] = dotdot;
	lseek(fd, WTFS_DB_FIRST * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &root_blk, sizeof(root_blk)) != sizeof(root_blk)) {
		return -EIO;
	} else {
		return 0;
	}
}

static void do_deep_format(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps, int quiet)
{
	uint64_t start = WTFS_DB_FIRST + inode_tables + blk_bitmaps +
		inode_bitmaps - 2;
	uint64_t i, percent, prev = 0;
	struct wtfs_data_block blk;

	memset(&blk, 0, sizeof(blk));
	if (!quiet) {
		printf("total %lu blocks to format\n", (blocks -= start));
		printf("\rformat complete 0%%");
		fflush(stdout);
	}
	lseek(fd, start * WTFS_BLOCK_SIZE, SEEK_SET);
	for (i = 0; i < blocks; ++i) {
		if (write(fd, &blk, sizeof(blk)) != sizeof(blk)) {
			break;
		}
		if (!quiet) {
			percent = (i + 1) * 100 / blocks;
			if (percent > prev) {
				printf("\rformat complete %lu%%", percent);
				fflush(stdout);
				prev = percent;
			}
		}
	}
	if (!quiet) {
		printf("\ndeep format completed\n");
	}
}
