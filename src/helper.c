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

static int __wtfs_init_dir(struct inode * vi);
static int __wtfs_init_file(struct inode * vi);
static int __wtfs_init_symlink(struct inode * vi, const char * path,
			       size_t length);
static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry,
				 int extendable);
static void __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
			    uint64_t objno);

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

	/* Get the count-th inode table from linked list */
	*pbh = wtfs_get_linked_block(vsb, sbi->inode_table_first, count, NULL);
	if (IS_ERR(*pbh)) {
		ret = PTR_ERR(*pbh);
		*pbh = NULL;
		goto error;
	}

	/* Here we get the inode */
	return (struct wtfs_inode *)(*pbh)->b_data + offset;

error:
	if (*pbh != NULL) {
		brelse(*pbh);
		/* Here we set it to NULL to avoid freeing it twice */
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
 * @blkno: place to store the block number, can be NULL
 *
 * return: the buffer_head of the block on success, error code otherwise
 *         it must be released outside after this function being called
 */
struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
					   uint64_t entry, int64_t count,
					   uint64_t * blkno)
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
			if (blkno != NULL) {
				*blkno = next;
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

/*
 * Create a new inode.
 *
 * @dir_vi: directory inode
 * @mode: file mode
 * @path: path linking to, only valid when the new inode is to be a symlink
 * @length: length of path, only valid when the new inode is to be a symlink
 *
 * return: the new inode on success, error code otherwise
 */
struct inode * wtfs_new_inode(struct inode * dir_vi, umode_t mode,
			      const char * path, size_t length)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct inode * vi = NULL;
	struct wtfs_inode_info * info = NULL;
	int ret = -EINVAL;

	/* Allocate a new VFS inode */
	vi = new_inode(vsb);
	if (vi == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	info = WTFS_INODE_INFO(vi);

	/* Allocate an inode number */
	vi->i_ino = wtfs_alloc_ino(vsb);
	if (vi->i_ino == 0) {
		wtfs_error("Inode numbers have been used up\n");
		ret = -ENOSPC;
		goto error;
	}

	/* Allocate a data block */
	info->first_block = wtfs_alloc_block(vsb);
	if (info->first_block == 0) {
		wtfs_error("Free blocks have been used up\n");
		ret = -ENOSPC;
		goto error;
	}

	/* Set up file type relevant stuffs */
	switch (mode & S_IFMT) {
	case S_IFDIR:
		if ((ret = __wtfs_init_dir(vi)) < 0) {
			goto error;
		}
		break;
	case S_IFREG:
		if ((ret = __wtfs_init_file(vi)) < 0) {
			goto error;
		}
		break;
	case S_IFLNK:
		if ((ret = __wtfs_init_symlink(vi, path, length)) < 0) {
			goto error;
		}
		break;
	default:
		wtfs_error("Special file type not supported\n");
		goto error;
	}

	/* Set up other things */
	inode_init_owner(vi, dir_vi, mode);
	vi->i_atime = vi->i_ctime = vi->i_mtime = CURRENT_TIME_SEC;
	clear_nlink(vi);

	insert_inode_hash(vi);
	mark_inode_dirty(vi);
	return vi;

error:
	/* We need to return the inode number and block on failure */
	if (vi != NULL) {
		if (info->first_block != 0) {
			wtfs_free_block(vsb, info->first_block);
		}
		if (vi->i_ino != 0) {
			wtfs_free_inode(vsb, vi->i_ino);
		}
		iput(vi);
	}
	return ERR_PTR(ret);
}

/*
 * Internal function used to initialize a VFS inode for directory.
 *
 * @vi: the VFS inode structure
 *
 * return: 0 on success, error code otherwise
 */
static int __wtfs_init_dir(struct inode * vi)
{
	struct super_block * vsb = vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct buffer_head * bh = NULL;

	/* Set up data block */
	bh = wtfs_init_linked_block(vsb, info->first_block, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}
	brelse(bh);

	/* Set up inode fields */
	vi->i_op = &wtfs_dir_inops;
	vi->i_fop = &wtfs_dir_ops;
	info->dentry_count = 0;
	i_size_write(vi, sbi->block_size);
	vi->i_blocks = 1;
	return 0;
}

/*
 * Internal function used to initialize a VFS inode for regular file.
 *
 * @vi: the VFS inode structure
 *
 * return: 0 on success, error code otherwise
 */
static int __wtfs_init_file(struct inode * vi)
{
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_index_block * blk = NULL;
	struct buffer_head * bh = NULL;
	uint64_t blkno;

	/* Set up index block */
	bh = wtfs_init_linked_block(vsb, info->first_block, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}
	blk = (struct wtfs_index_block *)bh->b_data;
	blkno = wtfs_alloc_block(vsb);
	if (blkno == 0) {
		brelse(bh);
		return -ENOSPC;
	}
	blk->indices[0] = cpu_to_wtfs64(blkno);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Set up inode fields */
	vi->i_op = &wtfs_file_inops;
	vi->i_fop = &wtfs_file_ops;
	i_size_write(vi, 0);
	vi->i_blocks = 2;
}

/*
 * Internal function used to initialize a VFS inode for symlink.
 *
 * @vi: the VFS inode structure
 * @path: path linking to
 * @length: length of path
 *
 * return: 0 on success, error code otherwise
 */
static int __wtfs_init_symlink(struct inode * vi, const char * path,
			       size_t length)
{
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_symlink_block * symlink = NULL;
	struct buffer_head * bh = NULL;

	/* Set up data block */
	if ((bh = sb_bread(vsb, info->first_block)) == NULL) {
		wtfs_error("Failed to read block %llu\n", info->first_block);
		return -EIO;
	}
	symlink = (struct wtfs_symlink_block *)bh->b_data;
	symlink->length = cpu_to_wtfs16(length);
	memcpy(symlink->path, path, length);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Set up inode fields */
	vi->i_op = &wtfs_symlink_inops;
	i_size_write(vi, length);
	vi->i_blocks = 1;
	return 0;
}

/*
 * Allocate an inode number.
 *
 * @vsb: the VFS super block structure
 *
 * return: a new inode number on success, 0 otherwise
 */
ino_t wtfs_alloc_ino(struct super_block * vsb)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	ino_t ino;

	ino = __wtfs_alloc_obj(vsb, sbi->inode_bitmap_first, 1);
	if (ino != 0) {
		++sbi->inode_count;
		wtfs_sync_super(vsb, 0);

		wtfs_debug("Inodes: %llu\n", sbi->inode_count);
	}
	return ino;
}

