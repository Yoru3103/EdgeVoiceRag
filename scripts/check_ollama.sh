#!/usr/bin/env bash

set -e

MODEL="${1:-qwen2.5:3b}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"

echo "[INFO] Checking Ollama service..."
echo "[INFO] Ollama URL: $OLLAMA_URL"
echo "[INFO] Expected model: $MODEL"

echo
echo "[INFO] Checking /api/tags..."

TAGS_OUTPUT=$(curl -s "$OLLAMA_URL/api/tags" || true)

# -z：字符串长度是否为0
if [ -z "$TAGS_OUTPUT" ]; then
    echo "[ERROR] Failed to connect to Ollama at $OLLAMA_URL"
    echo "Please make sure Ollama is running."
    echo
    echo "Try:"
    echo "  ollama serve"
    echo "or:"
    echo "  ollama run $MODEL"
    exit 1
fi

echo "$TAGS_OUTPUT"

echo
echo "[INFO] Checking whether model exists..."

# 表示真实的双引号而不是路径符号
if echo "$TAGS_OUTPUT" | grep -q "\"name\":\"$MODEL\""; then
    echo "[PASS] Model found: $MODEL"
else
    echo "[ERROR] Model not found: $MODEL"
    echo "Please run:"
    echo "  ollama pull $MODEL"
    exit 1
fi

echo
echo "[INFO] Testing /api/generate..."

GENERATE_OUTPUT=$(curl -s "$OLLAMA_URL/api/generate" -d "{
    \"model\": \"$MODEL\",
    \"prompt\":  \"请用一句话回答：你好。\",
    \"stream\": false
}" || true)

if [ -z "$GENERATE_OUTPUT" ]; then
    echo "[ERROR] Ollama generate request failed."
    exit1
fi

echo "$GENERATE_OUTPUT"

if echo "$GENERATE_OUTPUT" | grep -q "\"response\""; then
    echo
    echo "[PASS] Ollama generate API works."
else
    echo
    echo "[ERROR] Ollama response dose not contain response field."
    exit 1
fi