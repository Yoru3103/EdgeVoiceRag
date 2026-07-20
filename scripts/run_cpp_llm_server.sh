#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

cd "$PROJECT_ROOT"

ENDPOINT="${LLM_ENDPOINT:-tcp://*:8899}"
BACKEND="${LLM_BACKEND:-mock}"

if [ ! -x "build/llm_server" ]; then
    echo "[ERROR] build/llm_server not found."
    echo "Run:"
    echo "  cmake -S . -B build"
    echo "  cmake --build build -j"
    exit 1
fi

exec ./build/llm_server \
    --endpoint "$ENDPOINT" \
    --backend "$BACKEND"