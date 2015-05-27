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

/* declaration of internal helper functions */
static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry);
static uint64_t __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
	uint64_t no);

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
	struct inode * vi = NULL;
	struct wtfs_inode * inode = NULL;
	struct wtfs_inode_info * info = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	/* get inode in cache */
	vi = iget_locked(vsb, (unsigned long)inode_no);
	if (vi == NULL) {
		wtfs_error("unable to get the inode of number %llu\n", inode_no);
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
	vi->i_nlink = 1;
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
		vi->i_size = wtfs64_to_cpu(inode->block_count) * sbi->block_size;
		vi->i_op = &wtfs_dir_inops;
		vi->i_fop = &wtfs_dir_ops;
		info->dir_entry_count = wtfs64_to_cpu(inode->dir_entry_count);
		break;

	case S_IFREG:
		vi->i_size = wtfs64_to_cpu(inode->file_size);
		vi->i_op = &wtfs_file_inops;
		vi->i_fop = &wtfs_file_ops;
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

	/* find the count-th block */
	block = (struct wtfs_data_block *)bh->b_data;
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
		block = (struct wtfs_data_block *)bh->b_data;
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

	if (!wtfs_test_bit(offset, bh->b_data)) {
		wtfs_set_bit(offset, bh->b_data);
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

	if (wtfs_test_bit(offset, bh->b_data)) {
		wtfs_clear_bit(offset, bh->b_data);
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

	ret = wtfs_test_bit(offset, bh->b_data);
	brelse(bh);
	return ret;
}

/********************* implementation of wtfs_init_block **********************/

/*
 * initialize a block
 *
 * @vsb: the VFS super block structure
 * @prev: buffer_head of previous block to point to the block, can be NULL
 * @blk_no: block number
 *
 * return: the buffer_head of the block
 */
struct buffer_head * wtfs_init_block(struct super_block * vsb,
	struct buffer_head * prev, uint64_t blk_no)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_data_block * blk = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	if ((bh = sb_bread(vsb, blk_no)) == NULL) {
		wtfs_error("unable to read the block %llu\n", blk_no);
		goto error;
	}

	blk = (struct wtfs_data_block *)bh->b_data;
	memset(blk->data, 0, sizeof(blk->data));
	mark_buffer_dirty(bh);

	if (prev != NULL) {
		blk = (struct wtfs_data_block *)prev->b_data;
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
 * return: block number, 0 on fail
 */
uint64_t wtfs_alloc_block(struct super_block * vsb)
{
	uint64_t blk_no;

	blk_no = __wtfs_alloc_obj(vsb, WTFS_SB_INFO(vsb)->block_bitmap_first);
	if (blk_no != 0) {
		--WTFS_SB_INFO(vsb)->free_block_count;
		wtfs_sync_super(vsb);
	}
	return blk_no;
}

static uint64_t __wtfs_alloc_obj(struct super_block * vsb, uint64_t entry)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_data_block * bitmap = NULL;
	struct buffer_head * bh = NULL;
	uint64_t next, i, j;

	/* find the first zero bit in bitmaps */
	next = entry;
	i = 0;
	do {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the bitmap %llu\n", next);
			goto error;
		}
		bitmap = (struct wtfs_data_block *)bh->b_data;
		next = wtfs64_to_cpu(bitmap->next);
		j = wtfs_find_first_zero_bit(bitmap->data, WTFS_DATA_SIZE * 8);
		if (j < WTFS_DATA_SIZE * 8) {
			wtfs_set_bit(j, bitmap->data);
			mark_buffer_dirty(bh);
			break;
		}
		++i;
		brelse(bh);
	} while (next > WTFS_DB_FIRST && next < sbi->block_count);

	/* obj used up or error */
	if (next <= WTFS_DB_FIRST || next >= sbi->block_count) {
		return 0;
	}

	j += i * WTFS_DATA_SIZE * 8;
	brelse(bh);
	return j;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return 0;
}

/********************* implementation of wtfs_alloc_inode *********************/

/*
 * alloc a free inode
 *
 * @vsb: the VFS super block structure
 *
 * return: inode number, 0 on fail
 */
uint64_t wtfs_alloc_inode(struct super_block * vsb)
{
	uint64_t inode_no;

	inode_no = __wtfs_alloc_obj(vsb, WTFS_SB_INFO(vsb)->inode_bitmap_first);
	if (inode_no != 0) {
		++WTFS_SB_INFO(vsb)->inode_count;
		wtfs_sync_super(vsb);
	}
	return inode_no;
}

/********************* implementation of wtfs_new_inode ***********************/

/*
 * create a new inode
 *
 * @dir_vi: directory inode
 * @mode: file mode
 *
 * return: the new inode or error
 */
struct inode * wtfs_new_inode(struct inode * dir_vi, umode_t mode)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct inode * vi = NULL;
	struct wtfs_inode_info * info = NULL;
	int ret = -EINVAL;

	vi = new_inode(vsb);
	if (vi == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	info = WTFS_INODE_INFO(vi);

	switch (vi->i_mode) {
	case S_IFDIR:
		vi->i_op = &wtfs_dir_inops;
		vi->i_fop = &wtfs_dir_ops;
		info->dir_entry_count = 0;
		break;

	case S_IFREG:
		vi->i_op = &wtfs_file_inops;
		vi->i_fop = &wtfs_file_ops;
		vi->i_size = 0;
		break;

	default:
		wtfs_error("special file type not supported\n");
		goto error;
	}

	vi->i_ino = wtfs_alloc_inode(vsb);
	if (vi->i_ino == 0) {
		wtfs_error("inode numbers have used up\n");
		ret = -ENOSPC;
		goto error;
	}

	info->first_block = wtfs_alloc_block(vsb);
	if (info->first_block == 0) {
		wtfs_error("free blocks have used up\n");
		ret = -ENOSPC;
		goto error;
	}
	wtfs_init_block(vsb, NULL, info->first_block);

	++sbi->inode_count;
	wtfs_sync_super(vsb);

	inode_init_owner(vi, dir_vi, mode);
	vi->i_atime = vi->i_ctime = vi->i_mtime = CURRENT_TIME_SEC;
	vi->i_blocks = 1;
	insert_inode_hash(vi);
	mark_inode_dirty(vi);

	return vi;

error:
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

void wtfs_free_block(struct super_block * vsb, uint64_t blk_no)
{
	__wtfs_free_obj(vsb, WTFS_SB_INFO(vsb)->block_bitmap_first, blk_no);
	++WTFS_SB_INFO(vsb)->free_block_count;
	wtfs_sync_super(vsb);
}

static void __wtfs_free_obj(struct super_block * vsb, uint64_t entry,
	uint64_t no)
{
	uint64_t block, offset;

	block = no / (WTFS_DATA_SIZE * 8);
	offset = no % (WTFS_DATA_SIZE * 8);
	wtfs_clear_bitmap_bit(vsb, entry, block, offset);
}

/********************* implementation of wtfs_free_inode **********************/

void wtfs_free_inode(struct super_block * vsb, uint64_t inode_no)
{
	__wtfs_free_obj(vsb, WTFS_SB_INFO(vsb)->inode_bitmap_first, inode_no);
	--WTFS_SB_INFO(vsb)->inode_count;
	wtfs_sync_super(vsb);
}

/********************* implementation of wtfs_sync_super **********************/

/*
 * write back super block information to disk
 *
 * @vsb: the VFS super block structure
 *
 * return: status
 */
int wtfs_sync_super(struct super_block * vsb)
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
	sync_dirty_buffer(bh);
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
 * return: inode number, 0 on fail
 */
uint64_t wtfs_find_inode(struct inode * dir_vi, struct dentry * dentry)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	struct wtfs_inode_info * info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, next = info->first_block;

	if (dentry->d_name.len > WTFS_FILENAME_MAX) {
		goto error;
	}

	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		block = (struct wtfs_data_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			if (block->entries[i].inode_no != 0 &&
				strcmp(block->entries[i].filename, dentry->d_name.name) == 0) {
				next = wtfs64_to_cpu(block->entries[i].inode_no);
				brelse(bh);
				return next;
			}
		}
		next = wtfs64_to_cpu(block->next);
		brelse(bh);
	}

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
 * return: status
 */
int wtfs_add_entry(struct inode * dir_vi, uint64_t inode_no,
	const char * filename, size_t length)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_inode_info * dir_info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL, * bh2 = NULL;
	uint64_t i, next = dir_info->first_block, blk_no = 0;
	int ret = -EINVAL;

	if (length == 0) {
		wtfs_error("no dentry name specified\n");
		ret = -ENOENT;
		goto error;
	}
	if (length > WTFS_FILENAME_MAX) {
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
		block = (struct wtfs_data_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			/* find it */
			if (block->entries[i].inode_no == 0) {
				block->entries[i].inode_no = inode_no;
				strncpy(block->entries[i].filename, filename, length);
				mark_buffer_dirty(bh);
				brelse(bh);
				dir_vi->i_ctime = dir_vi->i_mtime = CURRENT_TIME_SEC;
				++dir_info->dir_entry_count;
				mark_inode_dirty(dir_vi);
				return 0;
			}
		}
		next = wtfs64_to_cpu(block->next);
		/* do not release the last block because we are to set its pointer */
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
	bh2 = wtfs_init_block(vsb, bh, blk_no);
	if (IS_ERR(bh2)) {
		ret = PTR_ERR(bh2);
		bh2 = NULL;
		goto error;
	}
	brelse(bh);
	block = (struct wtfs_data_block *)bh2->b_data;
	block->entries[0].inode_no = inode_no;
	strncpy(block->entries[0].filename, filename, length);
	mark_buffer_dirty(bh2);
	brelse(bh2);
	dir_vi->i_ctime = dir_vi->i_mtime = CURRENT_TIME_SEC;
	++dir_vi->i_blocks;
	i_size_write(dir_vi, i_size_read(dir_vi) + WTFS_SB_INFO(vsb)->block_size);
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
 * return: status
 */
int wtfs_delete_entry(struct inode * dir_vi, uint64_t inode_no)
{
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_inode_info * dir_info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, next = dir_info->first_block;
	int ret = -ENOENT;

	/* find the specified entry in existing entries */
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		block = (struct wtfs_data_block *)bh->b_data;
		for (i = 0; i < WTFS_INODE_COUNT_PER_TABLE; ++i) {
			if (block->entries[i].inode_no == inode_no) {
				block->entries[i].inode_no = 0;
				mark_buffer_dirty(bh);
				brelse(bh);
				dir_vi->i_ctime = dir_vi->i_mtime = CURRENT_TIME_SEC;
				mark_inode_dirty(dir_vi);
				return 0;
			}
		}
		next = wtfs64_to_cpu(block->next);
		brelse(bh);
	}

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
	struct wtfs_data_block * block = NULL;
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
				memset(&(table->inodes[i]), 0, sizeof(struct wtfs_inode));
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
		block = (struct wtfs_data_block *)bh->b_data;
		wtfs_free_block(vsb, next);
		next = wtfs64_to_cpu(table->next);
		brelse(bh);
	}

error:
	if (bh != NULL) {
		brelse(bh);
	}
}
