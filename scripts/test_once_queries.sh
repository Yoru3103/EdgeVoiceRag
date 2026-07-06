#!/usr/bin/env bash

set -e  # 只要脚本中某条命令执行失败，就立刻退出脚本。

BIN="./build/edge_voice_rag"
CONFIG="config/app.conf"

if [ ! -x "$BIN" ]; then    # -x 表示可执行 双引号用来防止路径或字符串里有空格时出问题
    echo "[ERROR] Binary not found or not executable: $BIN"
    echo "Please build the project first:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build"
    exit 1  # 退出脚本，并返回失败状态
fi

run_test() {
    local query="$1"
    local expected="$2"

    echo "Running: $query"

    local output
    output=$("$BIN" --config "$CONFIG" --once "$query")

    if echo "$output" | grep -q "$expected"; then   # -q:quiet安静模式，只判断有没有找到，不打印结果
        echo "[PASS] $query"
    else
        echo "[FAIL] $query"
        echo "Expected keyword: $expected"
        echo "Actual output:"
        echo "$output"
        exit 1
    fi

    echo
}

run_test "空调怎么打开" "空调系统"
run_test "蓝牙怎么连接" "蓝牙连接"
run_test "胎压报警怎么办" "胎压报警"
run_test "雨刷怎么开" "雨刮控制"
run_test "怎么打开后备箱" "后备箱开启"
run_test "怎么导航到最近的充电桩" "导航系统"
run_test "今天吃什么" "Unknown query type"

echo "All tests passed."