/*
 * mkfs.wtfs.c - mkfs for wtfs.
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

#include "wtfs.h"

#define BUF_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static int check_mounted_fs(const char * filename);
static int write_boot_block(int fd);
static int write_super_block(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps,
	const char * label, uuid_t uuid);
static int write_inode_table(int fd, uint64_t inode_tables);
static int write_block_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps);
static int write_inode_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps);
static int write_root_dir(int fd);
static void do_deep_format(int fd, uint64_t blocks, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps, int quiet);

int main(int argc, char * const * argv)
{
	/* used in argument parse */
	int opt;
	struct option long_options[] = {
		{ "fast", no_argument, NULL, 'f' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "force", no_argument, NULL, 'F' },
		{ "imaps", required_argument, NULL, 'i' },
		{ "label", required_argument, NULL, 'L' },
		{ "uuid", required_argument, NULL, 'U' },
		{ "version", no_argument, NULL, 'V' },
		{ "help", no_argument, NULL, 'h' },
		{ 0, 0, 0, 0 },
	};

	/* flags */
	int quick = 0, quiet = 0, force = 0;

	/* file descriptor */
	int fd = -1;

	/* bytes and blocks of the device */
	uint64_t bytes, blocks;

	/* inode tables */
	uint64_t inode_tables;

	/* block bitmaps */
	uint64_t blk_bitmaps;

	/* inode bitmaps (default 1) */
	int64_t inode_bitmaps = 1;

	/* minimum data blocks (not exact) */
	uint64_t min_data_blks;

	/* filesystem label */
	char * label = NULL;

	/* filesystem UUID */
	uuid_t uuid = { 0 };

	char err_msg[BUF_SIZE];
	const char * part = NULL;

	struct stat stat;

	const char * usage = "Usage: mkfs.wtfs [OPTIONS] <DEVICE>\n"
			     "Options:\n"
			     "  -f, --fast            quick format\n"
			     "  -q, --quiet           quiet mode\n"
			     "  -F, --force           force execution\n"
			     "  -i, --imaps=IMAPS     set inode bitmap count\n"
			     "  -L, --label=LABEL     set filesystem label\n"
			     "  -U, --uuid=UUID       set filesystem UUID\n"
			     "  -V, --version         show version and exit\n"
			     "  -h, --help            show this message and exit\n"
			     "\n";

	/* parse arguments */
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

		case 'i':
			inode_bitmaps = strtol(optarg, NULL, 10);
			/*
			 * here we just do the left side of the range of
			 * inode bitmap count, and the right side will be done
			 * after knowing the device size
			 *
			 * note that underflow is included in this judgement
			 */
			if (inode_bitmaps <= 0) {
				fprintf(stderr, "%s: too few inode bitmaps\n",
					argv[0]);
				goto error;
			}
			break;

		case 'L':
			label = optarg;
			if (strnlen(label, WTFS_LABEL_MAX) == WTFS_LABEL_MAX) {
				fprintf(stderr, "%s: label too long\n",
					argv[0]);
				goto error;
			}
			break;

		case 'U':
			if (uuid_parse(optarg, uuid) < 0) {
				fprintf(stderr, "%s: invalid UUID '%s'\n",
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
		fprintf(stderr, "%s: only block device and regular file "
			"supported\n", argv[0]);
		goto error;
	}

	/* do calculation */
	blocks = bytes / WTFS_BLOCK_SIZE;
	inode_tables = inode_bitmaps * WTFS_BITMAP_SIZE * 8 /
		WTFS_INODE_COUNT_PER_TABLE + 1;
	blk_bitmaps = blocks / (WTFS_BITMAP_SIZE * 8);
	min_data_blks = inode_bitmaps * WTFS_BLOCK_SIZE * 8;
	/*
	 * if the number of inode bitmap is more than 1, it indicates that
	 * the caller assumed the volume is big enough, so give it an extra
	 * restriction of minimum data blocks
	 */
	if (inode_bitmaps > 1) {
		if (blocks < inode_tables + blk_bitmaps + inode_bitmaps + 3 +
			min_data_blks) {
			fprintf(stderr, "%s: too many inode bitmaps\n",
				argv[0]);
			goto error;
		}
	} else {
		if (blocks < inode_tables + blk_bitmaps + inode_bitmaps + 3) {
			fprintf(stderr, "%s: volume too small\n", argv[0]);
			goto error;
		}
	}

	if (blocks % (WTFS_BITMAP_SIZE * 8) != 0) {
		++blk_bitmaps;
	}

	/*
	 * check if the filesystem is already mounted when option 'force' is
	 * not specified
	 */
	if (!force) {
		/* reuse opt */
		opt = check_mounted_fs(argv[optind]);
		if (opt < 0) {
			fprintf(stderr, "%s: an error occurred when checking "
				"mounted filesystems\n", argv[0]);
			goto error;
		} else if (opt == 1) {
			fprintf(stderr, "%s: '%s' is already mounted\n",
				argv[0], argv[optind]);
			goto error;
		}
	}

	/* do format */
	if (write_boot_block(fd) < 0) {
		part = "bootloader block";
		goto out;
	}
	if (write_super_block(fd, blocks, inode_tables, blk_bitmaps,
			inode_bitmaps, label, uuid) < 0) {
		part = "super block";
		goto out;
	}
	if (write_inode_table(fd, inode_tables) < 0) {
		part = "inode table";
		goto out;
	}
	if (write_block_bitmap(fd, inode_tables, blk_bitmaps,
		inode_bitmaps) < 0) {
		part = "block bitmap";
		goto out;
	}
	if (write_inode_bitmap(fd, inode_tables, blk_bitmaps,
			inode_bitmaps) < 0) {
		part = "inode bitmap";
		goto out;
	}
	if (write_root_dir(fd) < 0) {
		part = "root directory";
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
	fprintf(stderr, "%s: write %s failed\n", argv[0], part);

error:
	if (fd >= 0) {
		close(fd);
	}
	return 1;
}

/*
 * check if the given file (device or filesystem image) is mounted
 * return 0 or 1 on success, error code otherwise
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

	/* first get the canonical path of the file */
	if (realpath(filename, buf) == NULL) {
		ret = -EINVAL;
		goto error;
	}

	/* initialize libmount */
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

	/* do iterate mounted filesystems */
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
	uint64_t blk_bitmaps, uint64_t inode_bitmaps,
	const char * label, uuid_t uuid)
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

	/* set label */
	if (label != NULL) {
		memcpy(sb.label, label, strlen(label));
	}

	/* set UUID */
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
 * pre-build the whole inode table for the device
 */
