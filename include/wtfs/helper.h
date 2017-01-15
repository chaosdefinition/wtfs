/*
 * helper.h - definition and declaration of some helper functions.
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

#ifndef WTFS_HELPER_H_
#define WTFS_HELPER_H_

#include <wtfs/utils.h>

/*****************************************************************************
 * Inline functions are defined here *****************************************
 *****************************************************************************/

/*
 * Get size of a regular file in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 *
 * return: size of the file
 */
static inline loff_t wtfs_file_size(const struct wtfs_inode * inode)
{
	return wtfs64_to_cpu(inode->file_size);
}

/*
 * Get number of blocks of a regular file in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 * @blk_size: size of a block in this wtfs instance
 *
 * return: number of blocks of the file
 */
static inline blkcnt_t wtfs_file_blkcnt(const struct wtfs_inode * inode,
					uint64_t blk_size)
{
	blkcnt_t dblocks = wtfs_file_size(inode) / blk_size + 1;
	blkcnt_t iblocks = dblocks / WTFS_INDEX_COUNT_PER_BLOCK + 1;
	return dblocks + iblocks;
}

/*
 * Get number of blocks of a directory in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 *
 * return: number of blocks of the directory
 */
static inline blkcnt_t wtfs_dir_blkcnt(const struct wtfs_inode * inode)
{
	blkcnt_t divisor = WTFS_DENTRY_COUNT_PER_BLOCK;
	return wtfs64_to_cpu(inode->dentry_count) / divisor + 1;
}

/*
 * Get size of a directory in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 * @blk_size: size of a block in this wtfs instance
 *
 * return: size of the directory
 */
static inline loff_t wtfs_dir_size(const struct wtfs_inode * inode,
				   uint64_t blk_size)
{
	return wtfs_dir_blkcnt(inode) * blk_size;
}

/*
 * Get UID of an inode in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 *
 * return: the UID
 */
static inline uid_t wtfs_i_uid_read(const struct wtfs_inode * inode)
{
	return (wtfs16_to_cpu(inode->huid) << 16) | wtfs16_to_cpu(inode->uid);
}

/*
 * Get GID of an inode in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 *
 * return: the GID
 */
static inline gid_t wtfs_i_gid_read(const struct wtfs_inode * inode)
{
	return (wtfs16_to_cpu(inode->hgid) << 16) | wtfs16_to_cpu(inode->gid);
}

/*
 * Set the value of UID of an inode in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 * @uid: the new UID value
 */
static inline void wtfs_i_uid_write(struct wtfs_inode * inode, uid_t uid)
{
	inode->uid = cpu_to_wtfs16(uid);
	inode->huid = cpu_to_wtfs16(uid >> 16);
}

/*
 * Set the value of GID of an inode in wtfs.
 *
 * @inode: the on-disk inode structure in wtfs
 * @gid: the new GID value
 */
static inline void wtfs_i_gid_write(struct wtfs_inode * inode, gid_t gid)
{
	inode->gid = cpu_to_wtfs16(gid);
	inode->hgid = cpu_to_wtfs16(gid >> 16);
}

/*****************************************************************************
 * Others are defined in helper.c ********************************************
 *****************************************************************************/

struct inode * wtfs_iget(struct super_block * vsb, ino_t ino);
struct wtfs_inode * wtfs_get_inode(struct super_block * vsb, ino_t ino,
				   struct buffer_head ** pbh);
int ino_valid(struct super_block * vsb, ino_t ino);
struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
					   uint64_t entry, int64_t count,
					   uint64_t * blk_no);
int wtfs_set_bitmap_bit(struct super_block * vsb, uint64_t entry,
			int64_t count, uint64_t offset);
int wtfs_clear_bitmap_bit(struct super_block * vsb, uint64_t entry,
			  int64_t count, uint64_t offset);
int wtfs_test_bitmap_bit(struct super_block * vsb, uint64_t entry,
			 int64_t count, uint64_t offset);
int wtfs_sync_super(struct super_block * vsb, int wait);
struct inode * wtfs_new_inode(struct inode * dir_vi, umode_t mode,
			      const char * path, size_t length);
ino_t wtfs_alloc_ino(struct super_block * vsb);
uint64_t wtfs_alloc_block(struct super_block * vsb);
void wtfs_free_ino(struct super_block * vsb, ino_t ino);
void wtfs_free_block(struct super_block * vsb, uint64_t blkno);
struct buffer_head * wtfs_init_linked_block(struct super_block * vsb,
					    uint64_t blkno,
					    struct buffer_head * prev);
int wtfs_add_dentry(struct inode * dir_vi, struct inode * vi,
		    const char * filename, size_t length);

#endif /* WTFS_HELPER_H_ */
