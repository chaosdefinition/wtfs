/*
 * wtfs.h - header file for wtfs.
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

#pragma once

#ifndef WTFS_WTFS_H_
#define WTFS_WTFS_H_

#include <linux/types.h>
#include <linux/fs.h>

#include <wtfs/utils.h>

/*
 * Version of wtfs.
 * We may evolve several versions of it.
 */
#define WTFS_VERSION 0x0100
#define WTFS_VERSION_STR "1.0.0"

/* Version control */
#define WTFS_VERSION_MAJOR(v) ((v) >> 8)
#define WTFS_VERSION_MINOR(v) ((v) & 0xff)
#define WTFS_VERSION_PATCH(v) ((v) ^ (v)) /* Always zero */
#define WTFS_GET_VERSION(major, minor, patch) (((major) << 8) | (minor))

/*
 * Version 1.0.0 physical disk layout:
 *   +------------------+
 * 0 | Boot loader      |
 *   +------------------+
 * 1 | Super block      |
 *   +------------------+   +------------------+
 * 2 | 1st inode table  |<->| 2nd inode table  |<->...
 *   +------------------+   +------------------+
 * 3 | 1st block bitmap |<->| 2nd block bitmap |<->...
 *   +------------------+   +------------------+
 * 4 | 1st inode bitmap |<->| 2nd inode bitmap |<->...
 *   +------------------+   +------------------+
 * 5 | Data blocks...   |
 *   +------------------+
 */

/*
 * Magic number of wtfs.
 * I'll never tell you where this value is from...
 */
#define WTFS_MAGIC 0x0c3e

/* Size of each block in wtfs */
#define WTFS_BLOCK_SIZE 4096

/* Size of each inode in wtfs */
#define WTFS_INODE_SIZE 64

/* Max inode count per table in wtfs */
#define WTFS_INODES_PER_TABLE 63

/* Max length of filename in wtfs */
#define WTFS_FILENAME_MAX 56

/* Max dentry count per block in wtfs */
#define WTFS_DENTRIES_PER_BLOCK 63

/* Max length of symlink content in wtfs */
#define WTFS_SYMLINK_MAX 4094

/* Max length of filesystem label in wtfs */
#define WTFS_LABEL_MAX 32

/* Size of data in a linked block */
#define WTFS_LNKBLK_SIZE ((WTFS_BLOCK_SIZE) - 2 * sizeof(wtfs64_t))

/* Size of bitmap data in bytes */
#define WTFS_BITMAP_SIZE WTFS_LNKBLK_SIZE

/* Max index count per index block in wtfs */
#define WTFS_INDICES_PER_BLOCK 510

/* Reserved block indices */
#define WTFS_RB_BOOT	0 /* Boot loader block */
#define WTFS_RB_SUPER	1 /* Super block */
#define WTFS_RB_ITABLE	2 /* First inode table */
#define WTFS_RB_BMAP	3 /* First block bitmap */
#define WTFS_RB_IMAP	4 /* First inode bitmap */

/* First data block index (for root directory) */
#define WTFS_DB_FIRST	5

/* Inode number of root directory */
#define WTFS_ROOT_INO 1

/* Structure for super block in disk */
struct wtfs_super_block {
	wtfs64_t version;		/* 8 bytes */
	wtfs64_t magic;			/* 8 bytes */
	wtfs64_t block_size;		/* 8 bytes */
	wtfs64_t block_count;		/* 8 bytes */

	wtfs64_t inode_table_first;	/* 8 bytes */
	wtfs64_t inode_table_count;	/* 8 bytes */
	wtfs64_t block_bitmap_first;	/* 8 bytes */
	wtfs64_t block_bitmap_count;	/* 8 bytes */
	wtfs64_t inode_bitmap_first;	/* 8 bytes */
	wtfs64_t inode_bitmap_count;	/* 8 bytes */

	wtfs64_t inode_count;		/* 8 bytes */
	wtfs64_t free_block_count;	/* 8 bytes */

	char label[WTFS_LABEL_MAX];	/* 32 bytes */
	unsigned char uuid[16];		/* 16 bytes */

	wtfs8_t padding[3952];		/* 3952 bytes */
} __attribute__((packed));

/* Structure for inode in disk */
struct wtfs_inode {
	wtfs64_t ino;		/* 8 bytes */

