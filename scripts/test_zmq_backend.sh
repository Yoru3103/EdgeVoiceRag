#!/usr/bin/env bash
# #！解释器路径 参数；去当前环境变量PATH里找bash，然后执行脚本 前两个符号告诉操作系统后面是解释器

set -e

BIN="build/edge_voice_rag"
SERVER="build/rag_server"
CONFIG="config/zmq.conf"
LOG_FILE="/tmp/edge_voice_rag_server_test.log"

if [ ! -x "$BIN" ]; then
    echo "[ERROR] Binary not found or not executable: $BIN"
    echo "Please build the project first:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build"
    exit 1
fi

if [ ! -x "$SERVER" ]; then
    echo "[ERROR] Server binary not found or not executable: $SERVER"
    echo "Please build the project first:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "[ERROR] Config file not found: $CONFIG"
    exit 1
fi

# 0  stdin   标准输入
# 1  stdout  标准输出
# 2  stderr  标准错误
cleanup() {
    if pgrep -f "./build/rag_server" > /dev/null; then  #pgrep查找进程，并把标准输出（进程号）丢进/dev/null（黑洞）类似静默显示
        "$BIN" --config "$CONFIG" --once "exit" > /dev/null 2>&1 || true    #2>&1表示把错误输出stderr丢进当前stdout的位置，也就是/dev/null || true表示无论如何都为真 防止异常退出
        sleep 0.2
    fi
}

trap cleanup EXIT

echo "Starting rag_server..."
"$SERVER" --config config/app.conf > "$LOG_FILE" 2>&1 & # 最后的&表示脚本放在后台运行，不会卡在这里等待结束

SERVER_PID=$!   # 最近一个后台进程的 PID。

sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[ERROR] rag_server failed to start."
    echo "Server log:"
    cat "$LOG_FILE"
    exit 1
fi

run_test() {
    local query="$1"
    local expected="$2"

    echo "Running ZMQ query: $query"

    local output
    output=$("$BIN" --config "$CONFIG" --once "$query")

    if echo "$output" | grep -q "$expected"; then
        echo "[PASS] $query"
    else
        echo "[FAIL] $query"
        echo "Expected keyword: $expected"
        echo "Actual output:"
        echo "$output"
        echo
        echo "Server log:"
        cat "$LOG_FILE"
        exit 1
    fi

    echo
}

run_test "空调怎么打开" "空调系统"
run_test "蓝牙怎么连接" "蓝牙连接"
run_test "胎压报警怎么办" "胎压报警"
run_test "雨刷怎么开" "雨刮控制"
run_test "怎么打开后备箱" "后备箱开启"

echo "Stopping rag_server..."
"$BIN" --config "$CONFIG" --once "exit" > /dev/null 2>&1 || true

sleep 1

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[WARNING] rag_server is still running, killing it."
    kill "$SERVER_PID" || true
fi

echo "All ZMQ backend tests passed."