# wtfs
[![wtfs version](https://badge.fury.io/gh/chaosdefinition%2Fwtfs.svg)](http://badge.fury.io/gh/chaosdefinition%2Fwtfs)

适用于 Linux 的什么鬼文件系统

在 [GPLv3](https://github.com/chaosdefinition/wtfs/blob/master/LICENSE.txt) 的许可下发布

## 如何使用
首先编译整个项目，并将生成的模块加载至内核。
```Shell
$ cd wtfs && make
$ sudo insmod wtfs.ko
```

再创建一个目录（这里就叫 `~/wtfs-test`）作为挂载点。
```Shell
$ mkdir ~/wtfs-test
```

创建一个 4GB 大小的常规文件（任意大小都可以，但不要太小，这里就叫 `wtfs.data`）
作为设备，对它执行格式化操作并挂载到我们刚刚创建的目录下。
```Shell
$ dd bs=4096 count=1048576 if=/dev/zero of=wtfs.data
$ ./mkfs.wtfs -f wtfs.data
$ sudo mount -o loop wtfs.data ~/wtfs-test
```
或者你也可以直接使用一个块设备（这里就叫 `/dev/sda`），但**并不推荐**这么做。
```Shell
$ sudo ./mkfs.wtfs -f /dev/sda
$ sudo mount /dev/sda ~/wtfs-test
```

挂载完成后，你就可以在这个文件系统内做你任何想做的事了。

要卸载实例并从内核移除这个模块，执行下面的命令。
```Shell
$ sudo umount ~/wtfs-test
$ sudo rmmod wtfs
```

## wtfs 物理磁盘布局
版本 0.1.0

0 号块 | 1 号块 | 2 号块 | 3 号块 | 4 号块 | 5 号块… |
------ | ------ | ------ | ------ | ------ | ------- |
引导块 | 超级块 | 第 1 个索引节点表 | 第 1 个块位图 | 第 1 个索引节点位图 | 文件数据块…
 | | 第 2 个索引节点表 | 第 2 个块位图 | |
 | | … | … | |

* 每个块大小为 4096 字节。
* 每个索引节点大小为 64 字节。
* 每个目录项大小为 64 字节。
* 0 号块为引导块（目前暂不使用）。
* 1 号块为超级块。
* 2 号块为第 1 个索引节点表，也是索引节点表链的头。因为我们设计每个块的最后 8 字
节用来作为指向另一个块的指针，所以一个索引节点表最多能容纳 63 个索引节点。
* 3 号块为第 1 个块位图，也是块位图链的头。同样的原因，一个块位图最多能表示
 4088 * 8 个块。
* 4 号块为第 1 个索引节点位图，也是索引节点位图链的头。考虑到一个索引节点位图就
能表示 4088 * 8 个索引节点，对于一般使用完全能满足需求，所以目前整个文件系统只支
持一个索引节点位图。
* 从 5 号块开始为文件数据块。文件数据块同样设置最后 8 字节为指向另一个块的指针。
对于目录文件，每一个目录项也记录在文件数据块中。

## 联系我
如果有任何问题或建议，请发送邮件至 chaosdefinition@hotmail.com
