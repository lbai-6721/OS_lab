CC=gcc
CFLAGS=-std=c11

ifneq  ($(KERNELRELEASE),)
obj-m:=edu_dev.o
else
KDIR := /mnt/hgfs/openplylin_shared/3-stu-ne/vm/linux
PWD:=$(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	rm -f *.ko *.o *.symvers *.cmd *.cmd.o .*.cmd *.mod *.mod.c *.order

install:
	make -C $(KDIR) M=$(PWD) modules
	sudo insmod edu_dev.ko
	sudo mknod /dev/edu c 200 0

uninstall:
	sudo rmmod edu_dev
	sudo rm -rf /dev/edu

endif