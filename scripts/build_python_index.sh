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

python python/rag/build_index.py \
    --manual docs/vehicle_manual.txt \
    --output vector_db/chunks.json