/*
 * Allocate a block.
 *
 * @vsb: the VFS super block structure
 *
 * return: block number on success, 0 otherwise
 */
uint64_t wtfs_alloc_block(struct super_block * vsb)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t blkno;

	/*
	 * If total block count is smaller than that one block bitmap can
	 * state, we have to do this check explicitly.
	 */
	if (sbi->free_block_count == 0) {
		return 0;
	}

	blkno = __wtfs_alloc_obj(vsb, sbi->block_bitmap_first, 0);
	if (blkno != 0) {
		--sbi->free_block_count;
		wtfs_sync_super(vsb, 0);

		wtfs_debug("Free blocks: %llu\n", sbi->free_block_count);
	}
	return blkno;
}

/*
 * Internal function used to allocate a block or an inode number.
 *
 * @vsb: the VFS super block structure
 * @entry: block number of the first block/inode bitmap
 * @extendable: whether or not the bitmap is extendable
 *
 * return: block/inode number on success, 0 otherwise
 */
static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry,
				 int extendable)
{
	struct wtfs_bitmap_block * bitmap = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t blkno = 0;
	uint64_t next, i, j;

	/* Find the first zero bit in bitmaps */
	next = entry;
	i = 0; /* Bitmap counter */
	j = 0; /* Bit counter */
	do {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("Failed to read block %llu\n", next);
			goto error;
		}
		bitmap = (struct wtfs_bitmap_block *)bh->b_data;

		wtfs_debug("Finding first zero bit in bitmap %llu\n", next);
		j = wtfs_find_first_zero_bit(bitmap->data, WTFS_BITMAP_SIZE * 8);
		if (j < WTFS_BITMAP_SIZE * 8) {
			wtfs_debug("Found a zero bit %llu in bitmap %llu\n",
				   j, next);
			wtfs_set_bit(j, bitmap->data);
			mark_buffer_dirty(bh);
			brelse(bh);
			break;
		}

		++i;
		next = wtfs64_to_cpu(bitmap->next);
		if (next != entry) {
			brelse(bh);
		}
	} while (next != entry);

	/* Objects has been used up */
	if (next == entry) {
		if (!extendable) {
			goto error;
		}
		/* Allocate a new block to be the next bitmap */
		blkno = wtfs_alloc_block(vsb);
		if (blkno == 0) {
			goto error;
		}
		bh2 = wtfs_init_linked_block(vsb, blkno, bh);
		if (IS_ERR(bh2)) {
			goto error;
		}
		brelse(bh); /* Now it's okay to release previous bitmap */
		bh = NULL; /* Avoid double freeing on error */
		bitmap = (struct wtfs_bitmap_block *)bh2->b_data;

		/* Continue finding zero bit */
		wtfs_debug("Finding first zero bit in bitmap %llu\n", blkno);
		j = wtfs_find_first_zero_bit(bitmap->data, WTFS_BITMAP_SIZE * 8);
		if (j < WTFS_BITMAP_SIZE * 8) {
			wtfs_debug("Found a zero bit %llu in bitmap %llu\n",
				   j, blkno);
			wtfs_set_bit(j, bitmap->data);
			mark_buffer_dirty(bh2);
			brelse(bh2);
		} else {
			wtfs_error("Something strange happened\n");
			goto error;
		}
	}

	return i * WTFS_BITMAP_SIZE * 8 + j;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (blkno != 0) {
		wtfs_free_block(vsb, blkno);
	}
	if (bh2 != NULL) {
		brelse(bh2);
	}
	return 0;
}

