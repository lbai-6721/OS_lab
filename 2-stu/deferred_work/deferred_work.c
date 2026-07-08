#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FuShengyuan");
MODULE_DESCRIPTION("deferred work");

/*
 * 这里为学号后 3 位
 */
#define BASE_ID 120
#define FUNC_NUM 10

/*
 * work queue 上下文结构体
 * 每一个 work 对应一个独立的 work_struct 和 id
 */
struct work_ctx {
    struct work_struct work;
    int id;
};

/*
 * kernel thread 上下文结构体
 * 每一个 kernel thread 对应一个独立的线程和 id
 */
struct thread_ctx {
    struct task_struct *task;
    int id;
};

static struct work_ctx works[FUNC_NUM];
static struct thread_ctx thread_contexts[FUNC_NUM];

static struct delayed_work my_delayed_work;

/*
 * 10 个实验函数
 */
static void func1(const char *type)
{
    printk(KERN_INFO "%s function 1: %d\n", type, BASE_ID);
}

static void func2(const char *type)
{
    printk(KERN_INFO "%s function 2: %d\n", type, BASE_ID + 1);
}

static void func3(const char *type)
{
    printk(KERN_INFO "%s function 3: %d\n", type, BASE_ID + 2);
}

static void func4(const char *type)
{
    printk(KERN_INFO "%s function 4: %d\n", type, BASE_ID + 3);
}

static void func5(const char *type)
{
    printk(KERN_INFO "%s function 5: %d\n", type, BASE_ID + 4);
}

static void func6(const char *type)
{
    printk(KERN_INFO "%s function 6: %d\n", type, BASE_ID + 5);
}

static void func7(const char *type)
{
    printk(KERN_INFO "%s function 7: %d\n", type, BASE_ID + 6);
}

static void func8(const char *type)
{
    printk(KERN_INFO "%s function 8: %d\n", type, BASE_ID + 7);
}

static void func9(const char *type)
{
    printk(KERN_INFO "%s function 9: %d\n", type, BASE_ID + 8);
}

static void func10(const char *type)
{
    printk(KERN_INFO "%s function 10: %d\n", type, BASE_ID + 9);
}

typedef void (*lab_func_t)(const char *type);

static lab_func_t funcs[FUNC_NUM] = {
    func1,
    func2,
    func3,
    func4,
    func5,
    func6,
    func7,
    func8,
    func9,
    func10
};

/*
 * kernel thread 执行体
 * 每个线程调用一个函数
 */
static int kthread_handler(void *data)
{
    struct thread_ctx *ctx = data;
    int id;

    if (!ctx)
        return -EINVAL;

    id = ctx->id;

    /*
     * 加入不同的休眠时间，使 kernel thread 输出顺序更容易表现为非顺序。
     */
    msleep_interruptible((FUNC_NUM - id) * 20);

    if (!kthread_should_stop()) {
        if (id >= 0 && id < FUNC_NUM)
            funcs[id]("kernel thread");
    }

    /*
     * 打印完成后等待模块卸载时 kthread_stop()
     */
    while (!kthread_should_stop()) {
        msleep_interruptible(1000);
    }

    return 0;
}

/*
 * work queue 执行体
 * 每个 work 调用一个函数
 */
static void work_queue_handler(struct work_struct *work)
{
    struct work_ctx *ctx;
    int id;

    ctx = container_of(work, struct work_ctx, work);
    id = ctx->id;

    if (id >= 0 && id < FUNC_NUM)
        funcs[id]("work queue");
}

/*
 * delayed work 执行体
 */
static void delayed_work_handler(struct work_struct *work)
{
    printk(KERN_INFO "delayed work: this message is printed after 5 seconds\n");
}

/*
 * 内核模块初始化
 */
static int __init deferred_work_init(void)
{
    int i;
    int j;
    bool ok;

    printk(KERN_INFO "deferred work module init\n");

    /*
     * 初始化并调度 10 个 work
     */
    for (i = 0; i < FUNC_NUM; i++) {
        works[i].id = i;
        INIT_WORK(&works[i].work, work_queue_handler);

        ok = schedule_work(&works[i].work);
        if (!ok) {
            printk(KERN_WARNING "schedule_work failed or work already queued: %d\n", i);
        }
    }

    /*
     * 创建并唤醒 10 个 kernel thread
     */
    for (i = 0; i < FUNC_NUM; i++) {
        thread_contexts[i].id = i;

        thread_contexts[i].task = kthread_create(
            kthread_handler,
            &thread_contexts[i],
            "deferred_thread_%d",
            i + 1
        );

        if (IS_ERR(thread_contexts[i].task)) {
            int ret;

            printk(KERN_ERR "failed to create kernel thread %d\n", i + 1);

            ret = PTR_ERR(thread_contexts[i].task);
            thread_contexts[i].task = NULL;

            /*
             * 停止之前已经创建成功的线程
             */
            for (j = 0; j < i; j++) {
                if (thread_contexts[j].task) {
                    kthread_stop(thread_contexts[j].task);
                    thread_contexts[j].task = NULL;
                }
            }

            /*
             * 取消本模块已经提交的普通 work
             * 不再使用 flush_scheduled_work()
             */
            for (j = 0; j < FUNC_NUM; j++) {
                cancel_work_sync(&works[j].work);
            }

            return ret;
        }

        wake_up_process(thread_contexts[i].task);
    }

    /*
     * 初始化并调度 delayed work
     * 5 秒后执行 delayed_work_handler
     */
    INIT_DELAYED_WORK(&my_delayed_work, delayed_work_handler);
    schedule_delayed_work(&my_delayed_work, msecs_to_jiffies(5000));

    return 0;
}

/*
 * 内核模块退出
 */
static void __exit deferred_work_exit(void)
{
    int i;

    printk(KERN_INFO "deferred work module exit\n");

    /*
     * 取消 delayed work
     */
    cancel_delayed_work_sync(&my_delayed_work);

    /*
     * 取消或者等待本模块自己的普通 work
     * 不再使用 flush_scheduled_work()
     */
    for (i = 0; i < FUNC_NUM; i++) {
        cancel_work_sync(&works[i].work);
    }

    /*
     * 停止 10 个 kernel thread
     */
    for (i = 0; i < FUNC_NUM; i++) {
        if (thread_contexts[i].task) {
            kthread_stop(thread_contexts[i].task);
            thread_contexts[i].task = NULL;
        }
    }

    printk(KERN_INFO "deferred work module unloaded\n");
}

module_init(deferred_work_init);
module_exit(deferred_work_exit);