/*
 * mkfs.wtfs.c - mkfs for wtfs.
 *
 * Copyright (C) 2015-2017 Chaos Shen
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
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <libmount/libmount.h>

#include <wtfs/wtfs.h>

#define BUF_SIZE 4096

static int check_mounted_fs(const char * filename);
static int write_boot_block(int fd);
static int write_super_block(int fd, uint64_t blocks, uint64_t itables,
			     uint64_t bmaps, uint64_t imaps,
			     const char * label, uuid_t uuid);
static int write_inode_table(int fd, uint64_t itables);
static int write_block_bitmap(int fd, uint64_t itables, uint64_t bmaps,
			      uint64_t imaps);
static int write_inode_bitmap(int fd, uint64_t itables, uint64_t bmaps,
			      uint64_t imaps);
static int write_root_dir(int fd);
static void do_deep_format(int fd, uint64_t blocks, uint64_t itables,
			   uint64_t bmaps, uint64_t imaps, int quiet);

int main(int argc, char * const * argv)
{
	/* Used in argument parsing */
	int opt;
	struct option long_options[] = {
		{ "fast", no_argument, NULL, 'f' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "force", no_argument, NULL, 'F' },
		{ "label", required_argument, NULL, 'L' },
		{ "uuid", required_argument, NULL, 'U' },
		{ "version", no_argument, NULL, 'V' },
		{ "help", no_argument, NULL, 'h' },
		{ 0, 0, 0, 0 },
	};

	/* Flags */
	int quick = 0, quiet = 0, force = 0;

	/* File descriptor */
	int fd = -1;

	/* Bytes and blocks of the device */
	uint64_t bytes, blocks;

	/* Inode tables (default 1) */
	uint64_t itables = 1;

	/* Block bitmaps */
	uint64_t bmaps;

	/* Inode bitmaps (default 1) */
	int64_t imaps = 1;

	/* Filesystem label */
	char * label = NULL;

	/* Filesystem UUID */
	uuid_t uuid = { 0 };

	char err_msg[BUF_SIZE];
	const char * part = NULL;

	struct stat stat;

	const char * usage = "Usage: mkfs.wtfs [OPTIONS] <DEVICE>\n"
			     "\n"
			     "Make a wtfs filesystem.\n"
			     "\n"
			     "Options:\n"
			     "  -f, --fast            quick format\n"
			     "  -q, --quiet           quiet mode\n"
			     "  -F, --force           force execution\n"
			     "  -L, --label=LABEL     set filesystem label\n"
			     "  -U, --uuid=UUID       set filesystem UUID\n"
			     "  -V, --version         show version and exit\n"
			     "  -h, --help            show this message and exit\n"
			     "\n"
			     "See mkfs.wtfs(8) for more details.\n"
			     "\n";

	/* Parse arguments */
	while ((opt = getopt_long(argc, argv, "fqFi:L:U:Vh",
				  long_options, NULL)) != -1) {
		switch (opt) {
		case 'f':
			quick = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'F':
			force = 1;
			break;
		case 'L':
			label = optarg;
			if (strnlen(label, WTFS_LABEL_MAX) == WTFS_LABEL_MAX) {
				fprintf(stderr, "%s: Label too long\n",
					argv[0]);
				goto error;
			}
			break;
		case 'U':
			if (uuid_parse(optarg, uuid) < 0) {
				fprintf(stderr, "%s: Invalid UUID '%s'\n",
					argv[0], optarg);
				goto error;
			}
			break;
		case 'V':
			printf("\nmkfs.wtfs version %d.%d.%d\n\n",
			       WTFS_VERSION_MAJOR(WTFS_VERSION),
			       WTFS_VERSION_MINOR(WTFS_VERSION),
			       WTFS_VERSION_PATCH(WTFS_VERSION));
			return 0;
		case 'h':
			printf("%s", usage);
			return 0;
		case '?':
			printf("%s", usage);
			goto error;
		default:
			fprintf(stderr, "%s: Illegal option -- %c\n",
				argv[0], opt);
			printf("%s", usage);
			goto error;
		}
	}

	/* No device name */
	if (optind >= argc) {
		printf("%s", usage);
		goto error;
	}

	/* Open device file */
	if ((fd = open(argv[optind], O_RDWR)) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: Cannot open '%s'",
			 argv[0], argv[optind]);
		perror(err_msg);
		goto error;
	}

	/* Get device file stat */
	if (fstat(fd, &stat) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: Failed to stat '%s'",
			 argv[0], argv[optind]);
		perror(err_msg);
		goto error;
	}

	switch (stat.st_mode & S_IFMT) {
	/* Block device */
	case S_IFBLK:
		/* Get device size */
		if (ioctl(fd, BLKGETSIZE64, &bytes) < 0) {
			snprintf(err_msg, BUF_SIZE,
				 "%s: Failed to get the size of '%s'",
				 argv[0], argv[optind]);
			perror(err_msg);
			goto error;
		}
		break;
	/* Regular file */
	case S_IFREG:
		bytes = stat.st_size;
		break;
	default:
		fprintf(stderr, "%s: Only block device and regular file are "
			"supported\n", argv[0]);
		goto error;
	}

	/*
	 * Do calculation.
	 * Since the device size is fixed, we pre-build the whole block bitmap
	 * for the device.
	 */
	blocks = bytes / WTFS_BLOCK_SIZE;
	bmaps = blocks / (WTFS_BITMAP_SIZE * 8);
	if (blocks % (WTFS_BITMAP_SIZE * 8) != 0) {
		++bmaps;
	}
	if (blocks < itables + bmaps + imaps + 3) {
		fprintf(stderr, "%s: Volume too small\n", argv[0]);
		goto error;
	}

	/*
	 * Check if the filesystem is already mounted when option 'force' is
	 * not specified.
	 */
	if (!force) {
		/* Reuse opt */
		opt = check_mounted_fs(argv[optind]);
		if (opt < 0) {
			fprintf(stderr, "%s: An error occurred when checking "
				"mounted filesystems\n", argv[0]);
			goto error;
		} else if (opt == 1) {
			fprintf(stderr, "%s: '%s' is already mounted\n",
				argv[0], argv[optind]);
			goto error;
		}
	}

	/* Do format */
	if (write_boot_block(fd) < 0) {
		part = "bootloader block";
		goto out;
	}
	if (write_super_block(fd, blocks, itables, bmaps, imaps,
			      label, uuid) < 0) {
		part = "super block";
		goto out;
	}
	if (write_inode_table(fd, itables) < 0) {
		part = "inode table";
		goto out;
	}
	if (write_block_bitmap(fd, itables, bmaps, imaps) < 0) {
		part = "block bitmap";
		goto out;
	}
	if (write_inode_bitmap(fd, itables, bmaps, imaps) < 0) {
		part = "inode bitmap";
		goto out;
	}
	if (write_root_dir(fd) < 0) {
		part = "root directory";
		goto out;
	}
	if (quick) {
		if (!quiet) {
			printf("Quick format completed\n");
		}
	} else {
		do_deep_format(fd, blocks, itables, bmaps, imaps, quiet);
	}

	close(fd);
	return 0;

