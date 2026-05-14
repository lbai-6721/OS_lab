#include <asm/io.h>
#include <asm/processor.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/pid.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/types.h>

// 保存edu设备信息
struct edu_dev_info
{
	/* BAR0 物理起始地址、映射长度和资源属性 */
	resource_size_t io;
	long range, flags;
	/* ioremap 后得到的内核虚拟地址，用于访问 EDU 寄存器 */
	void __iomem *ioaddr;
	/* PCI 设备分配到的中断号 */
	int irq;
};

static struct pci_device_id id_table[] = {
	{PCI_DEVICE(0x1234, 0x11e8)}, // edu设备id
	{
		0,
	} // 最后一组是0，表示结束
};

/* 当前只管理一个 EDU PCI 设备，因此使用全局指针保存设备上下文 */
struct edu_dev_info *edu_info;
/* 保护 EDU 设备寄存器访问，避免多个 ioctl 线程同时操作硬件 */
spinlock_t lock;

/// @brief edu设备发现函数
/// @param dev 
/// @param id 
/// @return 
static int edu_driver_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret = 0;
	printk("executing edu driver probe function!\n");

	ret = pci_enable_device(dev);
	if (ret)
	{
		printk(KERN_ERR "IO Error.\n");
		return -EIO;
	}

	// 为当前PCI设备分配并清零驱动私有数据，避免成员未初始化
	edu_info = kzalloc(sizeof(*edu_info), GFP_KERNEL);
	if (!edu_info)
	{
		ret = -ENOMEM;
		goto out_disable_device;
	}
	// 保存设备中断号，后续申请中断或处理中断时使用
	edu_info->irq = dev->irq;

	ret = pci_request_regions(dev, "edu_dirver"); // 申请一块驱动掌管的内存空间
	if (ret)
	{
		printk("PCI request regions err!\n");
		goto out_mypci;
	}

	// EDU设备寄存器位于BAR0，记录BAR0的物理起始地址、长度和属性
	edu_info->io = pci_resource_start(dev, 0);
	edu_info->range = pci_resource_len(dev, 0);
	edu_info->flags = pci_resource_flags(dev, 0);

	// 将BAR0物理地址映射为内核虚拟地址，后续通过readl/writel访问设备寄存器
	edu_info->ioaddr = ioremap(edu_info->io, edu_info->range);
	if (!edu_info->ioaddr)
	{
		ret = -ENOMEM;
		goto out_regions;
	}


	pci_set_drvdata(dev, edu_info); // 设置驱动私有数据
	printk("Probe succeeds.PCIE ioport addr start at %llX, edu_info->ioaddr is 0x%p.\n", edu_info->io, edu_info->ioaddr);

	return 0;

out_regions:
	// 映射失败时释放已申请的PCI BAR资源
	pci_release_regions(dev);
out_mypci:
	// 资源申请失败或后续失败时释放驱动私有数据
	kfree(edu_info);
out_disable_device:
	// probe失败时关闭此前启用的PCI设备
	pci_disable_device(dev);
	return ret;
}

/// @brief edu设备移除函数
/// @param dev 
static void edu_driver_remove(struct pci_dev *dev)
{
	struct edu_dev_info *info = pci_get_drvdata(dev);

	/* 按 probe 中申请资源的反向顺序释放设备资源 */
	iounmap(info->ioaddr);
	pci_release_regions(dev);
	kfree(info);
	pci_set_drvdata(dev, NULL);
	edu_info = NULL;
	pci_disable_device(dev);
	printk("Device is removed successfully.\n");
}

MODULE_DEVICE_TABLE(pci, id_table); // 暴露驱动能发现的设备ID表单

static struct pci_driver pci_driver = {
	.name = "edu_dirver",
	.id_table = id_table,
	.probe = edu_driver_probe,
	.remove = edu_driver_remove,
};

// =============================================================================== //

#define EDU_DEV_MAJOR 200  /* 主设备号 */
#define EDU_DEV_NAME "edu" /* 设备名 */


int current_id = 0;

/* 每个打开的 /dev/edu 文件描述符对应一份私有数据 */
struct user_data
{
	/* 用于区分不同文件实例，也作为线程名的一部分 */
	int id;
	/* 保存最近一次阶乘计算结果，ioctl cmd 1 读取该值 */
	atomic64_t data;
	/* 当前文件实例正在运行的计算线程数量 */
	atomic_t active_threads;
	/* close 时等待所有计算线程完成，避免释放后线程继续访问 */
	struct completion threads_done;
};

/* 传递给内核线程的参数 */
struct thread_data
{
	/* 指回当前文件实例，线程完成后把结果写回这里 */
	struct user_data* user_data_ptr;
	/* 用户通过 ioctl cmd 0 传入的阶乘输入值 */
	int input_data;
};


static bool edu_thread_is_last(struct user_data *user_data_ptr)
{
	return atomic_dec_and_test(&user_data_ptr->active_threads);
}


