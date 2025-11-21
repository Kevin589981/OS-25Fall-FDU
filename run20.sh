#!/bin/bash
for i in {1..20}; do
    echo "=== round $i ==="
    cd build
    timeout --foreground 5s make qemu > ../check_pid/log/pid_log.txt 2>&1
    cd ../check_pid
    if python3 check_pid_log.py; then
        echo "round $i  ✅"
    else
        echo "round $i  ❌"
        cp ./log/pid_log.txt log/fail_$i.txt
    fi
    cd ..
done