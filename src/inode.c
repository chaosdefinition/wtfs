/*
 * inode.c - implementation of wtfs inode operations.
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
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "wtfs.h"

/* declaration of inode operations */
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
static void * wtfs_follow_link(struct dentry * dentry, struct nameidata * nd);
static void wtfs_put_link(struct dentry * dentry, struct nameidata * nd,
	void * cookie);

/* inode operations for directory */
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

/* inode operations for regular file */
const struct inode_operations wtfs_file_inops = {
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr,
};

/* inode operations for symbolic link */
const struct inode_operations wtfs_symlink_inops = {
	.readlink = wtfs_readlink,
	.follow_link = wtfs_follow_link,
	.put_link = wtfs_put_link,
	.setattr = wtfs_setattr,
	.getattr = wtfs_getattr,
};

/********************* implementation of create *******************************/

/*
 * routine called to create a new regular file
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

	/* create a new inode */
	vi = wtfs_new_inode(dir_vi, mode | S_IFREG, NULL, 0);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}

	/* add an entry to its parent directory */
	wtfs_add_entry(dir_vi, vi->i_ino, dentry->d_name.name,
		dentry->d_name.len);

	d_instantiate(dentry, vi);

	return 0;
}

/********************* implementation of lookup *******************************/

/*
 * routine called when the VFS needs to look up an inode in a parent directory
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the file to look up
 * @flags: ignored here
 *
 * return: NULL on success, error code otherwise
 */
static struct dentry * wtfs_lookup(struct inode * dir_vi,
	struct dentry * dentry, unsigned int flags)
{
	struct inode * vi = NULL;
	uint64_t inode_no;

	wtfs_debug("lookup called, dir inode %lu, file '%s'\n", dir_vi->i_ino,
		dentry->d_name.name);

	/* find inode by name */
	if ((inode_no = wtfs_find_inode(dir_vi, dentry)) != 0) {
		vi = wtfs_iget(dir_vi->i_sb, inode_no);
		if (IS_ERR(vi)) {
			return ERR_CAST(vi);
		}
	}

	/* we should call d_add() no matter if we find the inode */
	d_add(dentry, vi);
	return NULL;
}

/********************* implementation of unlink *******************************/

/*
 * routine called to delete an inode
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the file to delete
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_unlink(struct inode * dir_vi, struct dentry * dentry)
{
	uint64_t inode_no;
	int ret = -ENOENT;

	wtfs_debug("unlink called, file '%s' of inode %lu\n",
		dentry->d_name.name, dentry->d_inode->i_ino);

	/* find inode in directory entries */
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

/*
 * routine called to create a new directory
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the directory to create
 * @mode: file mode
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_mkdir(struct inode * dir_vi, struct dentry * dentry,
	umode_t mode)
{
	struct inode * vi = NULL;

	wtfs_debug("mkdir called, parent inode %lu, dir to create '%s', "
		"mode 0%o\n", dir_vi->i_ino, dentry->d_name.name, mode);

	/* create a new inode */
	vi = wtfs_new_inode(dir_vi, mode | S_IFDIR, NULL, 0);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}

	/* add an entry to its parent directory */
	wtfs_add_entry(dir_vi, vi->i_ino, dentry->d_name.name,
		dentry->d_name.len);

	/* add two entries of '.' and '..' to itself */
	wtfs_add_entry(vi, vi->i_ino, ".", 1);
	wtfs_add_entry(vi, dir_vi->i_ino, "..", 2);

	d_instantiate(dentry, vi);

	return 0;
}

/********************* implementation of rmdir ********************************/

/*
 * routine called to delete an empty directory
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the directory to delete
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_rmdir(struct inode * dir_vi, struct dentry * dentry)
{
	struct wtfs_inode_info * info = WTFS_INODE_INFO(dentry->d_inode);
	int ret = -ENOTEMPTY;

	wtfs_debug("rmdir called, dir '%s' of inode %lu "
		"with entry count %llu\n", dentry->d_name.name,
		dentry->d_inode->i_ino, info->dir_entry_count);

	/* call unlink() if it contains only 2 entries ('.' and '..') */
	if (info->dir_entry_count == 2) {
		return wtfs_unlink(dir_vi, dentry);
	}
	return ret;
}

/********************* implementation of rename *******************************/

/*
 * routine called to rename an inode
 *
 * @old_dir: the VFS inode of the old parent directory
 * @old_dentry: the old dentry of the inode
 * @new_dir: the VFS inode of the new parent directory
 * @new_dentry: the new dentry of the inode
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_rename(struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode * old_vi = old_dentry->d_inode;
	struct inode * new_vi = new_dentry->d_inode;
	int ret = -EINVAL;

	wtfs_debug("rename called to move '%s' in dir of inode %lu to "
		"'%s' in dir of inode %lu\n", old_dentry->d_name.name,
		old_dir->i_ino, new_dentry->d_name.name, new_dir->i_ino);

	/* destination entry exists, remove it */
	if (new_vi != NULL) {
		switch (new_vi->i_mode & S_IFMT) {
		case S_IFDIR:
			if ((ret = wtfs_rmdir(new_dir, new_dentry)) < 0) {
				return ret;
			}
			break;

		case S_IFREG:
		case S_IFLNK:
			if ((ret = wtfs_unlink(new_dir, new_dentry)) < 0) {
				return ret;
			}
			break;

		default:
			wtfs_error("special file type not supported\n");
			return ret;
		}
	}

	/* remove entry in old directory */
	if ((ret = wtfs_delete_entry(old_dir, old_vi->i_ino)) < 0) {
		return ret;
	}

	/* add a new entry in new directory */
	wtfs_add_entry(new_dir, old_vi->i_ino, new_dentry->d_name.name,
		new_dentry->d_name.len);

	return 0;
}

