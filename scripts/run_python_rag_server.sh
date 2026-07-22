#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"

LLM_BACKEND="${LLM_BACKEND:-zmq}"
LLM_ENDPOINT="${LLM_ENDPOINT:-tcp://127.0.0.1:8899}"
LLM_TIMEOUT="${LLM_TIMEOUT:-120}"
RAG_STREAM_ENDPOINT="${RAG_STREAM_ENDPOINT:-tcp://*:5557}"
LLM_STREAM_ENDPOINT="${LLM_STREAM_ENDPOINT:-tcp://127.0.0.1:8900}"

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

if [ ! -f "vector_db/chunks.json" ]; then
    echo "[INFO] vector_db/chunks.json not found. Building index first..."
    python python/rag/build_index.py \
        --manual docs/vehicle_manual.txt \
        --output vector_db/chunks.json
fi

PYTHONPATH=python python -m rag.python_rag_server \
    --endpoint tcp://*:5556 \
    --stream-endpoint "$RAG_STREAM_ENDPOINT" \
    --index vector_db/chunks.json \
    --top-k 3 \
    --llm-backend "$LLM_BACKEND" \
    --llm-endpoint "$LLM_ENDPOINT" \
    --llm-stream-endpoint "$LLM_STREAM_ENDPOINT" \
    --llm-timeout "$LLM_TIMEOUT"
