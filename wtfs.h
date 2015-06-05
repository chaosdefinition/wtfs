/*
 * wtfs.h - header file for wtfs.
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

#pragma once

#ifndef WTFS_H_
#define WTFS_H_

#include <linux/types.h>
#include <linux/fs.h>

#include "macro_utils.h"

/*
 * version of wtfs
 * we may evolve several versions of it
 */
#define WTFS_VERSION 0x0001 /* 0.1.0 */

/* version control */
#define WTFS_VERSION_MAJOR(v) ((v) >> 8)
#define WTFS_VERSION_MINOR(v) ((v) & 0xff)
#define WTFS_VERSION_PATCH(v) 0
#define WTFS_GET_VERSION(major, minor, patch) (((major) << 8) | (minor))

/*
 * version 0.1.0 physical disk layout:
 *   +------------------+
 * 0 | boot loader      |
 *   +------------------+
 * 1 | super block      |
 *   +------------------+  +------------------+
 * 2 | 1st inode table  |->| 2nd inode table  |->...
 *   +------------------+  +------------------+
 * 3 | 1st block bitmap |->| 2nd block bitmap |->...
 *   +------------------+  +------------------+
 * 4 | 1st inode bitmap |
 *   +------------------+
 * 5 | data blocks...   |
 *   +------------------+
 *
 * -- block information --
 * size of each block:              4096 bytes
 *
 * -- inode information --
 * size of each inode:              64 bytes
 * max inodes per table:            63
 *
 * -- bitmap information --
 * max blocks/inodes per bitmap:    4088 * 8
 *
 * -- data block information --
 * size of each dentry:             64 bytes
 * max dentries per block:          63
 */

/*
 * magic number of wtfs
 * I'll never tell you where this value is from...
 */
#define WTFS_MAGIC 0x0c3e

/* size of each block in wtfs */
#define WTFS_BLOCK_SIZE 4096

/* size of each inode in wtfs */
#define WTFS_INODE_SIZE 64

/* max inode count per table in wtfs */
#define WTFS_INODE_COUNT_PER_TABLE 63

/* max length of filename in wtfs */
#define WTFS_FILENAME_MAX 56

/* max dentry count per block in wtfs */
#define WTFS_DENTRY_COUNT_PER_BLOCK 63

/*
 * size of real data that each block can contain (except boot and super block)
 * we need the last 8 bytes as a pointer to another block
 */
#define WTFS_DATA_SIZE (WTFS_BLOCK_SIZE - sizeof(wtfs64_t))

/* reserved block indices */
#define WTFS_RB_BOOT            0   /* boot loader block */
#define WTFS_RB_SUPER           1   /* super block */
#define WTFS_RB_INODE_TABLE     2   /* first inode table */
#define WTFS_RB_BLOCK_BITMAP    3   /* first block bitmap */
#define WTFS_RB_INODE_BITMAP    4   /* first inode bitmap */

/* first data block index (for root directory) */
#define WTFS_DB_FIRST           5

/* inode number of root directory */
#define WTFS_ROOT_INO 1

/* max inode number */
#define WTFS_INODE_MAX (WTFS_DATA_SIZE * 8)

/* DEBUG macro for wtfs */
#ifdef DEBUG
# define WTFS_DEBUG 1
#endif /* DEBUG */