/********************* implementation of setattr ******************************/

/*
 * routine called by the VFS to set attributes for a file
 *
 * @dentry: dentry of the file
 * @attr: attributes to set
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_setattr(struct dentry * dentry, struct iattr * attr)
{
	struct inode * vi = dentry->d_inode;
	int ret;

	wtfs_debug("setattr called, file '%s' of inode %lu\n",
		dentry->d_name.name, dentry->d_inode->i_ino);

	/* check if the attributes can be set */
	if ((ret = inode_change_ok(vi, attr)) < 0) {
		return ret;
	}

	/* do set attributes */
	setattr_copy(vi, attr);
	if (attr->ia_valid & ATTR_SIZE) {
		i_size_write(vi, attr->ia_size);
	}
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

	wtfs_debug("getattr called, file '%s' of inode %lu\n",
		dentry->d_name.name, dentry->d_inode->i_ino);

	/*
	 * simply call generic_fillattr() because the VFS inode already contains
	 * most attributes
	 */
	generic_fillattr(dentry->d_inode, stat);

	/* the only thing we do is set block size */
	stat->blksize = sbi->block_size;

	return 0;
}

/********************* implementation of symlink ******************************/

/*
 * routine called to create a new symbolic link file
 *
 * @dir_vi: the VFS inode of the parent directory
 * @dentry: dentry of the symlink file to create
 * @symname: filename linking to
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_symlink(struct inode * dir_vi, struct dentry * dentry,
	const char * symname)
{
	struct inode * vi = NULL;
	size_t length;

	wtfs_debug("symlink called, dir inode %lu, file '%s' linking to '%s'\n",
		dir_vi->i_ino, dentry->d_name.name, symname);

	/* check filename length */
	length = strnlen(symname, WTFS_SYMLINK_MAX);
	if (length == WTFS_SYMLINK_MAX) {
		return -ENAMETOOLONG;
	}

	/* create a new inode */
	vi = wtfs_new_inode(dir_vi, S_IFLNK | S_IRWXUGO, symname, length);
	if (IS_ERR(vi)) {
		return PTR_ERR(vi);
	}

	/* add an entry to its parent directory */
	wtfs_add_entry(dir_vi, vi->i_ino, dentry->d_name.name,
		dentry->d_name.len);

	d_instantiate(dentry, vi);

	return 0;
}

/********************* implementation of readlink *****************************/

/*
 * routine called to read the content of a symbolic link
 *
 * @dentry: dentry of the symlink file to read
 * @buf: the userspace buffer to hold the symlink content
 * @length: length of the buffer in byte
 *
 * return: length of the symlink content on success, error code otherwise
 */
static int wtfs_readlink(struct dentry * dentry, char __user * buf, int length)
{
	int ret;

	wtfs_debug("readlink called, file '%s' of inode %lu\n",
		dentry->d_name.name, dentry->d_inode->i_ino);

	ret = generic_readlink(dentry, buf, length);
	if (IS_ERR_VALUE(ret)) {
		return ret;
	}

	wtfs_debug("'%s' -> '%s' of length %d\n", dentry->d_name.name, buf, ret);
	return ret;
}

/********************* implementation of follow_link **************************/

/*
 * routine called by the VFS to follow a symbolic link to the inode it points to
 *
 * @dentry: dentry of the symlink file to follow
 * @nd: nameidata structure to record the symlink path and depth
 *
 * return: buffer_head of the symlink block
 */
static void * wtfs_follow_link(struct dentry * dentry, struct nameidata * nd)
{
	struct inode * vi = dentry->d_inode;
	struct super_block * vsb = vi->i_sb;
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_symlink_block * symlink = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	wtfs_debug("follow_link called, file '%s' of inode %lu\n",
		dentry->d_name.name, vi->i_ino);

	/* read symlink block */
	if ((bh = sb_bread(vsb, info->first_block)) == NULL) {
		wtfs_error("unable to read the block %llu\n",
			info->first_block);
		ret = -EIO;
		goto error;
	}
	symlink = (struct wtfs_symlink_block *)bh->b_data;

	/* set link */
	nd_set_link(nd, symlink->path);

	return bh;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ERR_PTR(ret);
}

/********************* implementation of put_link *****************************/

/*
 * routine called by the VFS to release resources allocated by follow_link()
 *
 * @dentry: dentry of the symlink file to follow
 * @nd: nameidata structure to record the symlink path and depth
 * @cookie: buffer_head of the symlink block to release
 */
static void wtfs_put_link(struct dentry * dentry, struct nameidata * nd,
	void * cookie)
{
	wtfs_debug("put_link called\n");

	if (cookie != NULL) {
		brelse((struct buffer_head *)cookie);
	}
}
