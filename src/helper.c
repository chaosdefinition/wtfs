/*
 * helper.c - implementation of some helper functions.
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

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/err.h>

#include <wtfs/wtfs.h>
#include <wtfs/helper.h>

/*
 * Get the VFS inode from the inode cache.  If missed, add a new one into
 * the cache and fill it with information retrieved from disk.
 *
 * @vsb: the VFS super block structure
 * @ino: inode number
 *
 * return: a pointer to the VFS inode on success, error code otherwise
 */
struct inode * wtfs_iget(struct super_block * vsb, ino_t ino)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct inode * vi = NULL;
	struct wtfs_inode * inode = NULL;
	struct wtfs_inode_info * info = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	/* Get inode in cache */
	vi = iget_locked(vsb, (unsigned long)ino);
	if (vi == NULL) {
		wtfs_error("Failed to get the inode of number %llu\n", ino);
		ret = -ENOMEM;
		goto error;
	}
	info = WTFS_INODE_INFO(vi);

	/* Inode already in cache */
	if (!(vi->i_state & I_NEW)) {
		return vi;
	}

	/*
	 * Inode missed in cache, then we retrieve corresponding physical inode
	 * from disk and fill the VFS inode.
	 */
	inode = wtfs_get_inode(vsb, ino, &bh);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto error;
	}

	/* Now let's fill the VFS inode */
	vi->i_ino = (unsigned long)wtfs64_to_cpu(inode->ino);
	vi->i_mode = (umode_t)wtfs32_to_cpu(inode->mode);
	vi->i_atime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->atime);
	vi->i_ctime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->ctime);
	vi->i_mtime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->mtime);
	vi->i_atime.tv_nsec = 0;
	vi->i_ctime.tv_nsec = 0;
	vi->i_mtime.tv_nsec = 0;
	i_uid_write(vi, wtfs_i_uid_read(inode));
	i_gid_write(vi, wtfs_i_gid_read(inode));
	set_nlink(vi, wtfs32_to_cpu(inode->link_count));
	info->first_block = wtfs64_to_cpu(inode->first_block);
	switch (vi->i_mode & S_IFMT) {
	case S_IFDIR:
		i_size_write(vi, wtfs_dir_size(inode, sbi->block_size));
		vi->i_blocks = wtfs_dir_blkcnt(inode);
		vi->i_op = &wtfs_dir_inops;
		vi->i_fop = &wtfs_dir_ops;
		info->dentry_count = wtfs64_to_cpu(inode->dentry_count);
		break;
	case S_IFREG:
		i_size_write(vi, wtfs_file_size(inode));
		vi->i_blocks = wtfs_file_blkcnt(inode, sbi->block_size);
		vi->i_op = &wtfs_file_inops;
		vi->i_fop = &wtfs_file_ops;
		break;
	case S_IFLNK:
		i_size_write(vi, wtfs64_to_cpu(inode->file_size));
		vi->i_blocks = 1;
		vi->i_op = &wtfs_symlink_inops;
		break;
	default:
		wtfs_error("Special file type not supported\n");
		goto error;
	}

	/* Finally release the buffer and unlock the new VFS inode */
	brelse(bh);
	unlock_new_inode(vi);
	return vi;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (vi != NULL) {
		iget_failed(vi);
	}
	return ERR_PTR(ret);
}

/*
 * Get the physical inode from disk.
 * The buffer_head must be released after this function being called.
 *
 * @vsb: the VFS super block structure
 * @ino: inode number
 * @pbh: a pointer to struct buffer_head
 *
 * return: a pointer to the physical inode on success, error code otherwise
 */
struct wtfs_inode * wtfs_get_inode(struct super_block * vsb, ino_t ino,
				   struct buffer_head ** pbh)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	int64_t count;
	uint64_t offset;
	int ret = -EINVAL;

	/* First check if pbh is not NULL and inode number is valid */
	if (pbh == NULL) {
		wtfs_error("Pointer to the buffer_head cannot be NULL\n");
		goto error;
	}
	if (!is_ino_valid(vsb, ino)) {
		wtfs_error("Invalid inode number %llu\n", ino);
		goto error;
	}

	/* Calculate the index of inode table and the offset */
	count = (ino - WTFS_ROOT_INO) / WTFS_INODE_COUNT_PER_TABLE;
	offset = (ino - WTFS_ROOT_INO) % WTFS_INODE_COUNT_PER_TABLE;

	/* get the count-th inode table from linked list */
	*pbh = wtfs_get_linked_block(vsb, sbi->inode_table_first, count, NULL);
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
		/* here we set it to NULL to avoid freeing it twice */
		*pbh = NULL;
	}
	return ERR_PTR(ret);
}

