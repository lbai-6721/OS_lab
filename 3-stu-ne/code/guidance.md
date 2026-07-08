
## 在kylinos上编译
```
# 编译内核模块
cd /home/user/workspace/3-stu-ne/code
make clean
make

# 编译用户空间测试程序
gcc -o user_space user_space.c
```

## 启动QEMU虚拟机
```
cd /home/user/workspace/3-stu-ne/vm
chmod +x start.sh
sudo ./start.sh
```

## 在QEMU虚拟机内操作
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

```
exit

poweroff
```