out:
	fprintf(stderr, "%s: Failed to write %s\n", argv[0], part);

error:
	if (fd >= 0) {
		close(fd);
	}
	return 1;
}

/*
 * Check if the given file (device or filesystem image) is mounted.
 *
 * @filename: file name of the device or image
 *
 * return: 0 or 1 on success, error code otherwise
 */
static int check_mounted_fs(const char * filename)
{
	struct libmnt_context * ctx = NULL;
	struct libmnt_table * tbl = NULL;
	struct libmnt_iter * itr = NULL;
	struct libmnt_fs * fs = NULL;
	struct libmnt_cache * cache = NULL;
	const char * src = NULL, * type = NULL;
	char * xsrc = NULL;
	char buf[BUF_SIZE];
	int ret, done;

	/* First get the canonical path of the file */
	if (realpath(filename, buf) == NULL) {
		ret = -EINVAL;
		goto error;
	}

	/* Initialize libmount */
	if ((ctx = mnt_new_context()) == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	if ((ret = mnt_context_get_mtab(ctx, &tbl)) < 0) {
		goto error;
	}
	if ((itr = mnt_new_iter(MNT_ITER_FORWARD)) == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	if ((cache = mnt_new_cache()) == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	/* Do iterate mounted filesystems */
	done = 0;
	while (!done) {
		ret = mnt_table_next_fs(tbl, itr, &fs);
		if (ret < 0) {
			goto error;
		} else if (ret == 1) {
			done = 1;
			ret = 0;
			break;
		}

		src = mnt_fs_get_source(fs);
		type = mnt_fs_get_fstype(fs);
		if (!mnt_fstype_is_pseudofs(type)) {
			xsrc = mnt_pretty_path(src, cache);
		}
		if (src != NULL) {
			if (strcmp(buf, xsrc == NULL ? src : xsrc) == 0) {
				done = 1;
				ret = 1;
			}
		}
		if (xsrc != NULL) {
			free(xsrc);
			xsrc = NULL;
		}
	}

	mnt_free_cache(cache);
	mnt_free_iter(itr);
	mnt_free_context(ctx);
	return ret;

error:
	if (cache != NULL) {
		mnt_free_cache(cache);
	}
	if (itr != NULL) {
		mnt_free_iter(itr);
	}
	if (ctx != NULL) {
		mnt_free_context(ctx);
	}
	return ret;
}

/*
 * Write boot loader block to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 *
 * return: 0 on success, error code otherwise
 */
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

/*
 * Write super block to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 * @blocks: number of blocks
 * @itables: number of inode tables
 * @bmaps: number of block bitmaps
 * @imaps: number of inode bitmaps
 * @label: filesystem label
 * @uuid: filesystem UUID
 *
 * return: 0 on success, error code otherwise
 */
static int write_super_block(int fd, uint64_t blocks, uint64_t itables,
			     uint64_t bmaps, uint64_t imaps,
			     const char * label, uuid_t uuid)
{
	struct wtfs_super_block sb = {
		.version = cpu_to_wtfs64(WTFS_VERSION),
		.magic = cpu_to_wtfs64(WTFS_MAGIC),
		.block_size = cpu_to_wtfs64(WTFS_BLOCK_SIZE),
		.block_count = cpu_to_wtfs64(blocks),
		.inode_table_first = cpu_to_wtfs64(WTFS_RB_ITABLE),
		.inode_table_count = cpu_to_wtfs64(itables),
		.block_bitmap_first = cpu_to_wtfs64(WTFS_RB_BMAP),
		.block_bitmap_count = cpu_to_wtfs64(bmaps),
		.inode_bitmap_first = cpu_to_wtfs64(WTFS_RB_IMAP),
		.inode_bitmap_count = cpu_to_wtfs64(imaps),
		.inode_count = cpu_to_wtfs64(1),
		.free_block_count = cpu_to_wtfs64(blocks - itables - bmaps -
						  imaps - 3),
	};

	/* Set label */
	if (label != NULL) {
		memcpy(sb.label, label, strlen(label));
	}

	/* Set UUID */
	if (uuid_is_null(uuid)) {
		uuid_generate(uuid);
	}
	uuid_copy(sb.uuid, uuid);

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		return -EIO;
	} else {
		return 0;
	}

	return 0;
}

/*
 * Write inode tables to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 * @itables: number of inode tables
 *
 * return: 0 on success, error code otherwise
 */
static int write_inode_table(int fd, uint64_t itables)
{
	/* Buffer to write */
	struct wtfs_inode_table table;

	/* Inode for root dir */
	struct wtfs_inode inode = {
		.ino = cpu_to_wtfs64(WTFS_ROOT_INO),
		.dentry_count = cpu_to_wtfs64(2),
		.link_count = cpu_to_wtfs32(2),
		.huid = cpu_to_wtfs16(getuid() >> 16),
		.hgid = cpu_to_wtfs16(getgid() >> 16),
		.first_block = cpu_to_wtfs64(WTFS_DB_FIRST),
		.atime = cpu_to_wtfs64(time(NULL)),
		.ctime = cpu_to_wtfs64(time(NULL)),
		.mtime = cpu_to_wtfs64(time(NULL)),
		.mode = cpu_to_wtfs32(S_IFDIR | 0755),
		.uid = cpu_to_wtfs16(getuid()),
		.gid = cpu_to_wtfs16(getgid()),
	};

	/* Block number index array */
	uint64_t * indices = NULL;
	uint64_t i;
	int ret = -EINVAL;

	/* Construct indices */
	indices = (uint64_t *)calloc(itables + 1, sizeof(uint64_t));
	if (indices == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	indices[0] = WTFS_RB_ITABLE;
	indices[itables] = WTFS_RB_ITABLE;
	for (i = 1; i < itables; ++i) {
		indices[i] = WTFS_DB_FIRST + i;
	}

	/* Write 1st inode table */
	memset(&table, 0, sizeof(table));
	table.inodes[0] = inode;
	table.prev = cpu_to_wtfs64(indices[itables - 1]);
	table.next = cpu_to_wtfs64(indices[1]);
	lseek(fd, indices[0] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &table, sizeof(table)) != sizeof(table)) {
		ret = -EIO;
		goto error;
	}

	/* Write remaining inode tables */
	memset(&table, 0, sizeof(table));
	for (i = 1; i < itables; ++i) {
		table.prev = cpu_to_wtfs64(indices[i - 1]);
		table.next = cpu_to_wtfs64(indices[i + 1]);
		lseek(fd, indices[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &table, sizeof(table)) != sizeof(table)) {
			ret = -EIO;
			goto error;
		}
	}

	free(indices);
	return 0;

error:
	if (indices != NULL) {
		free(indices);
	}
	return ret;
}

/*
 * Write block bitmaps to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 * @itables: number of inode tables
 * @bmaps: number of block bitmaps
 * @imaps: number of inode bitmaps
 *
 * return: 0 on success, error code otherwise
 */
static int write_block_bitmap(int fd, uint64_t itables, uint64_t bmaps,
			      uint64_t imaps)
{
	/* Full bytes fill 0xff */
	uint64_t full_bytes = (bmaps + itables + imaps + 3) / 8;

	/* Half byte fills (1 << half_byte_bits) - 1 */
	uint64_t half_byte_bits = (bmaps + itables + imaps + 3) % 8;

	/* Number of full bitmap blocks */
	uint64_t full = full_bytes / WTFS_BITMAP_SIZE;

	/* Number of full bytes in the half-full bitmap block */
	uint64_t half_full = full_bytes % WTFS_BITMAP_SIZE;

	/* Block number index array */
	uint64_t * indices;
	uint64_t i, j;
	struct wtfs_bitmap_block bitmap;
	int ret = -EINVAL;

	/* Construct indices */
	indices = (uint64_t *)calloc(bmaps + 2, sizeof(uint64_t));
	if (indices == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	indices[0] = WTFS_DB_FIRST + itables + bmaps;
	indices[1] = WTFS_RB_BMAP;
	indices[bmaps + 1] = WTFS_RB_BMAP;
	for (i = 2; i <= bmaps; ++i) {
		indices[i] = WTFS_DB_FIRST + itables - 2 + i;
	}

	/* Write full block bitmaps */
	memset(&bitmap, 0xff, sizeof(bitmap));
	for (i = 1; i <= full; ++i) {
		bitmap.prev = cpu_to_wtfs64(indices[i - 1]);
		bitmap.next = cpu_to_wtfs64(indices[i + 1]);
		lseek(fd, indices[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			ret = -EIO;
			goto error;
		}
	}

	/* Write half-full block bitmap */
	memset(&bitmap, 0, sizeof(bitmap));
	for (j = 0; j < half_full; ++j) {
		bitmap.data[j] = 0xff;
	}
	bitmap.data[j] = (1 << half_byte_bits) - 1;
	bitmap.prev = cpu_to_wtfs64(indices[i - 1]);
	bitmap.next = cpu_to_wtfs64(indices[i + 1]);
	lseek(fd, indices[i] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		ret = -EIO;
		goto error;
	}
	++i;

	/* Write empty bitmaps */
	memset(&bitmap, 0, sizeof(bitmap));
	for (; i <= bmaps; ++i) {
		bitmap.prev = cpu_to_wtfs64(indices[i - 1]);
		bitmap.next = cpu_to_wtfs64(indices[i + 1]);
		lseek(fd, indices[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			ret = -EIO;
			goto error;
		}
	}

	free(indices);
	return 0;

error:
	if (indices != NULL) {
		free(indices);
	}
	return ret;
}

/*
 * Write inode bitmaps to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 * @itables: number of inode tables
 * @bmaps: number of block bitmaps
 * @imaps: number of inode bitmaps
 *
 * return: 0 on success, error code otherwise
 */
static int write_inode_bitmap(int fd, uint64_t itables, uint64_t bmaps,
			      uint64_t imaps)
{
	struct wtfs_bitmap_block bitmap;

	/* Block number index array */
	uint64_t * indices = NULL;
	uint64_t i;
	int ret = -EINVAL;

	/* Construct indices */
	indices = (uint64_t *)calloc(imaps + 1, sizeof(uint64_t));
	if (indices == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	indices[0] = WTFS_RB_IMAP;
	indices[imaps] = WTFS_RB_IMAP;
	for (i = 1; i < imaps; ++i) {
		indices[i] = WTFS_DB_FIRST + itables + bmaps - 2 + i;
	}

	/* Write 1st bitmap */
	memset(&bitmap, 0, sizeof(bitmap));
	bitmap.data[0] = 0x03;
	bitmap.prev = cpu_to_wtfs64(indices[imaps - 1]);
	bitmap.next = cpu_to_wtfs64(indices[1]);
	lseek(fd, indices[0] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		ret = -EIO;
		goto error;
	}

	/* Write remaining bitmaps */
	memset(&bitmap, 0, sizeof(bitmap));
	for (i = 1; i < imaps; ++i) {
		bitmap.prev = cpu_to_wtfs64(indices[i - 1]);
		bitmap.next = cpu_to_wtfs64(indices[i + 1]);
		lseek(fd, indices[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			ret = -EIO;
			goto error;
		}
	}

	free(indices);
	return 0;

error:
	if (indices != NULL) {
		free(indices);
	}
	return ret;
}

/*
 * Write the first data block (for root directory) to a device or image.
 *
 * @fd: an open file descriptor of the device or image
 *
 * return: 0 on success, error code otherwise
 */
static int write_root_dir(int fd)
{
	struct wtfs_dir_block root_blk = {
		.dentries = {
			[0] = {
				.ino = WTFS_ROOT_INO,
				.filename = ".",
			},
			[1] = {
				.ino = WTFS_ROOT_INO,
				.filename = "..",
			},
		},
		.prev = cpu_to_wtfs64(WTFS_DB_FIRST),
		.next = cpu_to_wtfs64(WTFS_DB_FIRST),
	};

	lseek(fd, WTFS_DB_FIRST * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &root_blk, sizeof(root_blk)) != sizeof(root_blk)) {
		return -EIO;
	} else {
		return 0;
	}
}

/*
 * Perform deep format to the device or image.
 *
 * @fd: an open file descriptor of the device or image
 * @blocks: number of blocks
 * @itables: number of inode tables
 * @bmaps: number of block bitmaps
 * @imaps: number of inode bitmaps
 * @quiet: whether or not to print some info to stdout
 */
static void do_deep_format(int fd, uint64_t blocks, uint64_t itables,
			   uint64_t bmaps, uint64_t imaps, int quiet)
{
	uint64_t start = WTFS_DB_FIRST + itables + bmaps +
		imaps - 2;
	uint64_t i, percent, prev = 0;
	struct wtfs_data_block blk;

	memset(&blk, 0, sizeof(blk));
	if (!quiet) {
		printf("Total %lu blocks to format.\n", (blocks -= start));
		printf("\rFormat complete 0%%...");
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
				printf("\rFormat complete %lu%%...", percent);
				fflush(stdout);
				prev = percent;
			}
		}
	}
	if (!quiet) {
		printf("\nDeep format completed.\n");
	}
}
