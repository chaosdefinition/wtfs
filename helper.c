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

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/err.h>

#include "wtfs.h"

/* declaration of internal helper functions */
static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry);
static void __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
	uint64_t no);

/********************* implementation of wtfs_iget ****************************/

/*
 * get the VFS inode from the inode cache, if missed, add a new one into
 * the cache and fill it with information retrieved from disk
 *
 * @vsb: the VFS super block structure
 * @inode_no: inode number
 *
 * return: a pointer to the VFS inode on success, error code otherwise
 */
struct inode * wtfs_iget(struct super_block * vsb, uint64_t inode_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct inode * vi = NULL;
	struct wtfs_inode * inode = NULL;
	struct wtfs_inode_info * info = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	/* get inode in cache */
	vi = iget_locked(vsb, (unsigned long)inode_no);
	if (vi == NULL) {
		wtfs_error("unable to get the inode of number %llu\n",
			inode_no);
		ret = -ENOMEM;
		goto error;
	}
	info = WTFS_INODE_INFO(vi);

	/* inode already in cache */
	if (!(vi->i_state & I_NEW)) {
		return vi;
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
	vi->i_ino = (unsigned long)wtfs64_to_cpu(inode->inode_no);
	vi->i_mode = (umode_t)wtfs32_to_cpu(inode->mode);
	vi->i_blocks = (blkcnt_t)wtfs64_to_cpu(inode->block_count);
	vi->i_atime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->atime);
	vi->i_ctime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->ctime);
	vi->i_mtime.tv_sec = (__kernel_time_t)wtfs64_to_cpu(inode->mtime);
	vi->i_atime.tv_nsec = 0;
	vi->i_ctime.tv_nsec = 0;
	vi->i_mtime.tv_nsec = 0;
	i_uid_write(vi, wtfs16_to_cpu(inode->uid));
	i_gid_write(vi, wtfs16_to_cpu(inode->gid));
	info->first_block = wtfs64_to_cpu(inode->first_block);
	switch (vi->i_mode & S_IFMT) {
	case S_IFDIR:
		i_size_write(vi, wtfs64_to_cpu(inode->block_count) *
			sbi->block_size);
		vi->i_op = &wtfs_dir_inops;
		vi->i_fop = &wtfs_dir_ops;
		info->dir_entry_count = wtfs64_to_cpu(inode->dir_entry_count);
		break;

	case S_IFREG:
		i_size_write(vi, wtfs64_to_cpu(inode->file_size));
		vi->i_op = &wtfs_file_inops;
		vi->i_fop = &wtfs_file_ops;
		break;

	case S_IFLNK:
		i_size_write(vi, wtfs64_to_cpu(inode->file_size));
		vi->i_op = &wtfs_symlink_inops;
		break;

	default:
		wtfs_error("special file type not supported\n");
		goto error;
	}

	/* finally release the buffer and unlock the new VFS inode */
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

/********************* implementation of wtfs_get_inode ***********************/

/*
 * get the physical inode from disk
 * the buffer_head must be released after calling this function
 *
 * @vsb: the VFS super block structure
 * @inode_no: inode number
 * @pbh: a pointer to struct buffer_head
 *
 * return: a pointer to the physical inode on success, error code otherwise
 */
struct wtfs_inode * wtfs_get_inode(struct super_block * vsb, uint64_t inode_no,
	struct buffer_head ** pbh)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t count, offset;
	int ret = -EINVAL;

	/* first check if inode number is valid */
	if (!is_ino_valid(vsb, inode_no)) {
		wtfs_error("invalid inode number %llu\n", inode_no);
		goto error;
	}

	/* calculate the index of inode table and the offset */
	count = (inode_no - WTFS_ROOT_INO) / WTFS_INODE_COUNT_PER_TABLE;
	offset = (inode_no - WTFS_ROOT_INO) % WTFS_INODE_COUNT_PER_TABLE;

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

/********************* implementation of is_ino_valid *************************/

/*
 * check if the given inode number is valid
 *
 * @vsb: the VFS super block
 * @inode_no: inode number
 *
 * return: 1 if valid, 0 otherwise
 */
