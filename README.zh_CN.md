# wtfs
[![wtfs 版本](https://badge.fury.io/gh/chaosdefinition%2Fwtfs.svg)](http://badge.fury.io/gh/chaosdefinition%2Fwtfs)
[![构建状态](https://travis-ci.org/chaosdefinition/wtfs.svg)](https://travis-ci.org/chaosdefinition/wtfs)

适用于 Linux 的什么鬼文件系统

在 [GPLv3](LICENSE.txt) 的许可下发布

## 如何使用
在编译之前，你需要安装好适当版本的构建要素和 Linux 内核头文件，以便能够构建内核模块（gcc 版本 >= 4.6 且 linux 版本 >= 3.11）。另外，构建 `mkfs.wtfs` 和 `statfs.wtfs` 现在需要 uuid 和 libmount 头文件了。
```Shell
# 仅适用于 Debian 衍生系
$ sudo apt-get install build-essential linux-headers-`uname -r`
$ sudo apt-get install uuid-dev libmount-dev
```

满足构建前提条件后, 克隆这个版本库。
```Shell
$ git clone https://github.com/chaosdefinition/wtfs.git
```

首先编译整个项目，并将生成的模块加载至内核。
```Shell
$ cd wtfs && make
$ sudo insmod build/wtfs.ko
```

再创建一个目录（这里就叫 `~/wtfs-test`）作为挂载点。
```Shell
$ mkdir ~/wtfs-test
```

使用一个块设备（这里就叫 `/dev/sda`），对它执行格式化操作并挂载到我们刚刚创建的目录下。
```Shell
$ sudo ./mkfs.wtfs -f /dev/sda
$ sudo mount -t wtfs /dev/sda ~/wtfs-test
```
或者你也可以使用一个回环设备，将一个常规文件当做块设备来访问，但**并不推荐**这么做（因为对回环设备调用 `mark_buffer_dirty` 可能会阻塞，导致写操作相当慢）。创建一个 4GB 大小的常规文件（任意大小都可以，但不要太小，这里就叫 `wtfs.img`），执行格式化操作并挂载。
```Shell
$ dd bs=4096 count=1048576 if=/dev/zero of=wtfs.img
$ ./mkfs.wtfs -f wtfs.img
$ sudo mount -o loop -t wtfs wtfs.img ~/wtfs-test
```

挂载完成后，你就可以在这个文件系统内做任何你想做的事了。

要卸载实例并从内核移除这个模块，执行下面的命令。
```Shell
$ sudo umount ~/wtfs-test
$ sudo rmmod wtfs
```

如果你想看 `mkfs.wtfs` 的具体用法，执行下面的命令。
```Shell
# mkfs.wtfs 的手册页
$ man man/zh_CN/man8/mkfs.wtfs.8
```

## 如何调试
除了将命令 `make` 替换为 `make debug` 之外，其他均按照上述步骤来做，这样生成的二进制文件就会包含调试符号信息，而且宏 `DEBUG` 也会被定义好。接下来你就可以使用外部的调试器——比如 gdb ——来调试 `mkfs.wtfs` 和 `statfs.wtfs` 了。

然而，调试模块的方法就比较原始了，因为到目前为止我没有使用任何内核调试器。我一直都是看模块的输出日志来调试的……所以如果你有任何更加高级的方法，那就用吧。

## 如何测试
当你完成构建后，只需敲入命令 `make test` 即可。有没有用调试模式都可以。

测试脚本需要使用下列二进制工具，但如果这些鬼不在你的 `PATH` 中也没有关系，在这种情况下，测试的某些部分会被跳过。

* `udisks2` 包下的 `udisksctl`
* `gvfs-bin` 包下的 `gvfs-mount`
* `uuid` 包下的 `uuid`
* `bc` 包下的 `bc`

## wtfs 物理磁盘布局
版本 0.6.0

![wtfs 布局](http://chaosdefinition.me/img/wtfs-layout-cn.png)

* 每个块大小为 4096 字节。
* 每个 i 节点大小为 64 字节。
* 每个目录项大小为 64 字节。
* 0 号块为引导块（暂不使用）。
* 1 号块为超级块。
* 2 号块为第 1 个 i 节点表，也是 i 节点表链的头。因为我们设计每个块的最后 8 字节用来作为指向另一个块的指针，所以一个 i 节点表最多能容纳 63 个 i 节点。i 节点表的个数由 i 节点位图的个数决定。
* 3 号块为第 1 个块位图，也是块位图链的头。同样的原因，一个块位图最多能表示 4088 * 8 个块。块位图的个数由设备大小决定。
* 4 号块为第 1 个 i 节点位图，也是 i 节点位图链的头。还是同样的原因，一个 i 节点位图最多能表示 4088 * 8 个 i 节点。i 节点位图的个数默认为 1 且在版本 0.5.0 之前无法改变。从版本 0.5.0 开始，它能在格式化时被设为一个在合理范围内的值（大于 0 且小于一个与设备大小相关的值）。
* 从 5 号块开始为文件数据块。文件数据块同样设置最后 8 字节为指向另一个块的指针，除了符号链接。对于符号链接，它们每一个都只有一个文件数据块，其中前 2 字节用来记录存放在剩下 4094 字节中的符号链接内容的长度。因此符号链接内容的最大长度为 4094 字节。

## 联系我
如果有任何问题或建议，请发送邮件至 chaosdefinition@hotmail.com
