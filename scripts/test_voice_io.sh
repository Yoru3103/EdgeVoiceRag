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

python python/rag/test_voice_io.py

if [ ! -f "voice_output/mock_answer.txt" ]; then
    echo "[FAIL] voice_output/mock_answer.txt was not generated."
    exit 1
fi

if grep -q "根据车辆手册" "voice_output/mock_answer.txt"; then
    echo "[PASS] Mock voice IO test passed."
else
    echo "[FAIL] Mock TTS output does not contain expected answer."
    echo "Actual output:"
    cat "voice_output/mock_answer.txt"
    exit 1
fi