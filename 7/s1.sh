#!/bin/bash

# 启动 m 程序，放到后台执行。
# m 和 ex 本身不负责打印 PID。
./m >/dev/null 2>&1 &

# $! 是刚刚放入后台的 m 进程 PID。
# 这里用它作为 ps 查询的定位依据，真正打印的 PID 仍然来自 ps 命令结果。
started_pid=$!

sleep 1

echo "========== s1: find m process by ps =========="

# 通过 ps 获取 m 的进程信息。
m_line=$(ps -p "$started_pid" -o pid=,ppid=,comm=,args=)

if [ -z "$m_line" ]; then
    echo "Error: m process not found."
    exit 1
fi

echo "$m_line"

# 从 ps 输出中取出 m 的 PID。
m_pid=$(echo "$m_line" | awk '{print $1}')

echo "m_pid=$m_pid"

echo
echo "========== s1: find sub process by ps =========="

# 此时 sub 还没有 execl 成 ex。
# sub 是 m 的子进程，所以用 ppid=m_pid 查找。
sub_line=""

for i in $(seq 1 10)
do
    sub_line=$(ps -eo pid=,ppid=,comm=,args= | awk -v ppid="$m_pid" '$2 == ppid {print; exit}')

    if [ -n "$sub_line" ]; then
        break
    fi

    sleep 1
done

if [ -z "$sub_line" ]; then
    echo "Error: sub process not found."
    exit 1
fi

echo "$sub_line"

sub_pid=$(echo "$sub_line" | awk '{print $1}')

echo "sub_pid=$sub_pid"

echo
echo "========== s1: wait sub execl to ex =========="

ex_line=""

# m.c 中 sub 会 sleep 3 秒后 execl("./ex", "ex", NULL)
# 因此这里等待 comm 字段变成 ex。
for i in $(seq 1 10)
do
    ex_line=$(ps -p "$sub_pid" -o pid=,ppid=,comm=,args= | awk '$3 == "ex" {print}')

    if [ -n "$ex_line" ]; then
        break
    fi

    sleep 1
done

if [ -z "$ex_line" ]; then
    echo "Error: ex process not found."
    exit 1
fi

echo "$ex_line"

ex_pid=$(echo "$ex_line" | awk '{print $1}')

echo "ex_pid=$ex_pid"

echo
echo "========== s1: summary =========="
echo "m_pid=$m_pid"
echo "sub_pid=$sub_pid"
echo "ex_pid=$ex_pid"
echo "Notice: sub_pid and ex_pid should be the same if execl succeeds."