int kthread_handler(void *data)
{
	struct thread_data* thread_data_ptr = (struct thread_data*)data;
	struct user_data *user_data_ptr = thread_data_ptr->user_data_ptr;
	void __iomem *base = edu_info->ioaddr;
	uint64_t value = thread_data_ptr->input_data;
	printk("ioctl cmd 0 : factorial\n");

	/*
	 * EDU 阶乘功能通过 MMIO 寄存器完成：
	 * 0x08 写入输入值并读取结果，0x20 bit0 表示设备忙。
	 */
	spin_lock(&lock);
	writel((u32)value, base + 0x08);
	while (readl(base + 0x20) & 0x01)
		cpu_relax();
	value = readl(base + 0x08);
	spin_unlock(&lock);

	/* 线程结束前把计算结果保存到当前文件实例，供后续 ioctl 读取 */
	atomic64_set(&user_data_ptr->data, value);
	kfree(thread_data_ptr);

	if (edu_thread_is_last(user_data_ptr))
		kthread_complete_and_exit(&user_data_ptr->threads_done, 0);

	return 0;
}



/// @brief open处理函数
/// @param inode 
/// @param filp 
/// @return 
static int edu_dev_open(struct inode *inode, struct file *filp)
{
	struct user_data* user_data_ptr = (struct user_data*)kmalloc(sizeof(struct user_data), GFP_KERNEL);
	if (!user_data_ptr)
		return -ENOMEM;

	/* 初始化当前文件实例的上下文，并挂到 private_data 上 */
	user_data_ptr->id = current_id++;
	atomic64_set(&user_data_ptr->data, 0);
	atomic_set(&user_data_ptr->active_threads, 0);
	init_completion(&user_data_ptr->threads_done);

	filp->private_data = user_data_ptr;
	return 0;
}


/// @brief close处理函数
/// @param inode 
/// @param filp 
/// @return 
static int edu_dev_release(struct inode *inode, struct file *filp)
{
	struct user_data *user_data_ptr = filp->private_data;

	if (user_data_ptr && atomic_read(&user_data_ptr->active_threads))
		wait_for_completion(&user_data_ptr->threads_done);

	/* 释放 open 时分配的文件私有数据 */
	kfree(user_data_ptr);
	filp->private_data = NULL;
	return 0;
}


/// @brief ioctl处理函数
/// @param pfilp_t 
/// @param cmd 
/// @param arg 
/// @return 
long edu_dev_unlocked_ioctl(struct file *pfilp_t, unsigned int cmd, unsigned long arg)
{
	struct user_data *user_data_ptr = pfilp_t->private_data;
	struct thread_data *thread_data_ptr;
	struct task_struct *task;

	if (!user_data_ptr)
		return -EINVAL;

	switch (cmd)
	{
	case 0:
		/* cmd 0：启动一次阶乘计算，arg 为用户传入的输入值 */
		if (!edu_info || !edu_info->ioaddr)
			return -ENODEV;

		thread_data_ptr = kmalloc(sizeof(*thread_data_ptr), GFP_KERNEL);
		if (!thread_data_ptr)
			return -ENOMEM;

		thread_data_ptr->user_data_ptr = user_data_ptr;
		thread_data_ptr->input_data = (int)arg;

		if (atomic_inc_return(&user_data_ptr->active_threads) == 1)
			reinit_completion(&user_data_ptr->threads_done);

		/* 使用内核线程执行硬件计算，避免 ioctl 长时间直接占用调用路径 */
		task = kthread_run(kthread_handler, thread_data_ptr,
				   "edu_factorial_%d", user_data_ptr->id);
		if (IS_ERR(task))
		{
			if (edu_thread_is_last(user_data_ptr))
				complete(&user_data_ptr->threads_done);
			kfree(thread_data_ptr);
			return PTR_ERR(task);
		}

		return 0;
	case 1:
		/* cmd 1：返回当前文件实例最近一次保存的阶乘结果 */
		return (long)atomic64_read(&user_data_ptr->data);
	default:
		return -EINVAL;
	}
}


static struct file_operations edu_dev_fops = {
	/* 字符设备对用户态暴露 open/close/ioctl 三个操作 */
	.owner = THIS_MODULE,
	.open = edu_dev_open,
	.release = edu_dev_release,
	.unlocked_ioctl = edu_dev_unlocked_ioctl,
};
/// @brief 驱动程序初始化
/// @param  
/// @return 
static int __init edu_dirver_init(void)
{
	printk("HELLO PCI\n");
	int ret = 0;
	// 注册字符设备
	ret = register_chrdev(EDU_DEV_MAJOR, EDU_DEV_NAME, &edu_dev_fops);
	if (0 > ret)
	{
		printk("kernel edu dev register_chrdev failure\n");
		return -1;
	}
	printk("chrdev edu dev is insmod, major_dev is 200\n");
	// 注册edu pci设备
	ret = pci_register_driver(&pci_driver);
	if (ret)
	{
		printk("kernel edu dev pci_register_driver failure\n");
		unregister_chrdev(EDU_DEV_MAJOR, EDU_DEV_NAME);
		return ret;
	}
	// 初始化自旋锁
    spin_lock_init(&lock);
	return 0;
}
/// @brief 驱动程序注销
/// @param  
/// @return 
static void __exit edu_dirver_exit(void)
{
	// 注销字符设备
	unregister_chrdev(EDU_DEV_MAJOR, EDU_DEV_NAME);
	// 注销edu pci设备
	pci_unregister_driver(&pci_driver);
	printk("GOODBYE PCI\n");
}

MODULE_LICENSE("GPL");

module_init(edu_dirver_init);
module_exit(edu_dirver_exit);
