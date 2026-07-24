#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

cd "$PROJECT_ROOT"

ENDPOINT="${LLM_ENDPOINT:-tcp://*:8899}"
STREAM_ENDPOINT="${LLM_STREAM_ENDPOINT:-tcp://*:8900}"
BACKEND="${LLM_BACKEND:-mock}"
CONTROL_ENDPOINT="${LLM_CONTROL_ENDPOINT:-tcp://*:8901}"

if [ ! -x "build/llm_server" ]; then
    echo "[ERROR] build/llm_server not found."
    echo "Run:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build -j"
    exit 1
fi

exec ./build/llm_server \
    --endpoint "$ENDPOINT" \
    --stream-endpoint "$STREAM_ENDPOINT" \
    --control-endpoint "$CONTROL_ENDPOINT" \
    --backend "$BACKEND"
