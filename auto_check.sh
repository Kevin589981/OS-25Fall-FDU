#!/usr/bin/bash
# auto_check.sh
# 20 轮自动化测试脚本

ROOT_DIR="$(pwd)"                       # 根目录就是脚本启动时的目录
BUILD_DIR="$ROOT_DIR/build"
LOG_DIR="$ROOT_DIR/check_log"
FAIL_LOG_DIR="$LOG_DIR/fail_log"

PYTHON_SCRIPT="$LOG_DIR/check.py"       # 你的 Python 检查脚本
LOG_FILE="$LOG_DIR/log.txt"             # 每轮日志路径

mkdir -p "$BUILD_DIR" "$LOG_DIR" "$FAIL_LOG_DIR"

for ((round=1; round<=20; round++)); do
    echo "=== Round $round/20 ==="

    # 1. 清空旧日志
    > "$LOG_FILE"

    # 2. 启动 qemu（后台）
    cd "$BUILD_DIR"
    cmake .. && make qemu > "$LOG_FILE" 2>&1 &
    QEMU_PID=$!

    sleep 5

    echo -e '\x01x' > /dev/null 2>&1 | nc -q1 127.0.0.1 1234 2>/dev/null || true
    # 如果上面 nc 方式无效，可改用 tmux 或 screen，这里用简单粗暴的 kill
    sleep 1
    kill $QEMU_PID 2>/dev/null || true
    wait $QEMU_PID 2>/dev/null || true

    # 5. 检查日志
    cd "$ROOT_DIR"
    # python3 "$PYTHON_SCRIPT" "$LOG_FILE" > "$LOG_DIR/result.txt" 2>&1
    if python3 "$PYTHON_SCRIPT" "$LOG_FILE" | grep -q "RESULT: SUCCESS"; then
        echo "Round $round: success"
    else
        echo "Round $round: fail"
        cp "$LOG_FILE" "$FAIL_LOG_DIR/round_${round}.txt"
    fi
done

echo "=== All 20 rounds finished ==="