static int write_inode_table(int fd, uint64_t inode_tables)
{
	/* buffer to write */
	struct wtfs_inode_table table;

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
	memset(&table, 0, sizeof(table));
	table.inodes[0] = inode;
	table.next = wtfs64_to_cpu(index[1]);
	lseek(fd, index[0] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &table, sizeof(table)) != sizeof(table)) {
		ret = -EIO;
		goto error;
	}

	/* write remaining inode tables */
	memset(&table, 0, sizeof(table));
	for (i = 1; i < inode_tables; ++i) {
		table.next = wtfs64_to_cpu(index[i + 1]);
		lseek(fd, index[i] * WTFS_BLOCK_SIZE, SEEK_SET);
		if (write(fd, &table, sizeof(table)) != sizeof(table)) {
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
 * since the device size is fixed, we pre-build the whole block bitmap for
 * the device
 */
static int write_block_bitmap(int fd, uint64_t inode_tables,
	uint64_t blk_bitmaps, uint64_t inode_bitmaps)
{
	/* full bytes fill 0xff */
	uint64_t full_bytes = (blk_bitmaps + inode_tables + inode_bitmaps +
		3) / 8;

	/* half byte fills (1 << half_byte) - 1 */
	uint64_t half_byte = (blk_bitmaps + inode_tables + inode_bitmaps +
		3) % 8;

	/* last full bitmap */
	uint64_t full = full_bytes / WTFS_BITMAP_SIZE;

	/* full bytes in half-full bitmap */
	uint64_t half_full = full_bytes % WTFS_BITMAP_SIZE;

	/* block number index array */
	uint64_t * index;
	uint64_t i, j;
	struct wtfs_bitmap_block bitmap;
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
	struct wtfs_bitmap_block bitmap = {
		.data = { 0x03 },
	};

	/* block number index array */
	uint64_t * index = NULL;
	uint64_t i;
	int ret = -EINVAL;

	/* construct index */
	index = (uint64_t *)calloc(inode_bitmaps + 1, sizeof(uint64_t));
	if (index == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	index[0] = WTFS_RB_INODE_BITMAP;
	for (i = 1; i < inode_bitmaps; ++i) {
		index[i] = WTFS_RB_INODE_BITMAP + inode_tables + blk_bitmaps -
			2 + i;
	}

	/* write 1st bitmap */
	bitmap.next = cpu_to_wtfs64(index[1]);
	lseek(fd, index[0] * WTFS_BLOCK_SIZE, SEEK_SET);
	if (write(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
		ret = -EIO;
		goto error;
	}

	/* write remaining bitmaps */
	memset(&bitmap, 0, sizeof(bitmap));
	for (i = 1; i < inode_bitmaps; ++i) {
		bitmap.next = wtfs64_to_cpu(index[i + 1]);
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

static int write_root_dir(int fd)
{
	struct wtfs_dir_block root_blk = {
		.entries = {
			[0] = {
				.inode_no = WTFS_ROOT_INO,
				.filename = ".",
			},
			[1] = {
				.inode_no = WTFS_ROOT_INO,
				.filename = "..",
			},
		},
	};

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

#ifdef __cplusplus
}
#endif /* __cplusplus */
