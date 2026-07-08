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

run_test() {
    local query="$1"
    local expected="$2"

    echo "Running TF-IDF query: $query"

    local output
    output=$(python python/rag/tfidf_search.py \
        --index vector_db/chunks.json \
        --query "$query" \
        --top-k 3)

    if echo "$output" | grep -q "$expected"; then
        echo "[PASS] $query"
    else
        echo "[FAIL] $query"
        echo "Expected keyword: $expected"
        echo "Actual output:"
        echo "$output"
        exit 1
    fi

    echo
}

run_test "空调怎么打开" "空调系统"
run_test "蓝牙怎么连接" "蓝牙连接"
run_test "胎压报警怎么办" "胎压报警"
run_test "雨刷怎么开" "雨刮控制"
run_test "怎么打开后备箱" "后备箱开启"
run_test "车里太热了怎么开冷气" "空调系统"
run_test "尾门怎么打开" "后备箱开启"
run_test "怎么导航到最近的充电桩" "导航系统"

echo "All Python TF-IDF tests passed."