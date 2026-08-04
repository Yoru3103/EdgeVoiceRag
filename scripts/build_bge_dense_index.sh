#!/usr/bin/env bash

set -euo pipefail

project_root="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"

cd "$project_root"

export PYTHONPATH="$project_root/python"

python -m embedding.export_bge_onnx \
    --model-id BAAI/bge-small-zh-v1.5 \
    --output-dir models/embedding/bge-small-zh-v1.5 \
    --max-length 512

python -m embedding.build_dense_index \
    --chunks vector_db/chunks.json \
    --model-dir models/embedding/bge-small-zh-v1.5 \
    --output-dir vector_db/bge-small-zh-v1.5 \
    --batch-size 8 \
    --max-length 512

python -m embedding.verify_dense_index \
    --chunks vector_db/chunks.json \
    --model-dir models/embedding/bge-small-zh-v1.5 \
    --index-dir vector_db/bge-small-zh-v1.5