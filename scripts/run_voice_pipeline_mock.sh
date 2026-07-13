#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"
QUERY="${1:-空调怎么打开}"
RAG_ENDPOINT="${RAG_ENDPOINT:-tcp://localhost:5556}"
RAG_TIMEOUT_MS="${RAG_TIMEOUT_MS:-5000}"
TTS_OUTPUT="${TTS_OUTPUT:-voice_output/answer.txt}"

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

PYTHONPATH=python python -m rag.voice_pipeline \
    --audio voice_input/mock.wav \
    --mock-asr-text "$QUERY" \
    --rag-endpoint "$RAG_ENDPOINT" \
    --rag-timeout-ms "$RAG_TIMEOUT_MS" \
    --asr-backend mock \
    --tts-backend mock \
    --tts-output "$TTS_OUTPUT"
