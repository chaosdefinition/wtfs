/*
 * file.c - implementation of wtfs file operations.
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

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/err.h>

#include "wtfs.h"

/* declaration of file operations */
static ssize_t wtfs_read(struct file * file, char __user * buf,
	size_t length, loff_t * ppos);
static ssize_t wtfs_write(struct file * file, const char __user * buf,
	size_t length, loff_t * ppos);
static loff_t wtfs_llseek(struct file * file, loff_t offset, int whence);

const struct file_operations wtfs_file_ops = {
	.read = wtfs_read,
	.write = wtfs_write,
	.llseek = wtfs_llseek
};

/********************* implementation of read *********************************/

/*
 * routine called to read the content of a file
 *
 * @file: the VFS file structure
 * @buf: the userspace buffer to hold the content
 * @length: length of the buffer in byte
 * @ppos: current position to read
 *
 * return: size of content actually read or error code
 */
static ssize_t wtfs_read(struct file * file, char __user * buf,
	size_t length, loff_t * ppos)
{
	struct inode * vi = file->f_inode;
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, count, offset, remain, next;
	ssize_t ret = -EINVAL, nbytes;

	/* use static variables to speed up reading */
	static uint64_t last_ino, last_blk_no, last_pos;

	wtfs_debug("read called, inode %lu, length %lu, pos %llu\n",
		vi->i_ino, length, *ppos);
	wtfs_debug("last_ino %llu, last_blk_no %llu, last_pos %llu\n",
		last_ino, last_blk_no, last_pos);

	/* check if we reach the EOF */
	if (*ppos >= vi->i_size) {
		return 0;
	}

	/* calculate which block to start read */
	count = *ppos / WTFS_DATA_SIZE;
	offset = *ppos % WTFS_DATA_SIZE;

	if (last_ino == vi->i_ino && last_pos == *ppos) {
		/*
		 * this is the subsequent call of previous read
		 * use the last block number directly
		 */
		next = last_blk_no;
	} else {
		/* skip the first count-th blocks from beginning */
		next = info->first_block;
		for (i = 0; next != 0 && i < count; ++i) {
			if ((bh = sb_bread(vsb, next)) == NULL) {
				wtfs_error("unable to read the block %llu\n", next);
				goto error;
			}
			block = (struct wtfs_data_block *)bh->b_data;
			next = wtfs64_to_cpu(block->next);
			brelse(bh);
		}
	}

	/* start reading */
	ret = 0;
	remain = vi->i_size - count * WTFS_DATA_SIZE - offset;
	while (remain > 0 && length > 0 && next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			break;
		}
		block = (struct wtfs_data_block *)bh->b_data;

		/* max bytes we can read from this block */
		nbytes = wtfs_min3(WTFS_DATA_SIZE - offset, length, remain);
		copy_to_user(buf + ret, block->data + offset, nbytes);

		/* record correct block number */
		if (nbytes == WTFS_DATA_SIZE - offset) {
			last_blk_no = wtfs64_to_cpu(block->next);
		} else {
			last_blk_no = next;
		}

		/* update bytes read */
		ret += nbytes;
		length -= nbytes;
		remain -= nbytes;
		offset = 0;

		next = wtfs64_to_cpu(block->next);
		brelse(bh);
	}

	wtfs_debug("read %ld bytes\n", ret);

	*ppos += ret; /* update position pointer */

	/* record inode number and the position read */
	last_ino = vi->i_ino;
	last_pos = *ppos;

	return ret;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}

/********************* implementation of write ********************************/

/*
 * routine called to write content to a file
 *
 * @file: the VFS file structure
 * @buf: the userspace buffer holding the content to write
 * @length: length of the content
 * @ppos: current position to write
 *
 * return: size of content actually written or error code
 */
static ssize_t wtfs_write(struct file * file, const char __user * buf,
	size_t length, loff_t * ppos)
{
	struct inode * vi = file->f_inode;
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t i, count, offset, next;
	uint64_t blk_no;
	ssize_t ret = -EINVAL, nbytes;

	wtfs_debug("write called, inode %lu, buf_size %lu, pos %llu\n",
		vi->i_ino, length, *ppos);

	/* calculate which block to start write */
	count = *ppos / WTFS_DATA_SIZE;
	offset = *ppos % WTFS_DATA_SIZE;

	/* skip the first count-th blocks from beginning */
	next = info->first_block;
	for (i = 0; next != 0 && i < count; ++i) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		block = (struct wtfs_data_block *)bh->b_data;
		next = wtfs64_to_cpu(block->next);
		/* do not release the last block */
		if (next == 0 || i == count) {
			break;
		}
		brelse(bh);
	}

	/* start writing */
	ret = 0;
	while (length > 0) {
		if (next != 0) {
			brelse(bh);
			if ((bh = sb_bread(vsb, next)) == NULL) {
				wtfs_error("unable to read the block %llu\n", next);
				break;
			}
		} else {
			/* alloc a new data block */
			if ((blk_no = wtfs_alloc_block(vsb)) == 0) {
				break;
			}
			bh2 = wtfs_init_block(vsb, bh, blk_no);
			if (IS_ERR(bh2)) {
				wtfs_free_block(vsb, blk_no);
				break;
			}
			brelse(bh);
			bh = bh2;
			next = blk_no;
			++vi->i_blocks;
			mark_inode_dirty(vi);
		}

		block = (struct wtfs_data_block *)bh->b_data;

		/* max bytes we can write to this block */
		nbytes = wtfs_min(WTFS_DATA_SIZE - offset, length);
		copy_from_user(block->data + offset, buf + ret, nbytes);
		mark_buffer_dirty(bh);

		/* update bytes write */
		ret += nbytes;
		length -= nbytes;
		offset = 0;

		next = wtfs64_to_cpu(block->next);
	}
	/* release the last buffer here */
	if (bh != NULL) {
		brelse(bh);
	}

	wtfs_debug("write %ld bytes\n", ret);

	*ppos += ret; /* update position pointer */
	i_size_write(vi, *ppos); /* update file size */
	mark_inode_dirty(vi);
	return ret;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}

/********************* implementation of llseek *******************************/

/*
 * routine called when the VFS needs to move the file position index
 *
 * @file: the VFS file structure
 * @offset: the offset to move from the current position
 * @whence: the current position to start seeking, can be SEEK_SET, SEEK_CUR or
 *          SEEK_END
 *
 * return: the offset from the beginning of the file after seeking or error code
 */
static loff_t wtfs_llseek(struct file * file, loff_t offset, int whence)
{
	return -EINVAL;
}
