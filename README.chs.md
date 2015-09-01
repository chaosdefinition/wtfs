# wtfs
[![wtfs version](https://badge.fury.io/gh/chaosdefinition%2Fwtfs.svg)](http://badge.fury.io/gh/chaosdefinition%2Fwtfs)
[![Build status](https://travis-ci.org/chaosdefinition/wtfs.svg)](https://travis-ci.org/chaosdefinition/wtfs)

适用于 Linux 的什么鬼文件系统

在 [GPLv3](https://github.com/chaosdefinition/wtfs/blob/master/LICENSE.txt) 的许可下发布

## 如何使用
在编译之前，你需要安装好适当版本的构建要素和 Linux 内核头文件，以便能够构建内核模块（gcc 版本 >= 4.6 且 linux 版本 >= 3.11）。另外，构建 `mkfs.wtfs` 和 `statfs.wtfs` 现在需要 uuid 头文件了。
```Shell
# for Debian derivatives
$ sudo apt-get install build-essential linux-headers-`uname -r` uuid-dev
# for Redhat derivatives
$ sudo yum groupinstall "Development Tools"
$ sudo yum install kernel-devel kernel-headers libuuid-devel
```

满足构建前提条件后, 克隆这个版本库。
```Shell
$ git clone https://github.com/chaosdefinition/wtfs.git
```

首先编译整个项目，并将生成的模块加载至内核。
```Shell
$ cd wtfs && make
$ sudo insmod wtfs.ko
```

再创建一个目录（这里就叫 `~/wtfs-test`）作为挂载点。
```Shell
$ mkdir ~/wtfs-test
```

使用一个块设备（这里就叫 `/dev/sda`），对它执行格式化操作并挂载到我们刚刚创建的目录下。
```Shell
$ sudo ./mkfs.wtfs -f /dev/sda
$ sudo mount /dev/sda ~/wtfs-test
```
或者你也可以使用一个回环设备，将一个常规文件当做块设备来访问，但**并不推荐**这么做（因为对回环设备调用 `mark_buffer_dirty` 可能会阻塞，导致写操作相当慢）。创建一个 4GB 大小的常规文件（任意大小都可以，但不要太小，这里就叫 `wtfs.img`），执行格式化操作并挂载。
```Shell
$ dd bs=4096 count=1048576 if=/dev/zero of=wtfs.img
$ ./mkfs.wtfs -f wtfs.img
$ sudo mount -o loop wtfs.img ~/wtfs-test
```

挂载完成后，你就可以在这个文件系统内做任何你想做的事了。

要卸载实例并从内核移除这个模块，执行下面的命令。
```Shell
$ sudo umount ~/wtfs-test
$ sudo rmmod wtfs
```

## 如何调试
除了将命令 `make` 替换为 `make debug` 之外，其他均按照上述步骤来做，这样生成的二进制文件就会包含调试符号信息，而且宏 `DEBUG` 也会被定义好。接下来你就可以使用外部的调试器——比如 gdb ——来调试 `mkfs.wtfs` 和 `statfs.wtfs` 了。

然而，调试模块的方法就比较原始了，因为到目前为止我没有使用任何内核调试器。我一直都是看模块的输出日志来调试的……所以如果你有任何更加高级的方法，那就用吧。

## wtfs 物理磁盘布局
版本 0.4.0

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
* 2 号块为第 1 个索引节点表，也是索引节点表链的头。因为我们设计每个块的最后 8 字节用来作为指向另一个块的指针，所以一个索引节点表最多能容纳 63 个索引节点。
* 3 号块为第 1 个块位图，也是块位图链的头。同样的原因，一个块位图最多能表示 4088 * 8 个块。
* 4 号块为第 1 个索引节点位图，也是索引节点位图链的头。考虑到一个索引节点位图就能表示 4088 * 8 个索引节点，对于一般使用完全能满足需求，所以目前整个文件系统只支持一个索引节点位图。
* 从 5 号块开始为文件数据块。文件数据块同样设置最后 8 字节为指向另一个块的指针。对于目录文件，每一个目录项也记录在文件数据块中。

## 联系我
如果有任何问题或建议，请发送邮件至 chaosdefinition@hotmail.com