int is_ino_valid(struct super_block * vsb, uint64_t inode_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t block, offset;

	/* calculate the index of inode bitmap and the offset */
	block = inode_no / (WTFS_BITMAP_SIZE * 8);
	offset = inode_no % (WTFS_BITMAP_SIZE * 8);

	return wtfs_test_bitmap_bit(vsb, sbi->inode_bitmap_first, block,
		offset);
}

/********************* implementation of wtfs_get_linked_block ****************/

/*
 * get the specified block in the block linked list
 *
 * @vsb: the VFS super block structure
 * @entry: the entry block number
 * @count: the position of the block we want in the linked list
 * @blk_no: place to store the block number, can be NULL
 *
 * return: the buffer_head of the block on success, error code otherwise
 *         it must be released outside after calling this function
 */
struct buffer_head * wtfs_get_linked_block(struct super_block * vsb,
	uint64_t entry, uint64_t count, uint64_t * blk_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_linked_block * blk = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next, i;
	int ret = -EINVAL;

	/* first check if the start block number is valid */
	if (entry < WTFS_RB_INODE_TABLE || entry >= sbi->block_count) {
		wtfs_error("invalid block number %llu in linked list\n", entry);
		goto error;
	}

	/* find the count-th block */
	next = entry;
	i = 0;
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", entry);
			goto error;
		}
		if (i == count) {
			if (blk_no != NULL) {
				*blk_no = next;
			}
			return bh;
		} else {
			blk = (struct wtfs_linked_block *)bh->b_data;
			++i;
			next = wtfs64_to_cpu(blk->next);
			brelse(bh);
		}
	}
	return ERR_PTR(ret);

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/********************* implementation of bitmap operations ********************/

/*
 * set a bit in bitmap
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_set_bitmap_bit(struct super_block * vsb, uint64_t entry,
	uint64_t count, uint64_t offset)
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
 * clear a bit in bitmap
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_clear_bitmap_bit(struct super_block * vsb, uint64_t entry,
		uint64_t count, uint64_t offset)
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
 * test a bit in bitmap
 *
 * @vsb: the VSF super block structure
 * @entry: block number of the first bitmap
 * @count: index of bitmap
 * @offset: index of the bit in bitmap
 *
 * return: bit status (0 or 1) on success, error code otherwise
 */
int wtfs_test_bitmap_bit(struct super_block * vsb, uint64_t entry,
		uint64_t count, uint64_t offset)
{
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	bh = wtfs_get_linked_block(vsb, entry, count, NULL);
	if (IS_ERR(bh)) {
		return PTR_ERR(bh);
	}

	ret = wtfs_test_bit(offset, bh->b_data);
	brelse(bh);
	return ret;
}

/********************* implementation of wtfs_init_linked_block ***************/

/*
 * initialize a block
 *
 * @vsb: the VFS super block structure
 * @blk_no: block number
 * @prev: buffer_head of previous block to point to the block, can be NULL
 *
 * return: the buffer_head of the block on success, error code otherwise
 *         it must be released outside after calling this function
 */
struct buffer_head * wtfs_init_linked_block(struct super_block * vsb,
	uint64_t blk_no, struct buffer_head * prev)
{
	struct wtfs_linked_block * blk = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	wtfs_debug("read block %llu\n", blk_no);
	if ((bh = sb_bread(vsb, blk_no)) == NULL) {
		wtfs_error("unable to read the block %llu\n", blk_no);
		goto error;
	}

	blk = (struct wtfs_linked_block *)bh->b_data;
	memset(blk, 0, sizeof(*blk));
	mark_buffer_dirty(bh);

	if (prev != NULL) {
		blk = (struct wtfs_linked_block *)prev->b_data;
		blk->next = cpu_to_wtfs64(blk_no);
		mark_buffer_dirty(prev);
	}

	return bh;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/********************* implementation of wtfs_alloc_block *********************/

/*
 * alloc a free block
 *
 * @vsb: the VFS super block structure
 *
 * return: block number on success, 0 otherwise
 */
uint64_t wtfs_alloc_block(struct super_block * vsb)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t blk_no;

	/*
	 * if total block count is smaller than that one block bitmap can state,
	 * we have to do this check explicitly
	 */
	if (sbi->free_block_count == 0) {
		return 0;
	}

	blk_no = __wtfs_alloc_obj(vsb, sbi->block_bitmap_first);
	if (blk_no != 0) {
		--sbi->free_block_count;
		wtfs_sync_super(vsb, 0);

		wtfs_debug("free blocks: %llu\n", sbi->free_block_count);
	}
	return blk_no;
}

