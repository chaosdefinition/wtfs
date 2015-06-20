/*
 * super.c - implementation of wtfs super block operations.
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/slab.h>

#include "wtfs.h"

/* module information */
MODULE_ALIAS_FS("wtfs");
MODULE_AUTHOR("Chaos Shen");
MODULE_DESCRIPTION("What the fxck filesystem");
MODULE_LICENSE("GPL");

/* declaration of module entry & exit points */
static int __init wtfs_init(void);
static void __exit wtfs_exit(void);

module_init(wtfs_init);
module_exit(wtfs_exit);

/* declaration of mount & unmount operations */
static struct dentry * wtfs_mount(struct file_system_type * fs_type, int flags,
	const char * dev_name, void * data);
static void wtfs_kill_sb(struct super_block * vsb);
static int wtfs_fill_super(struct super_block * vsb, void * data, int silent);

static struct file_system_type wtfs_type = {
	.owner = THIS_MODULE,
	.name = "wtfs",
	.mount = wtfs_mount,
	.kill_sb = wtfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV
};

/* declaration of super block operations */
static struct inode * wtfs_alloc_inode(struct super_block * vsb);
static void wtfs_destroy_inode(struct inode * vi);
static int wtfs_write_inode(struct inode * vi,
	struct writeback_control * wbc);
static void wtfs_evict_inode(struct inode * vi);
static void wtfs_put_super(struct super_block * vsb);
static int wtfs_statfs(struct dentry * dentry, struct kstatfs * buf);

const struct super_operations wtfs_super_ops = {
	.alloc_inode = wtfs_alloc_inode,
	.destroy_inode = wtfs_destroy_inode,
	.write_inode = wtfs_write_inode,
	.evict_inode = wtfs_evict_inode,
	.put_super = wtfs_put_super,
	.statfs = wtfs_statfs
};

/********************* implementation of alloc_inode **************************/

/* a slab memory that contains wtfs_inode_info structure */
static struct kmem_cache * wtfs_inode_cachep = NULL;

/*
 * routine called to allocate memory for struct inode and initialize it
 *
 * @vsb: the VFS super block structure
 *
 * return: a pointer to the VFS inode structure
 */
static struct inode * wtfs_alloc_inode(struct super_block * vsb)
{
	struct wtfs_inode_info * info = NULL;

	wtfs_debug("alloc_inode called\n");

	info = (struct wtfs_inode_info *)kmem_cache_alloc(wtfs_inode_cachep,
		GFP_KERNEL);
	if (info == NULL) {
		return NULL;
	} else {
		return &(info->vfs_inode);
	}
}

/********************* implementation of destroy_inode ************************/

/*
 * callback function called to release slab memory for wtfs_inode_info
 *
 * @head: callback structure for use with RCU and task_work
 */
static void wtfs_i_callback(struct rcu_head * head)
{
	struct inode * inode = container_of(head, struct inode, i_rcu);

	kmem_cache_free(wtfs_inode_cachep, WTFS_INODE_INFO(inode));
}

/*
 * routine called to release resources allocated by alloc_inode
 *
 * @vi: the VFS inode structure
 */
static void wtfs_destroy_inode(struct inode * vi)
{
	wtfs_debug("destroy_inode called, inode %lu\n", vi->i_ino);

	call_rcu(&(vi->i_rcu), wtfs_i_callback);
}

/********************* implementation of write_inode **************************/

/*
 * routine called when the VFS needs to write an inode to disk
 *
 * @vi: the VFS inode structure
 * @wbc: a control structure which tells the writeback code what to do
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_write_inode(struct inode * vi, struct writeback_control * wbc)
{
	struct wtfs_inode_info * info = WTFS_INODE_INFO(vi);
	struct wtfs_inode * inode = NULL;
	struct buffer_head * bh = NULL;
	int ret = -EINVAL;

	wtfs_debug("write_inode called, inode %lu\n", vi->i_ino);

	/*
	 * get the physical inode
	 * note that the buffer_head is also pointing to the inode table containing
	 * this inode, so we can directly write back this buffer_head later
	 */
	inode = wtfs_get_inode(vi->i_sb, vi->i_ino, &bh);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto error;
	}

	/* write to the physical inode (still in memory) */
	inode->inode_no = cpu_to_wtfs64(vi->i_ino);
	inode->mode = cpu_to_wtfs32(vi->i_mode);
	inode->uid = cpu_to_wtfs16(i_uid_read(vi));
	inode->gid = cpu_to_wtfs16(i_gid_read(vi));
	inode->atime = cpu_to_wtfs64(vi->i_atime.tv_sec);
	inode->ctime = cpu_to_wtfs64(vi->i_ctime.tv_sec);
	inode->mtime = cpu_to_wtfs64(vi->i_mtime.tv_sec);
	inode->block_count = cpu_to_wtfs64(vi->i_blocks);
	inode->first_block = cpu_to_wtfs64(info->first_block);
	switch (vi->i_mode & S_IFMT) {
	case S_IFDIR:
		inode->dir_entry_count = cpu_to_wtfs64(info->dir_entry_count);
		break;

	case S_IFREG: case S_IFLNK:
		inode->file_size = cpu_to_wtfs64(i_size_read(vi));
		break;

	default:
		wtfs_error("special file type not supported\n");
		goto error;
	}

	/* actually do write back */
	mark_buffer_dirty(bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			wtfs_error("inode %lu sync failed at %s", vi->i_ino,
				vi->i_sb->s_id);
			ret = -EIO;
			goto error;
		}
	}

	/* release the buffer_head read in wtfs_get_inode */
	brelse(bh);
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	return ret;
}

