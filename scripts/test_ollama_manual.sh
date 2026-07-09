#!/usr/bin/env bash

set -e

EXPECTED_ENV="edge-rag"
MODEL="${1:-qwen2.5:3b}"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"

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

PYTHONPATH=python/rag python - <<PY
from llm_generator import create_llm_generator

generator = create_llm_generator(
    backend="ollama",
    model="$MODEL",
    base_url="$OLLAMA_URL",
)

query = "空调怎么打开"
contexts = [
    "空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。"
]

result = generator.generate(query=query, contexts=contexts)

print("Backend:")
print(result.backend)

print("\\nPrompt:")
print(result.prompt)

print("\\nAnswer:")
print(result.answer)
PY