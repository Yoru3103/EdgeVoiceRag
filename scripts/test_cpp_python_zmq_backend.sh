#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"

BIN="./build/edge_voice_rag"
CONFIG="config/python_zmq.conf"
LOG_FILE="/tmp/python_rag_server_cpp_test.log"

if [ -z "$CONDA_DEFAULT_ENV" ]; then
    echo "[ERROR] No conda environment is currently activated."
    echo "Please run:"
    echo "  conda activate $EXPECTED_ENV"
    exit 1
fi

if [ "$CONDA_DEFAULT_ENV" != "$EXPECTED_ENV" ]; then
    echo "[ERROR] Current conda environment is: $CONDA_DEFAULT_ENV"
    echo "Expected environment: $EXPECTED_ENV"
    echo "Please run:"
    echo "  conda activate $EXPECTED_ENV"
    exit 1
fi

if [ ! -f "$BIN" ]; then
    echo "[ERROR] Binary not found or not executable: $BIN"
    echo "Please build the project first"
    echo "  cmake -S . -B build"
    echo "  cmake --build build"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "[ERROR] Config file not found: $CONFIG"
    exit 1
fi

python python/rag/build_index.py \
    --manual docs/vehicle_manual.txt \
    --output vector_db/chunks.json

cleanup() {
    "$BIN" --config "$CONFIG" --once "exit" > /dev/null 2>&1 || true
    sleep 0.2
}

trap cleanup EXIT

echo "Starting Python RAG server..."
python python/rag/python_rag_server.py \
    --endpoint tcp://*:5556 \
    --index vector_db/chunks.json \
    --top-k 3 \
    --llm-backend mock > "$LOG_FILE" 2>&1 &

SERVER_PID=$!

sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[ERROR] Python RAG server failed to start."
    echo "Server log:"
    cat "$LOG_FILE"
    exit 1
fi

run_test() {
    local query="$1"
    local expected="$2"

    echo "Running C++ -> Python ZMQ query: $query"

    local output
    output=$("$BIN" --config "$CONFIG" --once "$query")

    if echo "$output" | grep -q "$expected" &&
       echo "$output" | grep -q "根据车辆手册"; then
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

run_test "空调怎么打开" "打开空调"
run_test "蓝牙怎么连接" "配对连接"
run_test "胎压报警怎么办" "胎压报警"
run_test "雨刷怎么开" "方向盘右侧拨杆"
run_test "车里太热了怎么开冷气" "制冷"
run_test "尾门怎么打开" "尾门"

echo "Stopping Python RAG server..."
"$BIN" --config "$CONFIG" --once "exit" > /dev/null 2>&1 || true

sleep 0.5

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[WARNING] Python RAG server is still running, killing it."
    kill "$SERVER_PID" || true
fi

echo "All C++ -> Python ZMQ backend tests passed."