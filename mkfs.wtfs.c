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

static int write_boot_block(int fd);
static int write_super_block(int fd, unsigned long long blocks,
	unsigned long long inode_tables, unsigned long long blk_bitmaps);
static int write_inode_table(int fd, unsigned long long inode_tables);
static int write_block_bitmap(int fd, unsigned long long inode_tables,
	unsigned long long blk_bitmaps);
static int write_inode_bitmap(int fd);
static int write_root_dir(int fd);
static void do_deep_format(int fd, unsigned long long blocks, int quiet);

int main(int argc, char * const * argv)
{
	/* used in argument parse */
	int opt;

	/* flags */
	int quick = 0, quiet = 0;

	/* file descriptor */
	int fd = -1;

	/* bytes and blocks of the device */
	unsigned long long bytes, blocks;

	/* inode tables */
	unsigned long long inode_tables;

	/* block bitmaps */
	unsigned long long blk_bitmaps;

	const char * usage = "Usage: mkfs.wtfs [-fq] device\n\n"
						 "  -f                    quick format\n"
						 "  -q                    quiet mode\n\n";

	/* parse arguments */
	while ((opt = getopt(argc, argv, "fq")) != -1) {
		switch (opt) {
		case 'f':
			quick = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		default:
			fprintf(stderr, "mkfs.wtfs: illegal option -- %c\n", opt);
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
		perror("mkfs.wtfs: cannot open device");
		goto error;
	}

	/* get device size */
	if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
		perror("mkfs.wtfs: unable to get the device size");
		goto error;
	}

	/* do calculation */
	blocks = bytes / WTFS_BLOCK_SIZE;
	inode_tables = WTFS_DATA_SIZE * 8 / WTFS_INODE_COUNT_PER_TABLE + 1;
	blk_bitmaps = blocks / (WTFS_DATA_SIZE * 8);
	if (blocks % (WTFS_DATA_SIZE * 8) != 0) {
		++blk_bitmaps;
	}

	/* do format */
	if (write_boot_block(fd) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the bootloader block failed\n");
		goto error;
	}
	if (write_super_block(fd, blocks, inode_tables, blk_bitmaps) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the super block failed\n");
		goto error;
	}
	if (write_inode_table(fd, inode_tables) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the inode table failed\n");
		goto error;
	}
	if (write_block_bitmap(fd, inode_tables, blk_bitmaps) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the block bitmap failed\n");
		goto error;
	}
	if (write_inode_bitmap(fd) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the inode bitmap failed\n");
		goto error;
	}
	if (write_root_dir(fd) < 0) {
		fprintf(stderr, "mkfs.wtfs: write the root directory failed\n");
		goto error;
	}
	if (quick) {
		if (!quiet) {
			printf("quick format completed\n");
		}
	} else {
		do_deep_format(fd, blocks, quiet);
	}

	close(fd);
	return 0;

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

static int write_super_block(int fd, unsigned long long blocks,
	unsigned long long inode_tables, unsigned long long blk_bitmaps)
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
		.inode_bitmap_count = cpu_to_wtfs64(1),
		.inode_count = cpu_to_wtfs64(1),
		.free_block_count = cpu_to_wtfs64(blocks - inode_tables - blk_bitmaps - 4)
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
static int write_inode_table(int fd, unsigned long long inode_tables)
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
		.gid = cpu_to_wtfs16(getgid())
	};

	/* block number index array */
	unsigned long long * index = NULL;
	unsigned long long i;
	int ret = -EINVAL;

	/* construct index */
	index = (unsigned long long *)calloc(inode_tables + 1,
		sizeof(unsigned long long));
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
	if (write(fd, &inode_table, sizeof(inode_table)) != sizeof(inode_table)) {
		ret = -EIO;
		goto error;
	}

	/* write remaining inode tables */
	memset(&inode_table, 0, sizeof(inode_table));
	for (i = 1; i < inode_tables; ++i) {
		inode_table.next = wtfs64_to_cpu(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &inode_table, sizeof(inode_table)) != sizeof(inode_table)) {
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
static int write_block_bitmap(int fd, unsigned long long inode_tables,
	unsigned long long blk_bitmaps)
{
	/* full bytes fill 0xff */
	unsigned long long full_bytes = (blk_bitmaps + inode_tables + 4) / 8;

	/* half byte fill (1 << half_byte) - 1 */
	unsigned long long half_byte = (blk_bitmaps + inode_tables + 4) % 8;

	/* last full bitmap */
	unsigned long long full = full_bytes / WTFS_DATA_SIZE;

	/* full bytes in half-full bitmap */
	unsigned long long half_full = full_bytes % WTFS_DATA_SIZE;

	/* block number index array */
	unsigned long long * index;
	unsigned long long i, j;
	struct wtfs_data_block blk;
	int ret = -EINVAL;

	/* construct index */
	index = (unsigned long long *)calloc(blk_bitmaps + 1,
		sizeof(unsigned long long));
	if (index == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	index[0] = WTFS_RB_BLOCK_BITMAP;
	for (i = 1; i < blk_bitmaps; ++i) {
		index[i] = WTFS_DB_FIRST + inode_tables - 1 + i;
	}

	/* write full block bitmaps */
	memset(&blk, 0xff, sizeof(blk));
	for (i = 0; i < full; ++i) {
		blk.next = cpu_to_wtfs64(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &blk, sizeof(blk)) != sizeof(blk)) {
			ret = -EIO;
			goto error;
		}
	}

	/* write half-full block bitmap */
	memset(&blk, 0, sizeof(blk));
	for (j = 0; j < half_full; ++j) {
		blk.data[j] = 0xff;
	}
	blk.data[j] = (1 << half_byte) - 1;
	blk.next = cpu_to_wtfs64(index[i + 1]);
	lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &blk, sizeof(blk)) != sizeof(blk)) {
		ret = -EIO;
		goto error;
	}
	++i;

	/* write empty bitmaps */
	memset(&blk, 0, sizeof(blk));
	for (; i < blk_bitmaps; ++i) {
		blk.next = cpu_to_wtfs64(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &blk, sizeof(blk)) != sizeof(blk)) {
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

static int write_inode_bitmap(int fd)
{
	struct wtfs_data_block inode_bitmap = {
		.data = { 0x03 }
	};

	lseek(fd, WTFS_RB_INODE_BITMAP * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &inode_bitmap, sizeof(inode_bitmap)) != sizeof(inode_bitmap)) {
		return -EIO;
	} else {
		return 0;
	}
}

