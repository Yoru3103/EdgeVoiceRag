#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"

CLIENT="./build/rag_client"
LOG_FILE="/tmp/python_rag_server_test.log"
ENDPOINT_BIND="tcp://*:5556"
ENDPOINT_CONNECT="tcp://localhost:5556"

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

if [ ! -x "$CLIENT" ]; then
    echo "[ERROR] C++ rag_client not found: $CLIENT"
    echo "Please build the C++ project first:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build"
    exit 1
fi

python python/rag/build_index.py \
    --manual docs/vehicle_manual.txt \
    --output vector_db/chunks.json

cleanup() {
    python - <<PY || true
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.setsockopt(zmq.RCVTIMEO, 1000)
sock.setsockopt(zmq.SNDTIMEO, 1000)
sock.setsockopt(zmq.LINGER, 0)
try:
    sock.connect("$ENDPOINT_CONNECT")
    sock.send_string("exit")
    sock.recv_string()
except Exception:
    pass
finally:
    sock.close()
    ctx.term()
PY
}

trap cleanup EXIT

echo "Starting Python RAG server..."
python python/rag/python_rag_server.py \
    --endpoint "$ENDPOINT_BIND" \
    --index vector_db/chunks.json \
    --top-k 3 > "$LOG_FILE" 2>&1 &

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

    echo "Running Python Rag server queryz: $query"

    local output
    output=$(python - <<PY
import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.setsockopt(zmq.RCVTIMEO, 3000)
sock.setsockopt(zmq.SNDTIMEO, 3000)
sock.setsockopt(zmq.LINGER, 0)
sock.connect("$ENDPOINT_CONNECT")
sock.send_string("$query")
print(sock.recv_string())
sock.close()
ctx.term()
PY
)
    if echo "$output" | grep -q '"ok": true' && 
       echo "$output" | grep -q "$expected" &&
       echo "$output" | grep -q "answer"; then
        echo "[PASS] $query"
    else
        cho "[FAIL] $query"
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
run_test "车里太热了怎么开冷气" "空调系统"
run_test "尾门怎么打开" "后备箱开启"

echo "Stopping Python RAG server..."
cleanup

sleep 0.5

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[WARNING] Python RAG server is still running, killing it."
    kill "$SERVER_PID" || true
fi

echo "All Python RAG server tests passed."