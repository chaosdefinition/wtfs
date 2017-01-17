/*
 * inode.c - implementation of wtfs inode operations.
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
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <wtfs/wtfs.h>
#include <wtfs/helper.h>

/* wtfs inode operations */
static int wtfs_create(struct inode * dir_vi, struct dentry * dentry,
		       umode_t mode, bool excl);
static struct dentry * wtfs_lookup(struct inode * dir_vi,
				   struct dentry * dentry, unsigned int flags);
static int wtfs_unlink(struct inode * dir_vi, struct dentry * dentry);
static int wtfs_mkdir(struct inode * dir_vi, struct dentry * dentry,
		      umode_t mode);
static int wtfs_rmdir(struct inode * dir_vi, struct dentry * dentry);
static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
		       struct inode * new_dir, struct dentry * new_dentry);
static int wtfs_setattr(struct dentry * dentry, struct iattr * attr);
static int wtfs_getattr(struct vfsmount * mnt, struct dentry * dentry,
			struct kstat * stat);
static int wtfs_symlink(struct inode * dir_vi, struct dentry * dentry,
			const char * symname);
static int wtfs_readlink(struct dentry * dentry, char __user * buf, int length);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
static void * wtfs_follow_link(struct dentry * dentry, struct nameidata * nd);
static void wtfs_put_link(struct dentry * dentry, struct nameidata * nd,
			  void * cookie);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
static const char * wtfs_follow_link(struct dentry * dentry, void ** cookie);
static void wtfs_put_link(struct inode * vi, void * cookie);
#else
static const char * wtfs_get_link(struct dentry * dentry, struct inode * vi,
				  struct delayed_call * done);
static void wtfs_put_link(void * cookie);
#endif

/* Inode operations for directory */
const struct inode_operations wtfs_dir_inops = {
	.create = wtfs_create,
	.lookup = wtfs_lookup,
	.unlink = wtfs_unlink,
	.mkdir = wtfs_mkdir,
	.rmdir = wtfs_rmdir,
	.rename = wtfs_rename,
	.symlink = wtfs_symlink,
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr,
};

/* Inode operations for regular file */
const struct inode_operations wtfs_file_inops = {
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr,
};

/* Inode operations for symbolic link */
const struct inode_operations wtfs_symlink_inops = {
	.readlink = wtfs_readlink,

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
	.follow_link = wtfs_follow_link,
	.put_link = wtfs_put_link,
#else
	.get_link = wtfs_get_link,
#endif

	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr,
};

/*****************************************************************************
 * Implementation of wtfs inode operations ***********************************
 *****************************************************************************/

/*
 * Routine called to create a new regular file.
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the file to create
 * @mode: file mode
 * @excl: whether to fail if the file exists (ignored here)
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_create(struct inode * dir_vi, struct dentry * dentry,
		       umode_t mode, bool excl)
{
	struct inode * vi = NULL;

	wtfs_debug("create called, dir inode %lu, file '%s'\n", dir_vi->i_ino,
		   dentry->d_name.name);

	/* Create a new inode */
	vi = wtfs_new_inode(dir_vi, mode | S_IFREG, NULL, 0);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}

	/* Add a dentry to its parent directory */
	wtfs_add_dentry(dir_vi, vi->i_ino, dentry->d_name.name,
			dentry->d_name.len);
	inode_inc_link_count(vi);

	d_instantiate(dentry, vi);

	return 0;
}
