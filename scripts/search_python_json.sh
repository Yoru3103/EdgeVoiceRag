#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"

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

if [ $# -lt 1 ]; then
    echo "Usage:"
    echo "  $0 \"空调怎么打开\""
    echo "  $0 \"蓝牙怎么连接\" 5"
    exit 1
fi

QUERY="$1"
TOP_K="${2:-3}"

if [ ! -f "vector_db/chunks.json" ]; then
    echo "[INFO] vector_db/chunks.json not found. Building index first..." >&2
    python python/rag/build_index.py \
        --manual docs/vehicle_manual.txt \
        --output vector_db/chunks.json
fi

python python/rag/search.py \
    --index vector_db/chunks.json \
    --query "$QUERY" \
    --top-k "$TOP_K" \
    --backend tfidf