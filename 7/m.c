#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
    pid_t pid;

    pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }

    if (pid == 0) {
        /*
         * 这里是子进程 sub。
         *
         * 先 sleep 3 秒，是为了让 shell 脚本 s1 有时间通过 ps
         * 观察到 execl 之前的子进程。
         *
         * 注意：这里不打印 PID，PID 由 shell 脚本通过 ps 获取。
         */
        sleep(3);

        /*
         * 子进程 sub 调用 execl 执行 ex。
         * execl 成功后，当前子进程的代码段、数据段等会被 ex 程序替换。
         */
        execl("./ex", "ex", NULL);

        /*
         * 如果 execl 成功，下面代码不会执行。
         */
        perror("execl failed");
        exit(1);
    } else {
        /*
         * 父进程 m 等待子进程结束。
         * 因为 ex 会一直运行，所以 m 也会一直等待，方便 shell 脚本观察。
         */
        waitpid(pid, NULL, 0);
    }

    return 0;
}