/*
 * internal function used to alloc a free block/inode
 *
 * @vsb: the VFS super block structure
 * @entry: block number of the first block/inode bitmap
 *
 * return: block/inode number on success, 0 otherwise
 */
static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry)
{
	struct wtfs_bitmap_block * bitmap = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next, i, j;

	/* find the first zero bit in bitmaps */
	next = entry;
	i = 0; /* bitmap counter */
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the bitmap %llu\n", next);
			goto error;
		}
		bitmap = (struct wtfs_bitmap_block *)bh->b_data;

		wtfs_debug("finding first zero bit in bitmap %llu\n", next);
		j = wtfs_find_first_zero_bit(bitmap->data, WTFS_BITMAP_SIZE * 8);
		if (j < WTFS_BITMAP_SIZE * 8) {
			wtfs_debug("find a zero bit %llu in bitmap %llu\n",
				j, next);
			wtfs_set_bit(j, bitmap->data);
			mark_buffer_dirty(bh);
			brelse(bh);
			break;
		}

		++i;
		next = wtfs64_to_cpu(bitmap->next);
		brelse(bh);
	}

	/* obj used up */
	if (next == 0) {
		return 0;
	}

	j += i * WTFS_BITMAP_SIZE * 8;
	return j;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return 0;
}

/********************* implementation of wtfs_alloc_free_inode ****************/

/*
 * alloc a free inode
 *
 * @vsb: the VFS super block structure
 *
 * return: inode number on success, 0 otherwise
 */
uint64_t wtfs_alloc_free_inode(struct super_block * vsb)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	uint64_t inode_no;

	inode_no = __wtfs_alloc_obj(vsb, sbi->inode_bitmap_first);
	if (inode_no != 0) {
		++sbi->inode_count;
		wtfs_sync_super(vsb, 0);

		wtfs_debug("inodes: %llu\n", sbi->inode_count);
	}
	return inode_no;
}

/********************* implementation of wtfs_new_inode ***********************/

/*
 * create a new inode
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
	struct wtfs_symlink_block * symlink = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	/* alloc a new VFS inode */
	vi = new_inode(vsb);
	if (vi == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	info = WTFS_INODE_INFO(vi);

	/* set file type relevant stuffs */
	switch (mode & S_IFMT) {
	case S_IFDIR:
		vi->i_op = &wtfs_dir_inops;
		vi->i_fop = &wtfs_dir_ops;
		info->dir_entry_count = 0;
		i_size_write(vi, sbi->block_size);
		break;

	case S_IFREG:
		vi->i_op = &wtfs_file_inops;
		vi->i_fop = &wtfs_file_ops;
		i_size_write(vi, 0);
		break;

	case S_IFLNK:
		vi->i_op = &wtfs_symlink_inops;
		i_size_write(vi, length);
		break;

	default:
		wtfs_error("special file type not supported\n");
		goto error;
	}

	/* alloc an inode number */
	vi->i_ino = wtfs_alloc_free_inode(vsb);
	if (vi->i_ino == 0) {
		wtfs_error("inode numbers have used up\n");
		ret = -ENOSPC;
		goto error;
	}

	/* alloc a data block and initialize it */
	info->first_block = wtfs_alloc_block(vsb);
	if (info->first_block == 0) {
		wtfs_error("free blocks have used up\n");
		ret = -ENOSPC;
		goto error;
	}
	bh = wtfs_init_linked_block(vsb, info->first_block, NULL);
	if (IS_ERR(bh)) {
		ret = PTR_ERR(bh);
		goto error;
	}
	if (S_ISLNK(mode)) {
		symlink = (struct wtfs_symlink_block *)bh->b_data;
		symlink->length = cpu_to_wtfs16(length);
		memcpy(symlink->path, path, length);
		mark_buffer_dirty(bh);
	}
	brelse(bh);

	/* set other things */
	inode_init_owner(vi, dir_vi, mode);
	vi->i_atime = vi->i_ctime = vi->i_mtime = CURRENT_TIME_SEC;
	vi->i_blocks = 1;
	insert_inode_hash(vi);
	mark_inode_dirty(vi);

	return vi;

error:
	/* we need to return the inode number and block on fail */
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

/********************* implementation of wtfs_free_block **********************/

/*
 * free a block
 *
 * @vsb: the VFS super block structure
 * @blk_no: the block number
 */
void wtfs_free_block(struct super_block * vsb, uint64_t blk_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);

	if (sbi->free_block_count < sbi->block_count) {
		__wtfs_free_obj(vsb, sbi->block_bitmap_first, blk_no);
		++sbi->free_block_count; /* increase free block counter */
		wtfs_sync_super(vsb, 0);

		wtfs_debug("free blocks: %llu\n", sbi->free_block_count);
	}
}

