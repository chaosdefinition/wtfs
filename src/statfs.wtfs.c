/*
 * statfs.wtfs.c - statfs for wtfs.
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
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <endian.h>
#include <errno.h>
#include <uuid/uuid.h>

#include <wtfs/wtfs.h>

#define BUF_SIZE 4096

static int check_wtfs_instance(int fd);
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
	char err_msg[BUF_SIZE], buf[BUF_SIZE];
	const char * filename = NULL, * part = NULL;
	struct stat stat;
	const char * usage = "Usage: statfs.wtfs <FILE>\n"
			     "FILE can be a block device or image containing "
			     "a wtfs instance, or any file within a wtfs "
			     "instance\n";

	if (argc != 2) {
		printf("%s", usage);
		goto error;
	}
	filename = argv[1];

	/* Open and stat input file */
	if ((fd = open(filename, O_RDONLY)) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: cannot open '%s'",
			 argv[0], filename);
		perror(err_msg);
		goto error;
	}
	if (fstat(fd, &stat) < 0) {
		snprintf(err_msg, BUF_SIZE, "%s: unable to stat '%s'",
			 argv[0], filename);
		perror(err_msg);
		goto error;
	}

	switch (stat.st_mode & S_IFMT) {
	/*
	 * Regular file.
	 * It may be a file within a wtfs instance or an image containing a
	 * wtfs instance.
	 */
	case S_IFREG:
		ret = check_wtfs_instance(fd);
		if (ret < 0) {
			snprintf(err_msg, BUF_SIZE, "%s: Failed to read '%s'",
				 argv[0], filename);
			perror(err_msg);
			goto error;
		} else if (ret == 1) {
			break;
		}
		/* Fall-through if ret == 0 */

	/* Directory within a wtfs instance */
	case S_IFDIR:
		snprintf(buf, BUF_SIZE, "/dev/block/%u:%u",
			 major(stat.st_dev), minor(stat.st_dev));
		close(fd);
		filename = buf;
		if ((fd = open(filename, O_RDONLY)) < 0) {
			snprintf(err_msg, BUF_SIZE, "%s: Cannot open '%s'",
				 argv[0], filename);
			perror(err_msg);
			goto error;
		}
		/* Fall-through again */

	/* Block device containing a wtfs instance */
	case S_IFBLK:
		ret = check_wtfs_instance(fd);
		if (ret < 0) {
			snprintf(err_msg, BUF_SIZE, "%s: Failed to read '%s'",
				 argv[0], filename);
			perror(err_msg);
			goto error;
		} else if (ret == 0) {
			fprintf(stderr, "%s: No wtfs instance is found\n",
				argv[0]);
			goto error;
		}
		break;

	default:
		fprintf(stderr, "%s: No wtfs instance is found\n", argv[0]);
		goto error;
	}

	/* Read blocks */
	if ((ret = read_boot_block(fd)) < 0) {
		part = "bootloader block";
		goto out;
	}
	if ((ret = read_super_block(fd)) < 0) {
		if (ret == -EPERM) {
			fprintf(stderr, "%s: No wtfs instance found\n",
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
	fprintf(stderr, "%s: Failed to read %s\n", argv[0], part);

error:
	if (fd >= 0) {
		close(fd);
	}
	return 1;
}

/*
 * Check if the file is a valid wtfs instance.
 *
 * @fd: an open file descriptor of the file
 *
 * return: 0 or 1 on success, error code otherwise
 */
static int check_wtfs_instance(int fd)
{
	struct wtfs_super_block sb;
	ssize_t nread;

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	nread = read(fd, &sb, sizeof(sb));
	if (nread < 0) {
		return -EIO;
	}
	if (nread < sizeof(sb)) {
		return 0;
	}
	if (wtfs64_to_cpu(sb.magic) != WTFS_MAGIC) {
		return 0;
	}
	if (!is_power_of_2(wtfs64_to_cpu(sb.block_size))) {
		return 0;
	}

	return 1;
}

/*
 * Read the boot loader block of a wtfs instance.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0
 */
static int read_boot_block(int fd)
{
	/* Do nothing */
	return 0;
}

/*
 * Read the super block of a wtfs instance and print some info to stdout.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0 on success, error code otherwise
 */
static int read_super_block(int fd)
{
	struct wtfs_super_block sb;
	uint64_t version;
	char uuid_buffer[36 + 1];

	lseek(fd, WTFS_RB_SUPER * WTFS_BLOCK_SIZE, SEEK_SET);
	if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		return -EIO;
	}

	version = wtfs64_to_cpu(sb.version);
	printf("wtfs on this device\n");
	printf("%-24s%lu.%lu.%lu\n", "Version:",
	       WTFS_VERSION_MAJOR(version),
	       WTFS_VERSION_MINOR(version),
	       WTFS_VERSION_PATCH(version));
	printf("%-24s0x%llx\n", "Magic number:",
	       wtfs64_to_cpu(sb.magic));
	printf("%-24s%llu\n", "Block size:",
	       wtfs64_to_cpu(sb.block_size));
	printf("%-24s%llu\n", "Total blocks:",
	       wtfs64_to_cpu(sb.block_count));
	printf("%-24s%llu\n", "First inode table:",
	       wtfs64_to_cpu(sb.inode_table_first));
	printf("%-24s%llu\n", "Total inode tables:",
	       wtfs64_to_cpu(sb.inode_table_count));
	printf("%-24s%llu\n", "First block bitmap:",
	       wtfs64_to_cpu(sb.block_bitmap_first));
	printf("%-24s%llu\n", "Total block bitmaps:",
	       wtfs64_to_cpu(sb.block_bitmap_count));
	printf("%-24s%llu\n", "First inode bitmap:",
	       wtfs64_to_cpu(sb.inode_bitmap_first));
	printf("%-24s%llu\n", "Total inode bitmaps:",
	       wtfs64_to_cpu(sb.inode_bitmap_count));
	printf("%-24s%llu\n", "Total inodes:",
	       wtfs64_to_cpu(sb.inode_count));
	printf("%-24s%llu\n", "Free blocks:",
	       wtfs64_to_cpu(sb.free_block_count));
	/* Label and UUID are supported since v0.3.0 */
	if (WTFS_VERSION_MINOR(version) >= 3 || WTFS_VERSION_MAJOR(version) > 0) {
		if (strnlen(sb.label, WTFS_LABEL_MAX) != 0) {
			printf("%-24s%s\n", "Label:", sb.label);
		}
		if (!uuid_is_null(sb.uuid)) {
			uuid_unparse(sb.uuid, uuid_buffer);
			uuid_buffer[36] = '\0';
			printf("%-24s%s\n", "UUID:", uuid_buffer);
		}
	}
	printf("\n");

	return 0;
}

/*
 * Read the inode tables of a wtfs instance and print some info to stdout.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0 on success, error code otherwise
 */
static int read_inode_table(int fd)
{
	struct wtfs_inode_table itable;
	int i;
	uint64_t next = WTFS_RB_ITABLE;

	do {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &itable, sizeof(itable)) != sizeof(itable)) {
			return -EIO;
		}

		printf("In inode table %lu:\n", next);
		printf("prev: %llu\n", wtfs64_to_cpu(itable.prev));
		printf("next: %llu\n", wtfs64_to_cpu(itable.next));
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			if (wtfs64_to_cpu(itable.inodes[i].ino) != 0) {
				printf("ino: %llu\n",
				       wtfs64_to_cpu(itable.inodes[i].ino));
			}
		}
		printf("\n");

		next = wtfs64_to_cpu(itable.next);
	} while (next != WTFS_RB_ITABLE);

	return 0;
}

