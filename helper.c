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
 * the cache and fill it with information retrieved from disc
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
	struct wtfs_inode_table_block * inode_table = NULL;
	struct wtfs_inode * inode = NULL;
	struct buffer_head * bh = NULL;
	uint64_t block, offset, i;
	uint64_t next;
	int ret = -EINVAL;

	/* first check if inode number is valid */
	if (inode_no < WTFS_ROOT_INO || inode_no > sbi->inode_count) {
		wtfs_error("invalid inode number %llu\n", inode_no);
		ret = -EIO;
		goto error;
	}

	/* get inode in cache */
	vfs_inode = iget_locked(vsb, (unsigned long)inode_no);
	if (vfs_inode == NULL) {
		wtfs_error("unable to get the inode of number %d\n", inode_no);
		ret = -ENOMEM;
		goto error;
	}

	/* inode already in cache */
	if (!(vfs_inode->i_state & I_NEW)) {
		return vfs_inode;
	}

	/*
	 * inode missed in cache, then we retrieve corresponding physical inode
	 * from disc and fill the VFS inode
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
 * get the physical inode from disc
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
	struct wtfs_inode_table_block * inode_table = NULL;
	uint64_t block, offset, i;
	uint64_t next;
	int ret = -EINVAL;

	/* first check if inode number is valid */
	if (inode_no < WTFS_ROOT_INO || inode_no > sbi->inode_count) {
		wtfs_error("invalid inode number %llu\n", inode_no);
		goto error;
	}

	/* calculate the inode table block index and the offset */
	block = (inode_no - WTFS_ROOT_INO) / WTFS_INODE_COUNT_PER_TABLE;
	offset = (inode_no - WTFS_ROOT_INO) % WTFS_INODE_COUNT_PER_TABLE;

	/* read the first inode table into buffer */
	if ((*pbh = sb_bread(vsb, sbi->inode_table_first))) {
		wtfs_error("unable to read the inode table\n");
		goto error;
	}
	inode_table = (struct wtfs_inode_table_block *)(*pbh)->b_data;

	/* find the block-th inode table */
	next = wtfs64_to_cpu(inode_table->next);
	for (i = 0; i < block; ++i) {
		brelse(*pbh);
		if ((*pbh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the inode table\n");
			goto error;
		}
		inode_table = (struct wtfs_inode_table_block *)(*pbh)->b_data;
		next = wtfs64_to_cpu(inode_table->next);
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
