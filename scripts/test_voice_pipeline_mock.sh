#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"
LOG_FILE="/tmp/python_rag_server_voice_pipeline_test.log"
ENDPOINT_BIND="ipc:///tmp/edge_voice_rag_voice_pipeline_test_$$.ipc"
ENDPOINT_CONNECT="$ENDPOINT_BIND"
TTS_OUTPUT="voice_output/test_answer.txt"

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

    echo "Running voice pipeline query: $query"

    rm -f "$TTS_OUTPUT"

    local output
    output=$(PYTHONPATH=python python -m rag.voice_pipeline \
        --audio voice_input/mock.wav \
        --mock-asr-text "$query" \
        --rag-endpoint "$ENDPOINT_CONNECT" \
        --rag-timeout-ms 5000 \
        --asr-backend mock \
        --tts-backend mock \
        --tts-output "$TTS_OUTPUT")

    if ! echo "$output" | grep -q '"ok": true'; then
        echo "[FAIL] voice pipeline returned ok=false"
        echo "Actual output:"
        echo "$output"
        echo
        echo "Server log:"
        cat "$LOG_FILE"
        exit 1
    fi

    if ! echo "$output" | grep -q "$expected"; then
        echo "[FAIL] voice pipeline output missing expected keyword: $expected"
        echo "Actual output:"
        echo "$output"
        exit 1
    fi

    if [ ! -f "$TTS_OUTPUT" ]; then
        echo "[FAIL] TTS output file was not generated: $TTS_OUTPUT"
        exit 1
    fi

    if ! grep -q "$expected" "$TTS_OUTPUT"; then
        echo "[FAIL] TTS output file missing expected keyword: $expected"
        echo "TTS output:"
        cat "$TTS_OUTPUT"
        exit 1
    fi

    echo "[PASS] $query"
    echo
}

run_test "空调怎么打开" "空调系统"
run_test "车里太热了怎么开冷气" "空调系统"
run_test "尾门怎么打开" "后备箱开启"

echo "Stopping Python RAG server..."
cleanup

sleep 0.5

if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[WARNING] Python RAG server is still running, killing it."
    kill "$SERVER_PID" || true
fi

echo "All mock voice pipeline tests passed."
