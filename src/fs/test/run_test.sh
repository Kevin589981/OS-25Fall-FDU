#!/bin/bash

# 设置运行次数
COUNT=100
cd build
echo "开始运行测试 $COUNT 次..."

for ((i=1; i<=COUNT; i++)); do
    echo "========================================"
    echo "Iteration: $i / $COUNT"
    echo "========================================"

    # 运行命令
    # 1. 先运行 make
    # 2. 如果 make 成功，运行 ./inode_test，并限制最长运行时间为 2秒
    make inode_test && timeout 2s ./inode_test
    
    # 获取上一个命令的退出状态码
    EXIT_CODE=$?

    # 判断结果
    if [ $EXIT_CODE -eq 124 ]; then
        echo "----------------------------------------"
        echo "❌ 失败: 第 $i 次运行超时 (超过2秒)！"
        exit 1
    elif [ $EXIT_CODE -ne 0 ]; then
        echo "----------------------------------------"
        echo "❌ 失败: 第 $i 次运行出错 (Exit Code: $EXIT_CODE)"
        exit 1
    fi
done

echo "========================================"
echo "✅ 成功: 全部 $COUNT 次测试均在2秒内通过。"