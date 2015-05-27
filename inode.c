/*
 * inode.c - implementation of wtfs inode operations.
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
#include <linux/mount.h>

#include "wtfs.h"

/* declaration of inode operations */
static int wtfs_create(struct inode * dir_vi, struct dentry * dentry,
	umode_t mode, bool excl);
static struct dentry * wtfs_lookup(struct inode * dir_vi, struct dentry * dentry,
	unsigned int flags);
static int wtfs_unlink(struct inode * dir_vi, struct dentry * dentry);
static int wtfs_mkdir(struct inode * dir_vi, struct dentry * dentry, umode_t mode);
static int wtfs_rmdir(struct inode * dir_vi, struct dentry * dentry);
static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry);
static int wtfs_setattr(struct dentry * dentry, struct iattr * attr);
static int wtfs_getattr(struct vfsmount * mnt, struct dentry * dentry,
	struct kstat * stat);

const struct inode_operations wtfs_dir_inops = {
	.create = wtfs_create,
	.lookup = wtfs_lookup,
	.unlink = wtfs_unlink,
	.mkdir = wtfs_mkdir,
	.rmdir = wtfs_rmdir,
	.rename = wtfs_rename,
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr
};

const struct inode_operations wtfs_file_inops = {
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr
};

/********************* implementation of create *******************************/

static int wtfs_create(struct inode * dir_vi, struct dentry * dentry,
	umode_t mode, bool excl)
{
	struct inode * vi = NULL;

	vi = wtfs_new_inode(dir_vi, mode | S_IFREG);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}
	wtfs_add_entry(dir_vi, vi->i_ino, dentry->d_name.name, dentry->d_name.len);
	d_instantiate(dentry, vi);

	return 0;
}

/********************* implementation of lookup *******************************/

static struct dentry * wtfs_lookup(struct inode * dir_vi, struct dentry * dentry,
	unsigned int flags)
{
	struct inode * vi = NULL;
	uint64_t inode_no;

	if ((inode_no = wtfs_find_inode(dir_vi, dentry)) != 0) {
		vi = wtfs_iget(dir_vi->i_sb, inode_no);
		if (IS_ERR(vi)) {
			return ERR_PTR(vi);
		}
	}

	d_add(dentry, vi);
	return NULL;
}

/********************* implementation of unlink *******************************/

static int wtfs_unlink(struct inode * dir_vi, struct dentry * dentry)
{
	struct inode * vi = NULL;
	uint64_t inode_no;
	int ret = -ENOENT;

	/* find in directory entries */
	if ((inode_no = wtfs_find_inode(dir_vi, dentry)) == 0) {
		return ret;
	}

	/* delete entry and inode */
	if ((ret = wtfs_delete_entry(dir_vi, inode_no)) < 0) {
		return ret;
	}
	wtfs_delete_inode(dentry->d_inode);

	return 0;
}

/********************* implementation of mkdir ********************************/

static int wtfs_mkdir(struct inode * dir_vi, struct dentry * dentry,
	umode_t mode)
{
	struct inode * vi = NULL;

	vi = wtfs_new_inode(dir_vi, mode | S_IFDIR);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}
	wtfs_add_entry(dir_vi, vi->i_ino, dentry->d_name.name, dentry->d_name.len);
	wtfs_add_entry(vi, vi->i_ino, ".", 1);
	wtfs_add_entry(vi, dir_vi->i_ino, "..", 2);
	d_instantiate(dentry, vi);

	return 0;
}

/********************* implementation of rmdir ********************************/

static int wtfs_rmdir(struct inode * dir_vi, struct dentry * dentry)
{
	struct wtfs_inode_info * info = WTFS_INODE_INFO(dentry->d_inode);
	int ret = -ENOTEMPTY;

	if (info->dir_entry_count == 2) {
		return wtfs_unlink(dir_vi, dentry);
	}
	return ret;
}

/********************* implementation of rename *******************************/

static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry)
{
	return -EPERM;
}

/********************* implementation of setattr ******************************/

/*
 * routine called by the VFS to set attributes for a file
 *
 * @dentry: dentry of the file
 * @attr: attributes to set
 *
 * return: status
 */
static int wtfs_setattr(struct dentry * dentry, struct iattr * attr)
{
	struct inode * vi = dentry->d_inode;
	int ret;

	if ((ret = inode_change_ok(vi, attr)) < 0) {
		return ret;
	}
	setattr_copy(vi, attr);
	mark_inode_dirty(vi);

	return 0;
}

/********************* implementation of getattr ******************************/

/*
 * routine called by the VFS to get attributes of a file
 *
 * @mnt: mount point
 * @dentry: dentry of the file
 * @stat: buffer to hold the attributes
 *
 * return: 0
 */
static int wtfs_getattr(struct vfsmount * mnt, struct dentry * dentry,
	struct kstat * stat)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(dentry->d_sb);

	generic_fillattr(dentry->d_inode, stat);
	stat->blksize = sbi->block_size;

	return 0;
}
