#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

cd "$PROJECT_ROOT"

BACKEND="${LLM_BACKEND:-mock}"
MODEL="${LLM_MODEL:-qwen2.5:3b}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
ENDPOINT="${LLM_ENDPOINT:-tcp://*:8899}"
TIMEOUT_SECONDS="${LLM_TIMEOUT_SECONDS:-60}"

PYTHONPATH=python python \
    -m rag.llm_zmq_server \
    --endpoint "$ENDPOINT" \
    --backend "$BACKEND" \
    --model "$MODEL" \
    --ollama-url "$OLLAMA_URL" \
    --timeout-seconds "$TIMEOUT_SECONDS"