/********************* implementation of evict_inode **************************/

/*
 * routine called when the inode is evicted
 *
 * @vi: the VFS inode structure
 */
static void wtfs_evict_inode(struct inode * vi)
{
	wtfs_debug("evict_inode called, inode %lu\n", vi->i_ino);

	truncate_inode_pages(&(vi->i_data), 0);
	invalidate_inode_buffers(vi);
	clear_inode(vi);
}

/********************* implementation of put_super ****************************/

/*
 * routine called when the VFS wishes to free the superblock
 *
 * @vsb: the VFS super block structure
 */
static void wtfs_put_super(struct super_block * vsb)
{
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);

	wtfs_debug("put_super called\n");

	if (sbi != NULL) {
		kfree(sbi);
		vsb->s_fs_info = NULL;
	}
}

/********************* implementation of statfs *******************************/

/*
 * routine called when the VFS needs to get statistics of this wtfs instance
 *
 * @dentry: a dir entry within wtfs
 * @buf: buffer to store statistics
 *
 * return: 0
 */
static int wtfs_statfs(struct dentry * dentry, struct kstatfs * buf)
{
	struct super_block * vsb = dentry->d_sb;
	struct wtfs_sb_info * sbi = WTFS_SB_INFO(vsb);
	u64 id = huge_encode_dev(vsb->s_bdev->bd_dev);

	/* wtfs magic number */
	buf->f_type = WTFS_MAGIC;

	/* block size */
	buf->f_bsize = vsb->s_blocksize;

	/* block count */
	buf->f_blocks = sbi->block_count;

	/*
	 * free block & available block count
	 * they should be the same
	 */
	buf->f_bfree = sbi->free_block_count;
	buf->f_bavail = sbi->free_block_count;

	/* inode count */
	buf->f_files = sbi->inode_count;

	/* free inode count */
	buf->f_ffree = WTFS_INODE_MAX - sbi->inode_count;

	/* high & low 32 bits of device id */
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	/* max length of filename */
	buf->f_namelen = WTFS_FILENAME_MAX;

	return 0;
}

/********************* implementation of fill_super ***************************/

/*
 * routine called to fill information of wtfs into the VFS super block
 *
 * @vsb: the VFS super block structure
 * @data: arbitrary mount options
 * @silent: whether or not to be silent on error
 *
 * return: 0 on success, error code otherwise
 */
static int wtfs_fill_super(struct super_block * vsb, void * data, int silent)
{
	struct wtfs_super_block * sb = NULL;
	struct wtfs_sb_info * sbi = NULL;
	struct buffer_head * bh = NULL;
	struct inode * root_inode = NULL;
	int ret = -EINVAL;

	wtfs_debug("fill_super called\n");

	/* set block size */
	if (!sb_set_blocksize(vsb, WTFS_BLOCK_SIZE)) {
		wtfs_error("block size of %d bytes not supported\n", WTFS_BLOCK_SIZE);
		goto error;
	}

	/* read the whole super block into buffer */
	if ((bh = sb_bread(vsb, WTFS_RB_SUPER)) == NULL) {
		wtfs_error("unable to read the super block\n");
		goto error;
	}

	/* check if the magic number mismatches */
	sb = (struct wtfs_super_block *)bh->b_data;
	if (wtfs64_to_cpu(sb->magic) != WTFS_MAGIC) {
		wtfs_error("magic number mismatch: 0x%llx\n", wtfs64_to_cpu(sb->magic));
		ret = -EPERM;
		goto error;
	}

	/* do version relevant stuffs */
	if (wtfs64_to_cpu(sb->version) != WTFS_VERSION) {
		/* ... */
	}

	/* allocate a memory for sb_info */
	sbi = (struct wtfs_sb_info *)kzalloc(sizeof(struct wtfs_sb_info),
		GFP_KERNEL);
	if (sbi == NULL) {
		wtfs_error("memory allocate for sb_info failed\n");
		ret = -ENOMEM;
		goto error;
	}

	/* fill sb_info */
	sbi->version = wtfs64_to_cpu(sb->version);
	sbi->magic = WTFS_MAGIC;
	sbi->block_size = wtfs64_to_cpu(sb->block_size);
	sbi->block_count = wtfs64_to_cpu(sb->block_count);
	sbi->inode_table_first = wtfs64_to_cpu(sb->inode_table_first);
	sbi->inode_table_count = wtfs64_to_cpu(sb->inode_table_count);
	sbi->block_bitmap_first = wtfs64_to_cpu(sb->block_bitmap_first);
	sbi->block_bitmap_count = wtfs64_to_cpu(sb->block_bitmap_count);
	sbi->inode_bitmap_first = wtfs64_to_cpu(sb->inode_bitmap_first);
	sbi->inode_bitmap_count = wtfs64_to_cpu(sb->inode_bitmap_count);
	sbi->inode_count = wtfs64_to_cpu(sb->inode_count);
	sbi->free_block_count = wtfs64_to_cpu(sb->free_block_count);

	/* fill the VFS super block */
	vsb->s_magic = sbi->magic;
	vsb->s_fs_info = sbi;
	vsb->s_op = &wtfs_super_ops;

	/* get the root inode from inode cache */
	root_inode = wtfs_iget(vsb, WTFS_ROOT_INO);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		goto error;
	}

	/* make root dentry */
	if ((vsb->s_root = d_make_root(root_inode)) == NULL) {
		wtfs_error("make root dentry failed\n");
		goto error;
	}

	brelse(bh);
	return 0;