	union {			/* 8 bytes */
		wtfs64_t file_size;
		wtfs64_t dentry_count;
	};
	wtfs32_t link_count;	/* 4 bytes */
	wtfs16_t huid;		/* 2 bytes */
	wtfs16_t hgid;		/* 2 bytes */
	wtfs64_t first_block;	/* 8 bytes */
	wtfs64_t atime;		/* 8 bytes */
	wtfs64_t ctime;		/* 8 bytes */
	wtfs64_t mtime;		/* 8 bytes */
	wtfs32_t mode;		/* 4 bytes */
	wtfs16_t uid;		/* 2 bytes */
	wtfs16_t gid;		/* 2 bytes */
} __attribute__((packed));

/* Model of linked block */
struct wtfs_linked_block {
	wtfs8_t data[WTFS_LNKBLK_SIZE];	/* 4080 bytes */
	wtfs64_t prev;			/* 8 bytes */
	wtfs64_t next;			/* 8 bytes */
} __attribute__((packed));

/* Structure for inode table */
struct wtfs_inode_table {
	struct wtfs_inode inodes	/* 4032 bytes */
	[
		WTFS_INODES_PER_TABLE
	];
	wtfs8_t padding[48];		/* 48 bytes */
	wtfs64_t prev;			/* 8 bytes */
	wtfs64_t next;			/* 8 bytes */
} __attribute__((packed));

/* Structure for bitmap block */
struct wtfs_bitmap_block {
	wtfs8_t data[WTFS_BITMAP_SIZE];	/* 4080 bytes */
	wtfs64_t prev;			/* 8 bytes */
	wtfs64_t next;			/* 8 bytes */
} __attribute__((packed));

/* Structure for each directory entry */
struct wtfs_dentry {
	wtfs64_t ino;				/* 8 bytes */
	char filename[WTFS_FILENAME_MAX];	/* 56 bytes */
} __attribute__((packed));

/* Structure for directory data block */
struct wtfs_dir_block {
	struct wtfs_dentry dentries	/* 4032 bytes */
	[
		WTFS_DENTRIES_PER_BLOCK
	];
	wtfs8_t padding[48];		/* 48 bytes */
	wtfs64_t prev;			/* 8 bytes */
	wtfs64_t next;			/* 8 bytes */
} __attribute__((packed));

/* Structure for file data index block */
struct wtfs_index_block {
	wtfs64_t indices	/* 4080 bytes */
	[
		WTFS_INDICES_PER_BLOCK
	];
	wtfs64_t prev;		/* 8 bytes */
	wtfs64_t next;		/* 8 bytes */
} __attribute__((packed));

/* Structure for data block */
struct wtfs_data_block {
	wtfs8_t data[WTFS_BLOCK_SIZE];	/* 4096 bytes */
} __attribute__((packed));

/* Structure for symlink block */
struct wtfs_symlink_block {
	wtfs16_t length;		/* 2 bytes */
	char path[WTFS_SYMLINK_MAX];	/* 4094 bytes */
} __attribute__((packed));

/* Following only available for module itself */
#ifdef __KERNEL__

/* Structure for super block in memory */
struct wtfs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	uint64_t block_count;

	uint64_t inode_table_first;
	uint64_t inode_table_count;
	uint64_t block_bitmap_first;
	uint64_t block_bitmap_count;
	uint64_t inode_bitmap_first;
	uint64_t inode_bitmap_count;

	uint64_t inode_count;
	uint64_t free_block_count;
};

/* Structure for inode in memory */
struct wtfs_inode_info {
	uint64_t dentry_count;
	uint64_t first_block;
	struct inode vfs_inode;
};

/* Get sb_info from the VFS super block */
static inline struct wtfs_sb_info * WTFS_SB_INFO(struct super_block * vsb)
{
	return (struct wtfs_sb_info *)vsb->s_fs_info;
}

/* Get inode_info from the VFS inode */
static inline struct wtfs_inode_info * WTFS_INODE_INFO(struct inode * vi)
{
	return container_of(vi, struct wtfs_inode_info, vfs_inode);
}

/* Operations */
extern const struct super_operations wtfs_super_ops;
extern const struct inode_operations wtfs_file_inops;
extern const struct inode_operations wtfs_dir_inops;
extern const struct inode_operations wtfs_symlink_inops;
extern const struct file_operations wtfs_file_ops;
extern const struct file_operations wtfs_dir_ops;

#endif /* __KERNEL__ */

#endif /* WTFS_H_ */