/*
 * Check if the given inode number is valid.
 *
 * @vsb: the VFS super block
 * @ino: inode number
 *
 * return: 1 if valid, 0 otherwise
 */
int ino_valid(struct super_block * vsb, ino_t ino)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	int64_t count;
	uint64_t offset;

	/* Calculate the index of inode bitmap and the offset */
	count = ino / (WTFS_BITMAP_SIZE * 8);
	offset = ino % (WTFS_BITMAP_SIZE * 8);

	return wtfs_test_bitmap_bit(vsb, sbi->inode_bitmap_first,
				    count, offset);
}

/*
 * Get the specified block in the block linked list.
 *
 * @vsb: the VFS super block structure
 * @entry: the entry block number
 * @count: the position in the linked list, can be negative
 * @blk_no: place to store the block number, can be NULL
 *
 * return: the buffer_head of the block on success, error code otherwise
 *         it must be released outside after this function being called
 */
struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
					   uint64_t entry, int64_t count,
					   uint64_t * blk_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_linked_block * blk = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next;
	int64_t i;
	int ret = -EINVAL;

	/* First check if the start block number is valid */
	if (entry < WTFS_RB_ITABLE || entry >= sbi->block_count) {
		wtfs_error("Invalid block number %llu\n", entry);
		goto error;
	}

	/* Find the count-th block */
	next = entry;
	i = 0;
	do {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("Failed to read the block %llu\n", entry);
			ret = -EIO;
			goto error;
		}
		if (i == count) {
			if (blk_no != NULL) {
				*blk_no = next;
			}
			return bh;
		} else {
			blk = (struct wtfs_linked_block *)bh->b_data;
			if (count > 0) {
				++i;
				next = wtfs64_to_cpu(blk->next);
			} else {
				--i;
				next = wtfs64_to_cpu(blk->prev);
			}
			brelse(bh);
		}
	} while (next != entry);
	return ERR_PTR(ret);

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/*
 * Set a bit in bitmap.
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_set_bitmap_bit(struct super_block * vsb, uint64_t entry,
			int64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;

	bh = wtfs_get_linked_block(vsb, entry, count, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	if (!wtfs_test_bit(offset, bh->b_data)) {
		wtfs_set_bit(offset, bh->b_data);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
	return 0;
}

/*
 * Clear a bit in bitmap.
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_clear_bitmap_bit(struct super_block * vsb, uint64_t entry,
			  int64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;

	bh = wtfs_get_linked_block(vsb, entry, count, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	if (wtfs_test_bit(offset, bh->b_data)) {
		wtfs_clear_bit(offset, bh->b_data);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
	return 0;
}

/*
 * Test a bit in bitmap.
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: bit status (0 or 1) on success, error code otherwise
 */
int wtfs_test_bitmap_bit(struct super_block * vsb, uint64_t entry,
			 int64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;
	int ret;

	bh = wtfs_get_linked_block(vsb, entry, count, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	ret = wtfs_test_bit(offset, bh->b_data);
	brelse(bh);
	return ret;
}

/*
 * Write back super block information to disk.
 *
 * @vsb: the VFS super block structure
 * @wait: whether to wait for the super block to be synced to disk
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_sync_super(struct super_block * vsb, int wait)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_super_block * sb = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EIO;

	if ((bh = sb_bread(vsb, WTFS_RB_SUPER)) == NULL) {
		wtfs_error("Failed to read the super block\n");
		goto error;
	}

	sb = (struct wtfs_super_block *)bh->b_data;
	sb->version = cpu_to_wtfs64(sbi->version);
	sb->magic = cpu_to_wtfs64(sbi->magic);
	sb->block_size = cpu_to_wtfs64(sbi->block_size);
	sb->block_count = cpu_to_wtfs64(sbi->block_count);
	sb->inode_table_first = cpu_to_wtfs64(sbi->inode_table_first);
	sb->inode_table_count = cpu_to_wtfs64(sbi->inode_table_count);
	sb->block_bitmap_first = cpu_to_wtfs64(sbi->block_bitmap_first);
	sb->block_bitmap_count = cpu_to_wtfs64(sbi->block_bitmap_count);
	sb->inode_bitmap_first = cpu_to_wtfs64(sbi->inode_bitmap_first);
	sb->inode_bitmap_count = cpu_to_wtfs64(sbi->inode_bitmap_count);
	sb->inode_count = cpu_to_wtfs64(sbi->inode_count);
	sb->free_block_count = cpu_to_wtfs64(sbi->free_block_count);

	mark_buffer_dirty(bh);
	if (wait) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			wtfs_error("Failed to sync super block\n");
			goto error;
		}
	}
	brelse(bh);

	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}