error:
	if (bh != NULL) {
		brelse(bh);
	}
	if (sbi != NULL) {
		kfree(sbi);
	}
	return ret;
}

/********************* implementation of mount ********************************/

/*
 * routine called when an instance of wtfs should be mounted
 *
 * @type: describes the filesystem
 * @flags: mount flags
 * @dev_name: the device name we are mounting
 * @data: arbitrary mount options
 *
 * return: the root dentry
 */
static struct dentry * wtfs_mount(struct file_system_type * fs_type, int flags,
	const char * dev_name, void * data)
{
	struct dentry * entry = NULL;

	wtfs_debug("mount called\n");

	entry = mount_bdev(fs_type, flags, dev_name, data, wtfs_fill_super);
	if (IS_ERR(entry)) {
		wtfs_error("wtfs mount failed at device %s\n", dev_name);
	} else {
		wtfs_info("wtfs mounted at device %s\n", dev_name);
	}

	return entry;
}

/********************* implementation of kill_sb ******************************/

/*
 * routine called when an instance of wtfs should be shut down
 *
 * @vsb: the VFS super block structure
 */
static void wtfs_kill_sb(struct super_block * vsb)
{
	wtfs_debug("kill_sb called\n");

	/* simply call kill_block_super */
	kill_block_super(vsb);
	wtfs_info("wtfs unmounted\n");
}

/********************* implementation of init *********************************/

/*
 * callback function called to initialize VFS inode when creating inode cache
 *
 * @data: a pointer to inode cache
 */
static void init_once(void * data)
{
	struct wtfs_inode_info * info = (struct wtfs_inode_info *)data;

	inode_init_once(&(info->vfs_inode));
}

/*
 * create and initialize inode cache on module initialization
 *
 * return: 0 on success, error code otherwise
 */
static int __init create_inode_cache(void)
{
	wtfs_inode_cachep = kmem_cache_create("wtfs_inode_cache",
		sizeof(struct wtfs_inode_info), 0,
		SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, init_once);
	if (wtfs_inode_cachep == NULL) {
		return -ENOMEM;
	}
	return 0;
}

/*
 * destroy inode cache
 */
static void destroy_inode_cache(void)
{
	rcu_barrier();
	kmem_cache_destroy(wtfs_inode_cachep);
}

/*
 * routine called when wtfs module should be loaded
 *
 * return: 0 on success, error code otherwise
 */
static int __init wtfs_init(void)
{
	int ret = -EINVAL;

	wtfs_debug("init called\n");

	if ((ret = create_inode_cache()) != 0) {
		goto error;
	}

	/* register wtfs */
	if ((ret = register_filesystem(&wtfs_type)) == 0) {
		wtfs_info("wtfs registered\n");
	} else {
		wtfs_error("wtfs register failed: error %d\n", ret);
		goto error;
	}

	return 0;

error:
	if (wtfs_inode_cachep != NULL) {
		destroy_inode_cache();
	}
	return ret;
}

/********************* implementation of exit *********************************/

/*
 * routine called when wtfs module should be unloaded
 */
static void __exit wtfs_exit(void)
{
	wtfs_debug("exit called\n");

	/* unregister wtfs */
	unregister_filesystem(&wtfs_type);

	/* destroy inode cache */
	destroy_inode_cache();
}
