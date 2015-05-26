/*
 * helper.c - implementation of some helper functions.
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

#include "tricks.h"

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/err.h>

#include "wtfs.h"

/********************* implementation of wtfs_iget ****************************/

/*
 * get the VFS inode from the inode cache, if missed, add a new one into
 * the cache and fill it with information retrieved from disk
 *
 * @vsb: the VFS super block structure
 * @inode_no: inode number
 *
 * return: a pointer to the VFS inode
 */
struct inode * wtfs_iget(struct super_block * vsb, uint64_t inode_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct inode * vfs_inode = NULL;
	struct wtfs_inode * inode = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	/* get inode in cache */
	vfs_inode = iget_locked(vsb, (unsigned long)inode_no);
	if (vfs_inode == NULL) {
		wtfs_error("unable to get the inode of number %llu\n", inode_no);
		ret = -ENOMEM;
		goto error;
	}

	/* inode already in cache */
	if (!(vfs_inode->i_state & I_NEW)) {
		return vfs_inode;
	}

	/*
	 * inode missed in cache, then we retrieve corresponding physical inode
	 * from disk and fill the VFS inode
	 */
	inode = wtfs_get_inode(vsb, inode_no, &bh);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto error;
	}

	/* now let's fill the VFS inode */
	vfs_inode->i_mode = (umode_t)wtfs32_to_cpu(inode->mode);
	vfs_inode->i_blocks = (blkcnt_t)wtfs64_to_cpu(inode->block_count);
	vfs_inode->i_atime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->atime);
	vfs_inode->i_ctime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->ctime);
	vfs_inode->i_mtime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->mtime);
	vfs_inode->i_atime.tv_nsec = 0;
	vfs_inode->i_ctime.tv_nsec = 0;
	vfs_inode->i_mtime.tv_nsec = 0;
	i_uid_write(vfs_inode, wtfs16_to_cpu(inode->uid));
	i_gid_write(vfs_inode, wtfs16_to_cpu(inode->gid));
	switch (vfs_inode->i_mode & S_IFMT) {
	case S_IFDIR:
		vfs_inode->i_size = wtfs64_to_cpu(inode->block_count) * sbi->block_size;
		vfs_inode->i_op = &wtfs_dir_inops;
		vfs_inode->i_fop = &wtfs_dir_ops;
		break;

	case S_IFREG:
		vfs_inode->i_size = wtfs64_to_cpu(inode->file_size);
		vfs_inode->i_op = &wtfs_file_inops;
		vfs_inode->i_fop = &wtfs_file_ops;
		break;

	default:
		wtfs_error("special file type not supported\n");
		goto error;
	}

	/* finally release the buffer and unlock the new VFS inode */
	brelse(bh);
	unlock_new_inode(vfs_inode);
	return vfs_inode;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (vfs_inode != NULL) {
		iget_failed(vfs_inode);
	}
	return ERR_PTR(ret);
}

/********************* implementation of wtfs_get_inode ***********************/

/*
 * get the physical inode from disk
 * the buffer_head must be released after calling this function
 *
 * @vsb: the VFS super block structure
 * @inode_no: inode number
 * @pbh: a pointer to struct buffer_head
 *
 * return: a pointer to the physical inode or error
 */
struct wtfs_inode * wtfs_get_inode(struct super_block * vsb, uint64_t inode_no,
	struct buffer_head ** pbh)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t block, offset;
	int ret = -EINVAL;

	/* first check if inode number is valid */
	if (is_ino_valid(vsb, inode_no) != 0) {
		wtfs_error("invalid inode number %llu\n", inode_no);
		goto error;
	}

	/* calculate the index of inode table and the offset */
	block = (inode_no - WTFS_ROOT_INO) / WTFS_INODE_COUNT_PER_TABLE;
	offset = (inode_no - WTFS_ROOT_INO) % WTFS_INODE_COUNT_PER_TABLE;

	/* get the block-th inode table from linked list */
	*pbh = wtfs_get_linked_block(vsb, sbi->inode_table_first, block);
	if (IS_ERR(*pbh)) {
		ret = PTR_ERR(*pbh);
		*pbh = NULL;
		goto error;
	}

	/* here we get the inode */
	return (struct wtfs_inode *)(*pbh)->b_data + offset;

error:
	if (*pbh != NULL) {
		brelse(*pbh);
		*pbh = NULL; /* here we set it to NULL to avoid freeing it twice */
	}
	return ERR_PTR(ret);
}

/********************* implementation of is_ino_valid *************************/

/*
 * check if the given inode number is valid
 *
 * @vsb: the VFS super block
 * @inode_no: inode number
 *
 * return: 0 if valid, a nonzero number otherwise
 */
int is_ino_valid(struct super_block * vsb, uint64_t inode_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t block, offset;

	/* calculate the index of inode bitmap and the offset */
	block = inode_no / (WTFS_DATA_SIZE * 8);
	offset = inode_no % (WTFS_DATA_SIZE * 8);

	return wtfs_test_bitmap_bit(vsb, sbi->inode_bitmap_first, block, offset);
}

/********************* implementation of wtfs_get_linked_block ****************/

/*
 * get the specified block in the block linked list
 *
 * @vsb: the VFS super block structure
 * @entry: the entry block number
 * @count: the position of the block we want in the linked list
 *
 * return: a pointer to buffer_head structure containing the block
 *         it must be released after calling this function
 */
struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
	uint64_t entry, uint64_t count)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next, i;
	int ret = -EINVAL;

	/* first check if the start block number is valid */
	if (entry < WTFS_RB_INODE_TABLE || entry >= sbi->block_count) {
		wtfs_error("invalid block number %llu in linked list\n", entry);
		goto error;
	}

	/* read the entry block into buffer */
	if ((bh = sb_bread(vsb, entry)) == NULL) {
		wtfs_error("unable to read the block %llu\n", entry);
		goto error;
	}
	block = (struct wtfs_data_block *)bh->b_data;

	/* find the count-th block */
	next = wtfs64_to_cpu(block->next);
	for (i = 0; i < count; ++i) {
		brelse(bh);
		if (next < WTFS_RB_INODE_TABLE || next >= sbi->block_count) {
			wtfs_error("invalid block number %llu in linked list\n", next);
			goto error;
		}
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		block = (struct wtfs_data_block *)(bh)->b_data;
		next = wtfs64_to_cpu(block->next);
	}
	return bh;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/********************* implementation of bitmap operations ********************/

int wtfs_set_bitmap_bit(struct super_block * vsb, uint64_t entry,
	uint64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;

	bh = wtfs_get_linked_block(vsb, entry, count);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	if (!test_bit(offset, (const volatile unsigned long *)bh->b_data)) {
		set_bit(offset, (volatile unsigned long *)bh->b_data);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
	return 0;
}

int wtfs_clear_bitmap_bit(struct super_block * vsb, uint64_t entry,
		uint64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;

	bh = wtfs_get_linked_block(vsb, entry, count);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	if (test_bit(offset, (const volatile unsigned long *)bh->b_data)) {
		clear_bit(offset, (volatile unsigned long *)bh->b_data);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
	return 0;
}

int wtfs_test_bitmap_bit(struct super_block * vsb, uint64_t entry,
		uint64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	bh = wtfs_get_linked_block(vsb, entry, count);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	ret = test_bit(offset, (const volatile unsigned long *)bh->b_data);
	brelse(bh);
	return ret;
}
