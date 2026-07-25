#!/usr/bin/env bash

set -euo pipefail

EXPECTED_ENV="edge-rag"

PROJECT_ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

cd "$PROJECT_ROOT"

VAD_MODEL="${VAD_MODEL:-models/vad/silero_vad.onnx}"
ASR_MODEL="${ASR_MODEL:-small}"
TTS_MODEL_DIR="${TTS_MODEL_DIR:-models/tts/vits-melo-tts-zh_en}"
RAG_ENDPOINT="${RAG_ENDPOINT:-tcp://localhost:5556}"
RAG_STREAM_ENDPOINT="${RAG_STREAM_ENDPOINT:-tcp://localhost:5557}"
RAG_CONTROL_ENDPOINT="${RAG_CONTROL_ENDPOINT:-tcp://localhost:5558}"
RAG_TIMEOUT_MS="${RAG_TIMEOUT_MS:-120000}"
RAG_CONTROL_TIMEOUT_MS="${RAG_CONTROL_TIMEOUT_MS:-2000}"
STREAM_SENTENCE_MAX_CHARS="${STREAM_SENTENCE_MAX_CHARS:-60}"

MICROPHONE_DEVICE="${MICROPHONE_DEVICE:-}"
OUTPUT_DEVICE="${OUTPUT_DEVICE:-}"

VAD_THRESHOLD="${VAD_THRESHOLD:-0.25}"
VAD_MIN_SILENCE="${VAD_MIN_SILENCE:-0.8}"
VAD_MAX_WAIT="${VAD_MAX_WAIT:-10}"

TTS_NUM_THREADS="${TTS_NUM_THREADS:-2}"
TTS_SPEED="${TTS_SPEED:-1.0}"

MAX_TURNS="${MAX_TURNS:-0}"


check_environment() {
    if [ -z "${CONDA_DEFAULT_ENV:-}" ]; then
        echo "[ERROR] No conda environment is active."
        echo "Please run:"
        echo "  conda activate $EXPECTED_ENV"
        exit 1
    fi

    if [ "$CONDA_DEFAULT_ENV" != "$EXPECTED_ENV" ]; then
        echo "[ERROR] Current environment: $CONDA_DEFAULT_ENV"
        echo "[ERROR] Expected environment: $EXPECTED_ENV"
        echo "Please run:"
        echo "  conda activate $EXPECTED_ENV"
        exit 1
    fi
}


check_files() {
    if [ ! -f "$VAD_MODEL" ]; then
        echo "[ERROR] VAD model not found:"
        echo "  $VAD_MODEL"
        exit 1
    fi

    if [ ! -d "$TTS_MODEL_DIR" ]; then
        echo "[ERROR] TTS model directory not found:"
        echo "  $TTS_MODEL_DIR"
        exit 1
    fi

    if [ ! -f "$TTS_MODEL_DIR/model.onnx" ]; then
        echo "[ERROR] TTS model not found:"
        echo "  $TTS_MODEL_DIR/model.onnx"
        exit 1
    fi

    if [ ! -f "vector_db/chunks.json" ]; then
        echo "[ERROR] RAG index not found:"
        echo "  vector_db/chunks.json"
        echo "Please build it first:"
        echo "  ./scripts/build_python_index.sh"
        exit 1
    fi
}


print_configuration() {
    echo "Voice assistant configuration"
    echo "  VAD model:          $VAD_MODEL"
    echo "  ASR model:          $ASR_MODEL"
    echo "  TTS model dir:      $TTS_MODEL_DIR"
    echo "  RAG endpoint:       $RAG_ENDPOINT"
    echo "  RAG stream endpoint: $RAG_STREAM_ENDPOINT"
    echo "  RAG control endpoint: $RAG_CONTROL_ENDPOINT"
    echo "  RAG timeout:         $RAG_TIMEOUT_MS ms"
    echo "  RAG control timeout: $RAG_CONTROL_TIMEOUT_MS ms"
    echo "  Sentence max chars:  $STREAM_SENTENCE_MAX_CHARS"
    echo "  Microphone device:  ${MICROPHONE_DEVICE:-default}"
    echo "  Output device:      ${OUTPUT_DEVICE:-default}"
    echo "  VAD threshold:      $VAD_THRESHOLD"
    echo "  VAD min silence:    $VAD_MIN_SILENCE"
    echo "  VAD max wait:       $VAD_MAX_WAIT"
    echo "  TTS threads:        $TTS_NUM_THREADS"
    echo "  TTS speed:          $TTS_SPEED"
    echo "  Max turns:          $MAX_TURNS"
    echo
}


run_assistant() {
    command=(
        python
        -m
        rag.run_voice_assistant
        --vad-model
        "$VAD_MODEL"
        --asr-model
        "$ASR_MODEL"
        --tts-model-dir
        "$TTS_MODEL_DIR"
        --rag-endpoint
        "$RAG_ENDPOINT"
        --vad-threshold
        "$VAD_THRESHOLD"
        --vad-min-silence
        "$VAD_MIN_SILENCE"
        --vad-max-wait
        "$VAD_MAX_WAIT"
        --tts-num-threads
        "$TTS_NUM_THREADS"
        --tts-speed
        "$TTS_SPEED"
        --max-turns
        "$MAX_TURNS"
        --rag-stream-endpoint
        "$RAG_STREAM_ENDPOINT"
        --rag-control-endpoint
        "$RAG_CONTROL_ENDPOINT"
        --rag-timeout-ms
        "$RAG_TIMEOUT_MS"
        --rag-control-timeout-ms
        "$RAG_CONTROL_TIMEOUT_MS"
        --stream-sentence-max-chars
        "$STREAM_SENTENCE_MAX_CHARS"
    )

    if [ -n "$MICROPHONE_DEVICE" ]; then
        command+=(
            --microphone-device
            "$MICROPHONE_DEVICE"
        )
    fi

    if [ -n "$OUTPUT_DEVICE" ]; then
        command+=(
            --output-device
            "$OUTPUT_DEVICE"
        )
    fi

    echo "[INFO] Starting voice assistant..."
    echo

    PYTHONPATH=python "${command[@]}"
}


check_environment
check_files
print_configuration
run_assistant
