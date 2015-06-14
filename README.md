# wtfs
[![wtfs version](https://badge.fury.io/gh/chaosdefinition%2Fwtfs.svg)](http://badge.fury.io/gh/chaosdefinition%2Fwtfs)

What the fxck filesystem for Linux

Licensed under [GPLv3](https://github.com/chaosdefinition/wtfs/blob/master/LICENSE.txt)

中文说明见 [README.chs.md](https://github.com/chaosdefinition/wtfs/blob/master/README.chs.md)

## How to use it
First compile the whole project and load the module into kernel.
```Shell
$ cd wtfs && make
$ sudo insmod wtfs.ko
```

Then create a directory (here we name it `~/wtfs-test`) as the mount point.
```Shell
$ mkdir ~/wtfs-test
```

Create a 4GB (any size, but not too small, will fit) regular file (here we name
 it `wtfs.data`) as the device, do format on it and mount it on the directory we
 created.
```Shell
$ dd bs=4096 count=1048576 if=/dev/zero of=wtfs.data
$ ./mkfs.wtfs -f wtfs.data
$ sudo mount -o loop wtfs.data ~/wtfs-test
```
Or you can also use a block device (here we name it `/dev/sda`), but this is
 **not recommended**.
```Shell
$ sudo ./mkfs.wtfs -f /dev/sda
$ sudo mount /dev/sda ~/wtfs-test
```

After mount, you can do anything you want within this filesystem. Just have fun.

To unmount an instance and remove the module from kernel, do following.
```Shell
$ sudo umount ~/wtfs-test
$ sudo rmmod wtfs
```

## Physical disk layout of wtfs
Version 0.1.0

Block 0 | Block 1 | Block 2 | Block 3 | Block 4 | Block 5... |
------- | ------- | ------- | ------- | ------- | ---------- |
Boot loader block | Super block | 1st inode table | 1st block bitmap | 1st inode bitmap | Data block...
 | | 2nd inode table | 2nd block bitmap | |
 | | ... | ... | |

* Size of each block is 4096 bytes.
* Size of each inode is 64 bytes.
* Size of each dentry is 64 bytes.
* Block 0 is boot loader block (unused now).
* Block 1 is super block.
* Block 2 is the first inode table and the head of inode table chain. Because we
 use the last 8 bytes of a block as a pointer to another block, an inode table
 can contain a maximum of 63 inodes.
* Block 3 is the first block bitmap and the head of block bitmap chain. For the
 same reason, a block bitmap can state at most 4088 * 8 blocks.
* Block 4 is the first inode bitmap and the head of inode bitmap chain.
 Considering that an inode bitmap can state at most 4088 * 8 blocks, which is
 enough for normal use, therefore only one inode bitmap is supported now.
* Blocks from 5 are data blocks. Data blocks also have their last 8 bytes to be
 a pointer. For directories, the dentries are also recorded in data blocks.

## Contact me
Please send me email if you have any questions or suggestions: chaosdefinition@hotmail.com
