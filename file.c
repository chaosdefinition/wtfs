/*
 * file.c - implementation of wtfs file operations.
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

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "wtfs.h"

/* declaration of file operations */
static ssize_t wtfs_read(struct file * file, char __user * buf, size_t length,
	loff_t * ppos);
static ssize_t wtfs_write(struct file * file, const char __user * buf,
	size_t length, loff_t * ppos);
static loff_t wtfs_llseek(struct file * file, loff_t offset, int whence);
static int wtfs_open(struct inode * vi, struct file * file);
static int wtfs_release(struct inode * vi, struct file * file);

const struct file_operations wtfs_file_ops = {
	.read = wtfs_read,
	.write = wtfs_write,
	.llseek = wtfs_llseek,
	.open = wtfs_open,
	.release = wtfs_release,
};

/* structure to store I/O position */
struct wtfs_file_pos
{
	/* current read/write position */
	uint64_t pos;

	/* block available to read/write */
	uint64_t blk_no;
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
static ssize_t wtfs_read(struct file * file, char __user * buf, size_t length,
	loff_t * ppos)
{
	struct inode * vi = file->f_inode;
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_file_pos * file_pos = file->private_data;
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, count, offset, remain, next;
	ssize_t ret = -EIO, nbytes;

	wtfs_debug("read called, inode %lu, length %lu, pos %llu\n",
		vi->i_ino, length, *ppos);

	/* check if we reach the EOF */
	if (*ppos >= i_size_read(vi)) {
		return 0;
	}

	/* calculate which block to start read */
	count = *ppos / WTFS_DATA_SIZE;
	offset = *ppos % WTFS_DATA_SIZE;

	if (file_pos->pos != 0 && file_pos->pos == *ppos) {
		/*
		 * this is the subsequent call of previous read
		 * use the last block number directly
		 */
		next = file_pos->blk_no;
	} else {
		/* skip the first count-th blocks from beginning */
		next = info->first_block;
		for (i = 0; next != 0 && i < count; ++i) {
			if ((bh = sb_bread(vsb, next)) == NULL) {
				wtfs_error("unable to read the block %llu\n",
					next);
				goto error;
			}
			block = (struct wtfs_data_block *)bh->b_data;
			next = wtfs64_to_cpu(block->next);
			brelse(bh);
		}
	}

	/* start reading */
	ret = 0;
	remain = i_size_read(vi) - count * WTFS_DATA_SIZE - offset;
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
			file_pos->blk_no = wtfs64_to_cpu(block->next);
		} else {
			file_pos->blk_no = next;
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

	/* update position pointer */
	*ppos += ret;

	/* record the position read */
	file_pos->pos = *ppos;

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
	struct wtfs_file_pos * file_pos = file->private_data;
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t i, count, offset, next;
	ssize_t ret = -EIO, nbytes;

	wtfs_debug("write called, inode %lu, buf_size %lu, pos %llu\n",
		vi->i_ino, length, *ppos);

	/* calculate which block to start write */
	count = *ppos / WTFS_DATA_SIZE;
	offset = *ppos % WTFS_DATA_SIZE;

	if (file_pos->pos != 0 && file_pos->pos == *ppos) {
		/*
		 * this is the subsequent call of previous write
		 * use the last block number directly
		 */
		next = file_pos->blk_no;
	} else {
		/* skip the first count-th blocks from beginning */
		next = info->first_block;
		for (i = 0; next != 0 && i < count; ++i) {
			if ((bh = sb_bread(vsb, next)) == NULL) {
				wtfs_error("unable to read the block %llu\n",
					next);
				goto error;
			}
			block = (struct wtfs_data_block *)bh->b_data;
			next = wtfs64_to_cpu(block->next);
			brelse(bh);
		}
	}

	/* start writing */
	ret = 0;
	while (length > 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			break;
		}
		block = (struct wtfs_data_block *)bh->b_data;

		/* max bytes we can write to this block */
		nbytes = wtfs_min(WTFS_DATA_SIZE - offset, length);
		copy_from_user(block->data + offset, buf + ret, nbytes);
		mark_buffer_dirty(bh);

		/* record block number */
		if (nbytes == WTFS_DATA_SIZE - offset) {
			file_pos->blk_no = wtfs64_to_cpu(block->next);
			/*
			 * if we reach the last block, pre-allocate a new data
			 * block
			 */
			if (file_pos->blk_no == 0) {
				if ((file_pos->blk_no = wtfs_alloc_block(vsb))
					== 0) {
					/*
					 * here failure is not allowed, neither
					 * in the following
					 */
					goto error;
				}
				bh2 = wtfs_init_linked_block(vsb,
					file_pos->blk_no, bh);
				if (IS_ERR(bh2)) {
					wtfs_free_block(vsb, file_pos->blk_no);
					goto error;
				}
				brelse(bh2);
				++vi->i_blocks;
				mark_inode_dirty(vi);
			}
		} else {
			file_pos->blk_no = next;
		}

		/* update bytes write */
		ret += nbytes;
		length -= nbytes;
		offset = 0;

		next = wtfs64_to_cpu(block->next);
		brelse(bh);
	}

	wtfs_debug("write %ld bytes\n", ret);

	/* update position pointer */
	*ppos += ret;