/*
 * internal function used to free a block/inode
 *
 * @vsb: the VFS super block structure
 * @entry: block number of the first block/inode bitmap
 * @no: the block/inode number
 */
static void __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
	uint64_t no)
{
	uint64_t block, offset;

	block = no / (WTFS_BITMAP_SIZE * 8);
	offset = no % (WTFS_BITMAP_SIZE * 8);
	wtfs_clear_bitmap_bit(vsb, entry, block, offset);
}

/********************* implementation of wtfs_free_inode **********************/

/*
 * free an inode
 *
 * @vsb: the VFS super block structure
 * @inode_no: the inode number
 */
void wtfs_free_inode(struct super_block * vsb, uint64_t inode_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);

	if (inode_no != 0 && inode_no != WTFS_ROOT_INO) {
		__wtfs_free_obj(vsb, sbi->inode_bitmap_first, inode_no);
		--sbi->inode_count; /* decrease inode counter */
		wtfs_sync_super(vsb, 0);

		wtfs_debug("inodes: %llu\n", sbi->inode_count);
	}
}

/********************* implementation of wtfs_sync_super **********************/

/*
 * write back super block information to disk
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
	int ret = -EINVAL;

	if ((bh = sb_bread(vsb, WTFS_RB_SUPER)) == NULL) {
		wtfs_error("unable to read the super block\n");
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
			wtfs_error("super block sync failed\n");
			ret = -EIO;
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

/********************* implementation of wtfs_find_inode **********************/

/*
 * find the specified dentry in a directory
 *
 * @dir_vi: the VFS inode of the directory
 * @dentry: the dentry to search
 *
 * return: inode number on success, 0 otherwise
 */
uint64_t wtfs_find_inode(struct inode * dir_vi, struct dentry * dentry)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_dir_block * blk = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next = info->first_block, inode_no;
	int i;

	/* first check if name is too long */
	if (dentry->d_name.len >= WTFS_FILENAME_MAX) {
		goto error;
	}

	/* do search */
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		blk = (struct wtfs_dir_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			inode_no = wtfs64_to_cpu(blk->entries[i].inode_no);
			if (inode_no != 0 && strcmp(blk->entries[i].filename,
					dentry->d_name.name) == 0) {
				brelse(bh);
				return inode_no;
			}
		}
		next = wtfs64_to_cpu(blk->next);
		brelse(bh);
	}
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return 0;
}

/********************* implementation of wtfs_add_entry ***********************/

