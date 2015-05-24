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
#include <linux/printk.h>

#include "macro_utils.h"

/*
 * version of wtfs
 * we may evolve several versions of it
 */
#define WTFS_VERSION 0x0001

/*
 * version 1 physical disk layout:
 * +-------------+-------------+-------------------+--------------+------+-----+
 * | boot loader | super block | first inode table | first bitmap | data | ... |
 * +-------------+-------------+-------------------+--------------+------+-----+
 *
 * -- block information --
 * size of each block:              4096 bytes
 *
 * -- inode information --
 * size of each inode:              64 bytes
 * max inode count per table:       63
 * max inode count:                 0xfffffffeul
 *
 * -- bitmap information --
 * max block count per bitmap:      4088 * 8
 *
 * -- data block information --
 * size of each dentry:             64 bytes
 * max dentry count per block:      63
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

/*
 * size of real data that each block can contain (except boot and super block)
 * we need the last 8 bytes as a pointer to another block
 */
#define WTFS_DATA_SIZE (WTFS_BLOCK_SIZE - sizeof(wtfs64_t))

/* reserved block indices */
#define WTFS_RB_BOOT            0   /* boot loader block */
#define WTFS_RB_SUPER           1   /* super block */
#define WTFS_RB_INODE_FIRST     2   /* first inode table block */
#define WTFS_RB_BITMAP_FIRST    3   /* first bitmap block */

/* first data block index (for root directory) */
#define WTFS_DB_FIRST           4

/* inode number of root directory */
#define WTFS_ROOT_INO 1

/* max inode number */
#define WTFS_INODE_MAX 0xfffffffeul

/* DEBUG macro for wtfs */
#ifdef DEBUG
# define WTFS_DEBUG 1
#endif /* DEBUG */

/* macros for trace & debug */
#ifdef WTFS_DEBUG
# define wtfs_debug(fmt, ...)\
	printk(KERN_DEBUG "[wtfs] at %s:%d %s: " fmt,\
	__FILE__, __LINE__, __FUNC__, ##__VA_ARGS__)
# define wtfs_error(fmt, ...)\
	printk(KERN_ERR "[wtfs]: " fmt, ##__VA_ARGS__)
# define wtfs_info(fmt, ...)\
	printk(KERN_INFO "[wtfs]: " fmt, ##__VA_ARGS__)
#else
# define wtfs_debug(fmt, ...) {}
# define wtfs_error(fmt, ...)\
	no_printk(fmt, ##__VA_ARGS__)
# define wtfs_info(fmt, ...)\
	no_printk(fmt, ##__VA_ARGS__)
#endif /* WTFS_DEBUG */

/* structure for super block in disc */
struct wtfs_super_block
{
	wtfs64_t version;                   /* 8 bytes */
	wtfs64_t magic;                     /* 8 bytes */
	wtfs64_t block_size;                /* 8 bytes */
	wtfs64_t block_count;               /* 8 bytes */

	wtfs64_t inode_table_first;         /* 8 bytes */
	wtfs64_t inode_table_count;         /* 8 bytes */
	wtfs64_t bitmap_first;              /* 8 bytes */
	wtfs64_t bitmap_count;              /* 8 bytes */

	wtfs64_t inode_count;               /* 8 bytes */
	wtfs64_t free_block_count;          /* 8 bytes */

	wtfs8_t padding                     /* 4016 bytes */
	[
		WTFS_BLOCK_SIZE - sizeof(wtfs64_t) * 10
	];
};

/* structure for super block in memory */
struct wtfs_sb_info
{
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	uint64_t block_count;

	uint64_t inode_table_first;
	uint64_t inode_table_count;
	uint64_t bitmap_first;
	uint64_t bitmap_count;

	uint64_t inode_count;
	uint64_t free_block_count;
};

/* structure for inode in disc */
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

/* structure for inode in memory */
struct wtfs_inode_info
{
	uint64_t dir_entry_count;
	struct inode vfs_inode;
};

/* structure for inode table block */
struct wtfs_inode_table_block
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

/* structure for bitmap block */
struct wtfs_bitmap_block
{
	wtfs8_t map[WTFS_DATA_SIZE];        /* 4088 bytes */
	wtfs64_t next;                      /* 8 bytes */
};

/* structure for directory data */
struct wtfs_dentry
{
	wtfs64_t inode_no;		            /* 8 bytes */
	char filename[WTFS_FILENAME_MAX];   /* 56 bytes */
};

/* structure for data block */
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

/* get sb_info from the VFS super block */
static inline struct wtfs_sb_info * WTFS_SB_INFO(struct super_block * vsb)
{
	return (struct wtfs_sb_info *)vsb->s_fs_info;
}

/* get inode_info from the VFS inode */
static inline struct wtfs_inode_info * WTFS_INODE_INFO(struct inode * inode)
{
	return container_of(inode, struct wtfs_inode_info, vfs_inode);
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

#endif /* WTFS_H_ */