static int write_root_dir(int fd)
{
	struct wtfs_data_block root_dir;
	struct wtfs_dentry dot = {
		.inode_no = WTFS_ROOT_INO,
		.filename = "."
	};
	struct wtfs_dentry dotdot = {
		.inode_no = WTFS_ROOT_INO,
		.filename = ".."
	};

	root_dir.entries[0] = dot;
	root_dir.entries[1] = dotdot;
	lseek(fd, WTFS_DB_FIRST * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &root_dir, sizeof(root_dir)) != sizeof(root_dir)) {
		return -EIO;
	} else {
		return 0;
	}
}

static void do_deep_format(int fd, unsigned long long blocks, int quiet)
{
	unsigned long long i, percent, prev = 0;
	struct wtfs_data_block block;

	memset(&block, 0, sizeof(block));
	if (!quiet) {
		printf("total %llu blocks to format\n", blocks -= WTFS_DB_FIRST + 1);
		printf("\rformat complete 0%%");
		fflush(stdout);
	}
	lseek(fd, (WTFS_DB_FIRST + 1) * WTFS_BLOCK_SIZE, SEEK_SET);
	for (i = 0; i < blocks; ++i) {
		if (write(fd, &block, sizeof(block)) != sizeof(block)) {
			break;
		}
		if (!quiet) {
			percent = (i + 1) * 100 / blocks;
			if (percent > prev) {
				printf("\rformat complete %llu%%", percent);
				fflush(stdout);
				prev = percent;
			}
		}
	}
	if (!quiet) {
		printf("\ndeep format completed\n");
	}
}
