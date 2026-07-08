#include <unistd.h>

int main()
{
    /*
     * ex 程序保持运行，方便 shell 脚本通过 ps 命令观察到它。
     * 注意：本程序不打印 PID，PID 的获取由 shell 脚本通过 ps 完成。
     */
    while (1) {
        sleep(1);
    }

    return 0;
}