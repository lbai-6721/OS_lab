#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lbai");
MODULE_DESCRIPTION("Kernel API Lab: process list with two kernel threads");

struct pid_node {
    pid_t pid;
    char comm[TASK_COMM_LEN];
    struct list_head list;
};

static LIST_HEAD(my_list);
static spinlock_t list_lock;

static struct task_struct *thread1;
static struct task_struct *thread2;

/*
 * thread1:
 * 遍历当前系统所有进程，把 pid 和进程名保存到链表中
 */
static int thread1_func(void *data)
{
    struct task_struct *p;
    struct pid_node *node;

    printk(KERN_INFO "thread1 started\n");

    /*
     * 这里选择只遍历一次进程列表。
     * 如果你想周期性遍历，可以把 for_each_process 放到 while 循环里。
     */
    for_each_process(p) {
        if (kthread_should_stop())
            break;

        node = kmalloc(sizeof(struct pid_node), GFP_KERNEL);
        if (!node) {
            printk(KERN_ERR "kmalloc failed in thread1\n");
            continue;
        }

        node->pid = p->pid;
        get_task_comm(node->comm, p);
        INIT_LIST_HEAD(&node->list);

        spin_lock(&list_lock);
        list_add_tail(&node->list, &my_list);
        spin_unlock(&list_lock);
    }

    printk(KERN_INFO "thread1 finished collecting process info\n");

    while (!kthread_should_stop()) {
        msleep_interruptible(1000);
    }

    printk(KERN_INFO "thread1 stopped\n");

    return 0;
}

/*
 * thread2:
 * 从链表中取出节点，打印 pid 和进程名，然后释放节点
 */
static int thread2_func(void *data)
{
    struct pid_node *node;

    printk(KERN_INFO "thread2 started\n");

    while (!kthread_should_stop()) {
        node = NULL;

        spin_lock(&list_lock);

        if (!list_empty(&my_list)) {
            node = list_first_entry(&my_list, struct pid_node, list);
            list_del(&node->list);
        }

        spin_unlock(&list_lock);

        if (node) {
            printk(KERN_INFO "pid: %d, name: %s\n", node->pid, node->comm);
            kfree(node);
        } else {
            msleep_interruptible(500);
        }
    }

    printk(KERN_INFO "thread2 stopped\n");

    return 0;
}

static int __init kernel_module_init(void)
{
    printk(KERN_INFO "kernel_module init\n");

    spin_lock_init(&list_lock);

    thread1 = kthread_create(thread1_func, NULL, "thread1");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "failed to create thread1\n");
        return PTR_ERR(thread1);
    }

    wake_up_process(thread1);

    thread2 = kthread_create(thread2_func, NULL, "thread2");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "failed to create thread2\n");
        kthread_stop(thread1);
        thread1 = NULL;
        return PTR_ERR(thread2);
    }

    wake_up_process(thread2);

    return 0;
}

static void __exit kernel_module_exit(void)
{
    struct pid_node *node;

    printk(KERN_INFO "kernel_module exit\n");

    if (thread1 && !IS_ERR(thread1)) {
        kthread_stop(thread1);
        thread1 = NULL;
    }

    if (thread2 && !IS_ERR(thread2)) {
        kthread_stop(thread2);
        thread2 = NULL;
    }

    spin_lock(&list_lock);

    while (!list_empty(&my_list)) {
        node = list_first_entry(&my_list, struct pid_node, list);
        list_del(&node->list);
        kfree(node);
    }

    spin_unlock(&list_lock);

    printk(KERN_INFO "kernel_module unloaded\n");
}

module_init(kernel_module_init);
module_exit(kernel_module_exit);