/*
 * Free an inode number.
 *
 * @vsb: the VFS super block structure
 * @ino: the inode number
 */
void wtfs_free_ino(struct super_block * vsb, ino_t ino)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);

	if (ino != 0 && ino != WTFS_ROOT_INO) {
		__wtfs_free_obj(vsb, sbi->inode_bitmap_first, ino);
		--sbi->inode_count; /* Decrease inode counter */
		wtfs_sync_super(vsb, 0);

		wtfs_debug("Inodes: %llu\n", sbi->inode_count);
	}
}

/*
 * Free a block.
 *
 * @vsb: the VFS super block structure
 * @blkno: the block number
 */
void wtfs_free_block(struct super_block * vsb, uint64_t blkno)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);

	if (sbi->free_block_count < sbi->block_count) {
		__wtfs_free_obj(vsb, sbi->block_bitmap_first, blkno);
		++sbi->free_block_count; /* Increase free block counter */
		wtfs_sync_super(vsb, 0);

		wtfs_debug("Free blocks: %llu\n", sbi->free_block_count);
	}
}

/*
 * Internal function used to free a block or an inode number.
 *
 * @vsb: the VFS super block structure
 * @entry: block number of the first block/inode bitmap
 * @objno: the block/inode number
 */
static void __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
			    uint64_t objno)
{
	uint64_t block, offset;

	block = objno / (WTFS_BITMAP_SIZE * 8);
	offset = objno % (WTFS_BITMAP_SIZE * 8);
	wtfs_clear_bitmap_bit(vsb, entry, block, offset);
}

/*
 * Initialize a linked list block.
 *
 * @vsb: the VFS super block structure
 * @blkno: block number
 * @prev: buffer_head of previous block that should point to this block,
 *        can be NULL
 *
 * return: the buffer_head of the block on success, error code otherwise
 *         it must be released outside after this function being called
 */
struct buffer_head * wtfs_init_linked_block(struct super_block * vsb,
					    uint64_t blkno,
					    struct buffer_head * prev)
{
	struct wtfs_linked_block * blk = NULL;
	struct wtfs_linked_block * prev_blk = NULL, * next_blk = NULL;
	struct buffer_head * bh = NULL, * next = NULL;
	int ret = -EIO;

	if ((bh = sb_bread(vsb, blkno)) == NULL) {
		wtfs_error("Failed to read block %llu\n", blkno);
		goto error;
	}

	blk = (struct wtfs_linked_block *)bh->b_data;
	memset(blk, 0, sizeof(*blk));

