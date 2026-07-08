# 安装依赖
```
sudo apt update
sudo apt install -y \
  build-essential gcc g++ make \
  flex bison libncurses-dev libssl-dev libelf-dev \
  bc dwarves \
  qemu-system-x86 qemu-utils \
  git rsync cpio unzip wget file \
  btrfs-progs
```

# 编译内核
```
cd linux
make x86_64_defconfig
make menuconfig
# make bzImage -j$(nproc)

mkdir -p ~/oslab
cp arch/x86/boot/bzImage ~/oslab/bzImage
```

```
mkdir -p ~/oslab

rsync -a /var/mnt/hgfs/openplylin_shared/4-stu-ne/ne/linux/ ~/oslab/linux/

cd ~/oslab/linux

make olddefconfig

make bzImage -j$(nproc)

cd ~/oslab/linux
grep CONFIG_NETFILTER_XT_TARGET_TCPMSS .config
ls -l net/netfilter/xt_TCPMSS.c

./scripts/config --disable NETFILTER_XT_TARGET_TCPMSS
make olddefconfig
make bzImage -j$(nproc)

cp ~/oslab/linux/arch/x86/boot/bzImage ~/oslab/images/bzImage


cd ~/oslab/buildroot
make menuconfig

cd ~/oslab/buildroot
make -j$(nproc)

mkdir -p ~/oslab/images
cp ~/oslab/buildroot/output/images/rootfs.btrfs ~/oslab/images/rootfs.btrfs


cd ~/oslab/images
qemu-system-x86_64 \
  -smp 2 \
  -m 2048 \
  -kernel bzImage \
  -drive file=rootfs.btrfs,if=virtio,format=raw \
  -append "console=ttyS0 root=/dev/vda rw init=/sbin/init" \
  -virtfs local,path=/,mount_tag=host0,security_model=passthrough \
  -device edu \
  -nographic
```

```
mkdir -p ~/oslab

rsync -a /var/mnt/hgfs/openplylin_shared/4-stu-ne/ne/buildroot/ ~/oslab/buildroot/
```

```
mkdir -p ~/oslab

rsync -a /var/mnt/hgfs/openplylin_shared/3-stu-ne/code/ ~/oslab/edu_driver/

cd ~/oslab/linux
make modules_prepare

cd ~/oslab/edu_driver
make clean
make
cp edu.ko ~/oslab/images/
```

# fix
```
cd ~/oslab/buildroot

cat > package/busybox/S02sysctl <<'EOF'
#!/bin/sh

case "$1" in
	start)
		[ -x /sbin/sysctl ] && /sbin/sysctl -e -p /etc/sysctl.conf >/dev/null 2>&1 || true
		;;
	stop)
		;;
	restart|reload)
		[ -x /sbin/sysctl ] && /sbin/sysctl -e -p /etc/sysctl.conf >/dev/null 2>&1 || true
		;;
	*)
		echo "Usage: $0 {start|stop|restart|reload}"
		exit 1
		;;
esac

exit 0
EOF

chmod +x package/busybox/S02sysctl

make -j$(nproc)
```
cd ~/oslab/buildroot

mkdir -p board/oslab/rootfs-overlay/etc/init.d

cat > board/oslab/rootfs-overlay/etc/init.d/S00runtime <<'EOF'
#!/bin/sh

case "$1" in
	start)
		mkdir -p /run /run/lock
		chmod 755 /run /run/lock

		mkdir -p /var

		if [ ! -e /var/run ]; then
			ln -snf /run /var/run
		fi

		if [ ! -e /var/lock ]; then
			ln -snf /run/lock /var/lock
		fi
		;;
	stop)
		;;
	restart|reload)
		"$0" start
		;;
	*)
		echo "Usage: $0 {start|stop|restart|reload}"
		exit 1
		;;
esac

exit 0
EOF

chmod +x board/oslab/rootfs-overlay/etc/init.d/S00runtime


cat > board/oslab/post_build.sh <<'EOF'
#!/bin/sh

TARGET_DIR="$1"

mkdir -p "${TARGET_DIR}/var"
mkdir -p "${TARGET_DIR}/run"

rm -rf "${TARGET_DIR}/var/run"
ln -snf /run "${TARGET_DIR}/var/run"

rm -rf "${TARGET_DIR}/var/lock"
ln -snf /run/lock "${TARGET_DIR}/var/lock"
EOF

chmod +x board/oslab/post_build.sh

./utils/config --set-str BR2_ROOTFS_OVERLAY "board/oslab/rootfs-overlay"
./utils/config --set-str BR2_ROOTFS_POST_BUILD_SCRIPT "board/oslab/post_build.sh"
make olddefconfig
make


## 验证定制系统
```
# 查看根文件系统类型
df -T /
# 输出：/dev/root  btrfs  209920  45748  92972  33% /
# 验证 vim
vim --version | head -1
# 输出：VIM - Vi IMproved 9.1
# 验证 bash
bash --version | head -1
# 输出：GNU bash, version 5.2.21(1)-release (x86_64-buildroot-linux-gnu)
```

## 加载edu驱动并测试
```
# 以 root 登录后，挂载宿主机文件系统
mkdir -p /mnt/host
mount -t 9p -o trans=virtio host0 /mnt/host

# 进入代码目录
cd /mnt/host/mnt/hgfs/openplylin_shared/3-stu-ne/code

# 加载驱动
insmod edu_dev.ko

# 查看内核日志确认 probe 成功
dmesg | tail

# 创建设备节点（主设备号 200，次设备号 0）
mknod /dev/edu c 200 0

# 运行测试程序
./user_space
```


## 查询与修改内核参数
```
# 查看修改前的值
sysctl kernel.shmmax
# 输出：kernel.shmmax = 33554432
# 修改参数
sysctl -w kernel.shmmax=68719476736
# 输出：kernel.shmmax = 68719476736
# 验证修改成功
sysctl kernel.shmmax
# 输出：kernel.shmmax = 68719476736
```