/*
 * Read the block bitmaps of a wtfs instance and print some info to stdout.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0 on success, error code otherwise
 */
static int read_block_bitmap(int fd)
{
	struct wtfs_bitmap_block bitmap;
	uint64_t next = WTFS_RB_BMAP;

	do {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			return -EIO;
		}

		printf("In block bitmap %lu:\n", next);
		printf("prev: %llu\n", wtfs64_to_cpu(bitmap.prev));
		printf("next: %llu\n", wtfs64_to_cpu(bitmap.next));
		printf("\n");

		next = wtfs64_to_cpu(bitmap.next);
	} while (next != WTFS_RB_BMAP);

	return 0;
}

/*
 * Read the inode bitmaps of a wtfs instance and print some info to stdout.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0 on success, error code otherwise
 */
static int read_inode_bitmap(int fd)
{
	struct wtfs_bitmap_block bitmap;
	uint64_t next = WTFS_RB_IMAP;

	do {
		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
			return -EIO;
		}

		printf("In inode bitmap %lu:\n", next);
		printf("prev: %llu\n", wtfs64_to_cpu(bitmap.prev));
		printf("next: %llu\n", wtfs64_to_cpu(bitmap.next));
		printf("\n");

		next = wtfs64_to_cpu(bitmap.next);
	} while (next != WTFS_RB_IMAP);

	return 0;
}

/*
 * Read the root directory of a wtfs instance and print some info to stdout.
 *
 * @fd: an open file descriptor of the instance
 *
 * return: 0 on success, error code otherwise
 */
static int read_root_dir(int fd)
{
	struct wtfs_dir_block root_blk;
	int i;
	uint64_t next = WTFS_DB_FIRST, ino;
	const char * filename = NULL;

	do {
		if (next == WTFS_DB_FIRST) {
			printf("Root directory\n");
		}

		lseek(fd, next * WTFS_BLOCK_SIZE, SEEK_SET);
		if (read(fd, &root_blk, sizeof(root_blk)) != sizeof(root_blk)) {
			return -EIO;
		}

		for (i = 0; i < WTFS_DENTRY_COUNT_PER_BLOCK; ++i) {
			ino = wtfs64_to_cpu(root_blk.dentries[i].ino);
			filename = root_blk.dentries[i].filename;
			if (ino != 0) {
				printf("%lu  %s\n", ino, filename);
			}
		}
		next = wtfs64_to_cpu(root_blk.next);
	} while (next != WTFS_DB_FIRST);
	printf("\n");

	return 0;
}