	if (prev != NULL) {
		prev_blk = (struct wtfs_linked_block *)prev->b_data;
		next = sb_bread(vsb, wtfs64_to_cpu(prev_blk->next));
		if (next == NULL) {
			wtfs_error("Failed to read block %llu\n",
				   wtfs64_to_cpu(prev_blk->next));
			goto error;
		}
		next_blk = (struct wtfs_linked_block *)next->b_data;

		/* Join this block to the existing linked list */
		blk->prev = next_blk->prev;
		blk->next = prev_blk->next;
		prev_blk->next = cpu_to_wtfs64(blkno);
		next_blk->prev = cpu_to_wtfs64(blkno);
		mark_buffer_dirty(prev);
		mark_buffer_dirty(next);
		brelse(next);
	} else {
		/* New linked list */
		blk->prev = cpu_to_wtfs64(blkno);
		blk->next = cpu_to_wtfs64(blkno);
	}
	mark_buffer_dirty(bh);

	return bh;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/*
 * Add a new dentry to a directory.
 *
 * @dir_vi: the VFS inode of the directory
 * @vi: the VFS inode associated with the new dentry
 * @filename: name of the new dentry
 * @length: size of name
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_add_dentry(struct inode * dir_vi, struct inode * vi,
		    const char * filename, size_t length)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_inode_info * dir_info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_dir_block * blk = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t next, blkno = 0;
	int i;
	int ret = -EIO;

	/* Check filename */
	if (length == 0) {
		wtfs_error("No dentry name specified\n");
		ret = -ENOENT;
		goto error;
	}
	if (length >= WTFS_FILENAME_MAX) {
		wtfs_error("Dentry name too long\n");
		ret = -ENAMETOOLONG;
		goto error;
	}

	/* Find an empty dentry in existing dentries */
	next = dir_info->first_block;
	do {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("Failed to read block %llu\n", next);
			goto error;
		}
		blk = (struct wtfs_dir_block *)bh->b_data;

		for (i = 0; i < WTFS_DENTRY_COUNT_PER_BLOCK; ++i) {
			/* Found it */
			if (blk->dentries[i].ino == 0) {
				/* Update parent directory */
				blk->dentries[i].ino = cpu_to_wtfs64(vi->i_ino);
				strncpy(blk->dentries[i].filename, filename,
					length);
				mark_buffer_dirty(bh);
				brelse(bh);
				dir_vi->i_ctime = CURRENT_TIME_SEC;
				dir_vi->i_mtime = CURRENT_TIME_SEC;
				++dir_info->dentry_count;
				mark_inode_dirty(dir_vi);
				/* Update associated inode */
				inc_nlink(vi);
				mark_inode_dirty(vi);
				return 0;
			}
		}
		next = wtfs64_to_cpu(blk->next);
		if (next != dir_info->first_block) {
			brelse(bh);
		}
	} while (next != dir_info->first_block);

	/*
	 * Dentries have been used up, so we create a new data block for
	 * parent directory.
	 */
	if ((blkno = wtfs_alloc_block(vsb)) == 0) {
		ret = -ENOSPC;
		goto error;
	}
	bh2 = wtfs_init_linked_block(vsb, blkno, bh);
	if (IS_ERR(bh2)) {
		ret = PTR_ERR(bh2);
		bh2 = NULL;
		goto error;
	}
	brelse(bh); /* Now we can release the previous block */
	blk = (struct wtfs_dir_block *)bh2->b_data;
	blk->dentries[0].ino = cpu_to_wtfs64(vi->i_ino);
	strncpy(blk->dentries[0].filename, filename, length);
	mark_buffer_dirty(bh2);
	brelse(bh2);

	/* Update parent directory's information */
	dir_vi->i_ctime = dir_vi->i_mtime = CURRENT_TIME_SEC;
	++dir_vi->i_blocks;
	i_size_write(dir_vi, i_size_read(dir_vi) + sbi->block_size);
	++dir_info->dentry_count;
	mark_inode_dirty(dir_vi);

	/* Update associated inode */
	inc_nlink(vi);
	mark_inode_dirty(vi);
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (bh2 != NULL) {
		brelse(bh2);
	}
	if (blkno != 0) {
		wtfs_free_block(vsb, blkno);
	}
	return ret;
}