/* macros for trace & debug */
#ifdef WTFS_DEBUG
# define wtfs_debug(fmt, ...)\
	printk(KERN_DEBUG "[wtfs] at %s:%d %s: " fmt,\
	__FILE__, __LINE__, __func__, ##__VA_ARGS__)
# define wtfs_error(fmt, ...)\
	printk(KERN_ERR "[wtfs]: " fmt, ##__VA_ARGS__)
# define wtfs_info(fmt, ...)\
	printk(KERN_INFO "[wtfs]: " fmt, ##__VA_ARGS__)
#else
# define wtfs_debug(fmt, ...)\
	no_printk(fmt, ##__VA_ARGS__)
# define wtfs_error(fmt, ...)\
	no_printk(fmt, ##__VA_ARGS__)
# define wtfs_info(fmt, ...)\
	no_printk(fmt, ##__VA_ARGS__)
#endif /* WTFS_DEBUG */

/* structure for super block in disk */
struct wtfs_super_block
{
	wtfs64_t version;                   /* 8 bytes */
	wtfs64_t magic;                     /* 8 bytes */
	wtfs64_t block_size;                /* 8 bytes */
	wtfs64_t block_count;               /* 8 bytes */

	wtfs64_t inode_table_first;         /* 8 bytes */
	wtfs64_t inode_table_count;         /* 8 bytes */
	wtfs64_t block_bitmap_first;        /* 8 bytes */
	wtfs64_t block_bitmap_count;        /* 8 bytes */
	wtfs64_t inode_bitmap_first;        /* 8 bytes */
	wtfs64_t inode_bitmap_count;        /* 8 bytes */

	wtfs64_t inode_count;               /* 8 bytes */
	wtfs64_t free_block_count;          /* 8 bytes */

	wtfs8_t padding                     /* 4000 bytes */
	[
		WTFS_BLOCK_SIZE - sizeof(wtfs64_t) * 12
	];
};

/* structure for inode in disk */
struct wtfs_inode
{
	wtfs64_t inode_no;                  /* 8 bytes */

	union {                             /* 8 bytes */
		wtfs64_t file_size;
		wtfs64_t dir_entry_count;
	};
	wtfs64_t block_count;               /* 8 bytes */
	wtfs64_t first_block;               /* 8 bytes */
	wtfs64_t atime;                     /* 8 bytes */
	wtfs64_t ctime;                     /* 8 bytes */
	wtfs64_t mtime;                     /* 8 bytes */
	wtfs32_t mode;                      /* 4 bytes */
	wtfs16_t uid;                       /* 2 bytes */
	wtfs16_t gid;                       /* 2 bytes */
};

/* structure for inode table */
struct wtfs_inode_table
{
	struct wtfs_inode inodes            /* 4032 bytes */
	[
		WTFS_DATA_SIZE / sizeof(struct wtfs_inode)
	];
	wtfs8_t padding                     /* 56 bytes */
	[
		WTFS_DATA_SIZE % sizeof(struct wtfs_inode)
	];
	wtfs64_t next;                      /* 8 bytes */
};

/* structure for directory data */
struct wtfs_dentry
{
	wtfs64_t inode_no;                  /* 8 bytes */
	char filename[WTFS_FILENAME_MAX];   /* 56 bytes */
};

/* structure for data block and bitmap */
struct wtfs_data_block
{
	union {                             /* 4088 bytes */
		wtfs8_t data[WTFS_DATA_SIZE];
		struct {
			struct wtfs_dentry entries
			[
				WTFS_DATA_SIZE / sizeof(struct wtfs_dentry)
			];
			wtfs8_t padding
			[
				WTFS_DATA_SIZE % sizeof(struct wtfs_dentry)
			];
		};
	};
	wtfs64_t next;                      /* 8 bytes */
};

/* following only available for module itself */
#ifdef __KERNEL__

/* structure for super block in memory */
struct wtfs_sb_info
{
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

/* structure for inode in memory */
struct wtfs_inode_info
{
	uint64_t dir_entry_count;
	uint64_t first_block;
	struct inode vfs_inode;
};

/* get sb_info from the VFS super block */
static inline struct wtfs_sb_info * WTFS_SB_INFO(struct super_block * vsb)
{
	return (struct wtfs_sb_info *)vsb->s_fs_info;
}

/* get inode_info from the VFS inode */
static inline struct wtfs_inode_info * WTFS_INODE_INFO(struct inode * vi)
{
	return container_of(vi, struct wtfs_inode_info, vfs_inode);
}

/* operations */
extern const struct super_operations wtfs_super_ops;
extern const struct inode_operations wtfs_file_inops;
extern const struct inode_operations wtfs_dir_inops;
extern const struct file_operations wtfs_file_ops;
extern const struct file_operations wtfs_dir_ops;

/* helper functions */
extern struct inode * wtfs_iget(struct super_block * vsb, uint64_t inode_no);
extern struct wtfs_inode * wtfs_get_inode(struct super_block * vsb,
	uint64_t inode_no, struct buffer_head ** pbh);
extern int is_ino_valid(struct super_block * vsb, uint64_t inode_no);
extern struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
	uint64_t start, uint64_t count);
extern int wtfs_set_bitmap_bit(struct super_block * vsb, uint64_t entry,
	uint64_t count, uint64_t offset);
extern int wtfs_clear_bitmap_bit(struct super_block * vsb, uint64_t entry,
	uint64_t count, uint64_t offset);
extern int wtfs_test_bitmap_bit(struct super_block * vsb, uint64_t entry,
	uint64_t count, uint64_t offset);
extern struct buffer_head * wtfs_init_block(struct super_block * vsb,
	struct buffer_head * prev, uint64_t blk_no);
extern uint64_t wtfs_alloc_block(struct super_block * vsb);
extern uint64_t wtfs_alloc_free_inode(struct super_block * vsb);
extern struct inode * wtfs_new_inode(struct inode * dir_vi, umode_t mode);
extern void wtfs_free_block(struct super_block * vsb, uint64_t blk_no);
extern void wtfs_free_inode(struct super_block * vsb, uint64_t inode_no);
extern int wtfs_sync_super(struct super_block * vsb);
extern uint64_t wtfs_find_inode(struct inode * dir_vi, struct dentry * dentry);
extern int wtfs_add_entry(struct inode * dir_vi, uint64_t inode_no,
	const char * filename, size_t length);
extern int wtfs_delete_entry(struct inode * dir_vi, uint64_t inode_no);
extern void wtfs_delete_inode(struct inode * vi);

#endif /* __KERNEL__ */

#endif /* WTFS_H_ */
