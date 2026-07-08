#!/bin/bash

echo "========== s2: receive output from s1 =========="

# 从管道读取 s1 的全部输出。
input=$(cat)

# 为了运行截图清楚，这里把 s1 的输出重新打印出来。
echo "$input"

echo
echo "========== s2: parse PID values =========="

# 提取形如 m_pid=1234、sub_pid=1235、ex_pid=1235 的行。
# 由于 sub 和 ex 的 PID 通常相同，所以要 sort -u 去重。
pids=$(echo "$input" | grep -E '^(m_pid|sub_pid|ex_pid)=' | cut -d '=' -f 2 | grep -E '^[0-9]+$' | sort -u)

if [ -z "$pids" ]; then
    echo "No PID found from s1 output."
    exit 1
fi

echo "PIDs to kill:"
echo "$pids"

echo
echo "========== s2: process status before kill =========="

for pid in $pids
do
    ps -p "$pid" -o pid,ppid,comm,args
done

echo
echo "========== s2: send SIGTERM =========="

for pid in $pids
do
    if ps -p "$pid" >/dev/null 2>&1; then
        echo "kill $pid"
        kill "$pid" 2>/dev/null
    else
        echo "PID $pid does not exist."
    fi
done

sleep 1

echo
echo "========== s2: check after SIGTERM =========="

for pid in $pids
do
    if ps -p "$pid" >/dev/null 2>&1; then
        echo "PID $pid is still alive, send SIGKILL."
        kill -9 "$pid" 2>/dev/null
    else
        echo "PID $pid has been killed."
    fi
done

sleep 1

echo
echo "========== s2: final check =========="

for pid in $pids
do
    if ps -p "$pid" >/dev/null 2>&1; then
        echo "PID $pid still exists."
    else
        echo "PID $pid does not exist now."
    fi
done