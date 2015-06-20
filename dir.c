/*
 * dir.c - implementation of wtfs directory operations.
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

/* declaration of directory operations */
static int wtfs_iterate(struct file * file, struct dir_context * ctx);

const struct file_operations wtfs_dir_ops = {
	.iterate = wtfs_iterate
};

/********************* implementation of iterate ******************************/

/*
 * routine called when the VFS needs to read the directory contents
 *
 * @file: the VFS file structure of the directory
 * @ctx: directory context
 *
 * return: 0 or error
 */
static int wtfs_iterate(struct file * file, struct dir_context * ctx)
{
	struct inode * dir_vi = file_inode(file);
	struct super_block * vsb = dir_vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(dir_vi);
	struct wtfs_data_block * block = NULL;
	struct buffer_head * bh = NULL;
	uint64_t i, j, k, count, offset, next;
	int ret = -EINVAL;

	/* calculate how many entries we have counted, including null entries */
	count = ctx->pos / sizeof(struct wtfs_dentry);
	offset = ctx->pos % sizeof(struct wtfs_dentry);
	if (offset != 0) {
		wtfs_error("bad position %llu at %s:%lu\n", ctx->pos,
			dir_vi->i_sb->s_id, dir_vi->i_ino);
		goto error;
	}

	/* do iterate */
	next = info->first_block;
	i = 0; /* valid entry counter */
	j = 0; /* total entry counter, including null ones */
	while (next != 0) {
		if ((bh = sb_bread(vsb, next)) == NULL) {
			wtfs_error("unable to read the block %llu\n", next);
			goto error;
		}
		block = (struct wtfs_data_block *)bh->b_data;
		for (k = 0; k < WTFS_DENTRY_COUNT_PER_BLOCK; ++k) {
			if (block->entries[k].inode_no != 0) {
				if (j >= count && i < info->dir_entry_count) {
					wtfs_debug("emitting entry '%s' of inode %llu\n",
						block->entries[k].filename,
						wtfs64_to_cpu(block->entries[k].inode_no));

					if (dir_emit(ctx, block->entries[k].filename,
						strnlen(block->entries[k].filename, WTFS_FILENAME_MAX),
						wtfs64_to_cpu(block->entries[k].inode_no),
						DT_UNKNOWN) == 0) {
						brelse(bh);
						return 0;
					}
				}
				++i;
			}
			++j;
			ctx->pos += sizeof(struct wtfs_dentry);
		}
		next = wtfs64_to_cpu(block->next);
		brelse(bh);
	}
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}