	/* update file size */
	i_size_write(vi, *ppos);
	mark_inode_dirty(vi);

	/* record the position written */
	file_pos->pos = *ppos;

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
 * return: the offset from the beginning of the file after seeking on success,
 *         error code otherwise
 */
static loff_t wtfs_llseek(struct file * file, loff_t offset, int whence)
{
	struct inode * vi = file->f_inode;
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_file_pos * file_pos = file->private_data;
	struct buffer_head * bh = NULL;
	uint64_t file_size = i_size_read(vi);
	uint64_t seek_pos;
	uint64_t count, count2;
	uint64_t blk_no;
	int ret = -EINVAL;

	wtfs_debug("llseek called, inode %lu, current pos %llu, start pos %d, "
		"offset %llu\n", vi->i_ino, file->f_pos, whence, offset);

	/*
	 * in llseek, we update not only file position pointer, but also blk_no
	 * field in struct wtfs_file_pos
	 */
	switch (whence) {
	case SEEK_SET:
		/* check if exceeding the file size */
		if (offset < 0 || offset > file_size) {
			goto error;
		}

		/* get block number of the count-th block */
		count = offset / WTFS_DATA_SIZE;
		bh = wtfs_get_linked_block(vsb, info->first_block, count,
			&blk_no);
		if (IS_ERR(bh)) {
			ret = -EIO;
			goto error;
		}
		brelse(bh);

		file->f_pos = file_pos->pos = offset;
		file_pos->blk_no = blk_no; /* update block number */

		wtfs_debug("seek to %llu-th block %llu\n", count, blk_no);

		return file->f_pos;

	case SEEK_CUR:
		/* check if exceeding the file size */
		seek_pos = file->f_pos + offset;
		if (seek_pos < 0 || seek_pos > file_size) {
			goto error;
		}

		count = seek_pos / WTFS_DATA_SIZE;
		count2 = file->f_pos / WTFS_DATA_SIZE;
		if (count == count2) {
			/*
			 * current position and seeking position are in the
			 * same block
			 */
			file->f_pos = file_pos->pos = seek_pos;

			wtfs_debug("seek to %llu-th block %llu\n",
				count, file_pos->blk_no);

			return file->f_pos;
		} else if (count > count2) {
			/*
			 * current position and seeking position are not in the
			 * same block, and the seeking position is ahead of the
			 * current, so we do seeking by starting reading the
			 * link from the current block
			 */
			bh = wtfs_get_linked_block(vsb, file_pos->blk_no,
				count - count2, &blk_no);
			if (IS_ERR(bh)) {
				ret = -EIO;
				goto error;
			}
			brelse(bh);

			file->f_pos = file_pos->pos = seek_pos;
			file_pos->blk_no = blk_no; /* update block number */

			wtfs_debug("seek to %llu-th block %llu\n",
				count, blk_no);

			return file->f_pos;
		}
		/*
		 * in other cases, we have no efficient way to do seeking from
		 * the current position, so just seek from the beginning
		 */
		return wtfs_llseek(file, file->f_pos + offset, SEEK_SET);

	case SEEK_END:
		/* check if exceeding the file size */
		seek_pos = file_size + offset;
		if (offset > 0 || seek_pos < 0) {
			goto error;
		}

		count = seek_pos / WTFS_DATA_SIZE;
		count2 = file->f_pos / WTFS_DATA_SIZE;
		if (count == count2) {
			/*
			 * current position and seeking position are in the
			 * same block
			 *
			 * this is the only special case we can fast deal with
			 * in a single-chained file
			 */
			file->f_pos = file_pos->pos = seek_pos;

			wtfs_debug("seek to %llu-th block %llu\n",
				count, file_pos->blk_no);

			return file->f_pos;
		}
		/*
		 * in other cases, we have no efficient way to do seeking from
		 * the EOF, so just seek from the beginning
		 */
		return wtfs_llseek(file, file_size + offset, SEEK_SET);
	}

error:
	return ret;
}

/********************* implementation of open *********************************/

/*
 * routine called by the VFS when an inode should be opened
 *
 * @vi: the VFS inode to open
 * @file: the VFS file structure
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_open(struct inode * vi, struct file * file)
{
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_file_pos * data = NULL;
	int ret = -EINVAL;

	wtfs_debug("open called, inode %lu\n", vi->i_ino);

	if (file->private_data == NULL) {
		/* alloc a new struct wtfs_file_pos */
		data = (struct wtfs_file_pos *)kzalloc(sizeof(*data),
			GFP_KERNEL);
		if (data == NULL) {
			ret = -ENOMEM;
			goto error;
		}

		/* set private_data */
		data->blk_no = info->first_block;
		file->private_data = data;
		return 0;
	}

error:
	return ret;
}

/********************* implementation of release ******************************/

/*
 * routine called when the last reference to an open file is closed
 *
 * @vi: the VFS inode associated with the file
 * @file: the VFS file structure
 *
 * return: 0
 */
static int wtfs_release(struct inode * vi, struct file * file)
{
	wtfs_debug("release called, inode %lu\n", vi->i_ino);

	if (file->private_data != NULL) {
		/* release the memory we alloc at the open call */
		kfree(file->private_data);
	}

	/* TODO: do shrink here */

	return 0;
}
