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

#include "wtfs.h"

/* declaration of inode operations */
static int wtfs_create(struct inode * vfs_inode, struct dentry * dentry,
	umode_t mode, bool excl);
static struct dentry * wtfs_lookup(struct inode * vfs_inode,
	struct dentry * dentry, unsigned int flags);
static int wtfs_mkdir(struct inode * vfs_inode, struct dentry * dentry,
	umode_t mode);
static int wtfs_rmdir(struct inode * vfs_inode, struct dentry * dentry);
static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry);
static int wtfs_setattr(struct dentry * dentry, struct iattr * attr);
static int wtfs_getattr(struct vfsmount * mnt, struct dentry * dentry,
	struct kstat * stat);

const struct inode_operations wtfs_dir_inops = {
	.create = wtfs_create,
	.lookup = wtfs_lookup,
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

static int wtfs_create(struct inode * vfs_inode, struct dentry * dentry,
	umode_t mode, bool excl)
{
	return -EPERM;
}

/********************* implementation of lookup *******************************/

static struct dentry * wtfs_lookup(struct inode * vfs_inode,
	struct dentry * dentry, unsigned int flags)
{
	return NULL;
}

/********************* implementation of mkdir ********************************/

static int wtfs_mkdir(struct inode * vfs_inode, struct dentry * dentry,
	umode_t mode)
{
	return -EPERM;
}

/********************* implementation of rmdir ********************************/

static int wtfs_rmdir(struct inode * vfs_inode, struct dentry * dentry)
{
	return -EPERM;
}

/********************* implementation of rename *******************************/

static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry)
{
	return -EPERM;
}

/********************* implementation of setattr ******************************/

static int wtfs_setattr(struct dentry * dentry, struct iattr * attr)
{
	return -EPERM;
}

/********************* implementation of getattr ******************************/

static int wtfs_getattr(struct vfsmount * mnt, struct dentry * dentry,
	struct kstat * stat)
{
	return -EPERM;
}