/*
 * add a new entry to a directory
 *
 * @dir_vi: the VFS inode of the directory
 * @inode_no: inode number of the new entry
 * @filename: name of the new entry
 * @length: size of name
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_add_entry(struct inode * dir_vi, uint64_t inode_no,
	const char * filename, size_t length)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_inode_info * dir_info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_dir_block * blk = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t next = dir_info->first_block, blk_no = 0;
	int i;
	int ret = -EINVAL;

	/* check name */
	if (length == 0) {
		wtfs_error("no dentry name specified\n");
		ret = -ENOENT;
		goto error;
	}
	if (length >= WTFS_FILENAME_MAX) {
		wtfs_error("dentry name too long %s\n", filename);
		ret = -ENAMETOOLONG;
		goto error;
	}

	/* find an empty entry in existing entries */
	while (1) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		blk = (struct wtfs_dir_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			/* find it */
			if (blk->entries[i].inode_no == 0) {
				blk->entries[i].inode_no = inode_no;
				strncpy(blk->entries[i].filename, filename,
					length);
				mark_buffer_dirty(bh);
				brelse(bh);
				dir_vi->i_ctime = CURRENT_TIME_SEC;
				dir_vi->i_mtime = CURRENT_TIME_SEC;
				++dir_info->dir_entry_count;
				mark_inode_dirty(dir_vi);
				return 0;
			}
		}
		next = wtfs64_to_cpu(blk->next);
		/*
		 * do not release the last block because we are to set its
		 * pointer
		 */
		if (next == 0) {
			break;
		}
		brelse(bh);
	}

	/* entries used up, so we have to create a new data block */
	if ((blk_no = wtfs_alloc_block(vsb)) == 0) {
		ret = -ENOSPC;
		goto error;
	}
	bh2 = wtfs_init_linked_block(vsb, blk_no, bh);
	if (IS_ERR(bh2)) {
		ret = PTR_ERR(bh2);
		bh2 = NULL;
		goto error;
	}
	brelse(bh); /* now we can release the previous block */
	blk = (struct wtfs_dir_block *)bh2->b_data;
	blk->entries[0].inode_no = inode_no;
	strncpy(blk->entries[0].filename, filename, length);
	mark_buffer_dirty(bh2);
	brelse(bh2);

	/* update parent directory's information */
	dir_vi->i_ctime = dir_vi->i_mtime = CURRENT_TIME_SEC;
	++dir_vi->i_blocks;
	i_size_write(dir_vi, i_size_read(dir_vi) + sbi->block_size);
	++dir_info->dir_entry_count;
	mark_inode_dirty(dir_vi);
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (bh2 != NULL) {
		brelse(bh2);
	}
	if (blk_no != 0) {
		wtfs_free_block(vsb, blk_no);
	}
	return ret;
}

/********************* implementation of wtfs_delete_entry ********************/

/*
 * delete an entry of a directory
 *
 * @dir_vi: the VFS inode of the directory
 * @inode_no: inode number of the entry to delete
 *
 * return: 0 on success, error code otherwise
 */
int wtfs_delete_entry(struct inode * dir_vi, uint64_t inode_no)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_inode_info * dir_info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_dir_block * blk = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next = dir_info->first_block;
	int i;
	int ret = -EINVAL;

	/* find the specified entry in existing entries */
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		blk = (struct wtfs_dir_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			if (blk->entries[i].inode_no == inode_no) {
				memset(&(blk->entries[i]), 0,
					sizeof(struct wtfs_dentry));
				mark_buffer_dirty(bh);
				brelse(bh);

				/* also, update parent dir's info */
				dir_vi->i_ctime = CURRENT_TIME_SEC;
				dir_vi->i_mtime = CURRENT_TIME_SEC;
				--dir_info->dir_entry_count;
				mark_inode_dirty(dir_vi);
				return 0;
			}
		}
		next = wtfs64_to_cpu(blk->next);
		brelse(bh);
	}

	/* not found */
	return -ENOENT;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}

/********************* implementation of wtfs_delete_inode ********************/

/*
 * delete an inode on disk
 *
 * @vi: the VFS inode structure
 */
void wtfs_delete_inode(struct inode * vi)
{
	struct super_block * vsb = vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_data_block * blk = NULL;
	struct wtfs_inode_table * table = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, next;

	/* first free inode number in inode bitmap */
	wtfs_free_inode(vsb, vi->i_ino);

	/* then clear inode data in inode table */
	next = sbi->inode_table_first;
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		table = (struct wtfs_inode_table *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			if (table->inodes[i].inode_no == vi->i_ino) {
				memset(&(table->inodes[i]), 0,
					sizeof(struct wtfs_inode));
				mark_buffer_dirty(bh);
				brelse(bh);
				goto out;
			}
		}
		next = wtfs64_to_cpu(table->next);
		brelse(bh);
	}

out:
	/* finally release file data blocks */
	next = info->first_block;
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		blk = (struct wtfs_data_block *)bh->b_data;
		i = wtfs64_to_cpu(blk->next);
		wtfs_free_block(vsb, next);
		next = i;
		brelse(bh);
	}

	return;

error:
	if (bh != NULL) {
		brelse(bh);
	}
}
