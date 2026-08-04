""" 实现文档向量索引生成器 """
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Dict, List

import numpy as np
import onnxruntime as ort
from transformers import AutoTokenizer

DEFAULT_MODEL_ID = "BAAI/bge-small-zh-v1.5"
DEFAULT_QUERY_INSTRUCTION = (
    "为这个句子生成表示以用于检索相关文章："
)

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    
    with path.open("rb") as file:
        while True:
            block = file.read(1024 * 1024)
            
            if not block:
                break
            
            digest.update(block)
            
    return digest.hexdigest()

def load_chunks(path: Path) -> List[dict]:
    if not path.exists():
        raise FileNotFoundError(
            f"chunks file not found: {path}"
        )
        
    with path.open(
        "r",
        encoding="utf-8",
    ) as file:
        root = json.load(file)
        
    if not isinstance(root, list):
        raise ValueError(
            "chunks file must contain a JSON array"
        )
        
    chunks: List[dict] = []
    loaded_ids = set()
    
    for item in root:
        if not isinstance(item, dict):
            raise ValueError(
                "each chunk must be an object"
            )
            
        for field in (
            "chunk_id",
            "title",
            "content",
        ):
            if field not in item:
                raise ValueError(
                    f"chunk is missing field: {field}"
                )
                
        chunk_id = int(item["chunk_id"])
        title = str(item["title"])
        content = str(item["content"])
        
        text = str(
            item.get(
                "text",
                f"{title}: {content}",
            )
        )
        
        if (
            chunk_id < 0
            or not title
            or not content
            or not text
        ):
            raise ValueError(
                f"invalid chunk: {chunk_id}"
            )
            
        if chunk_id in loaded_ids:
            raise ValueError(
                f"duplicate chunk_id: {chunk_id}"
            )
            
        loaded_ids.add(chunk_id)
        
        chunks.append(
            {
                "chunk_id": chunk_id,
                "title": title,
                "content": content,
                "text": text,
            }
        )
        
    if not chunks:
        raise ValueError("no chunks loaded")
    
    return chunks

def build_session_inputs(
    encoded: Dict[str, np.ndarray],
    session: ort.InferenceSession,
) -> Dict[str, np.ndarray]:
    input_ids = np.asarray(
        encoded["input_ids"],
        dtype=np.int64,
    )
    
    attention_mask = np.asarray(
        encoded["attention_mask"],
        dtype=np.int64,
    )

    token_type_ids = np.asarray(
        encoded.get(
            "token_type_ids",
            np.zeros_like(input_ids),
        ),
        dtype=np.int64,
    )
    
    availiable = {
        "input_ids": input_ids,
        "attention_mask": attention_mask,
        "token_type_ids": token_type_ids,
    }
    
    inputs: Dict[str, np.ndarray] = {}
    
    for input_info in session.get_inputs():
        if input_info.name not in availiable:
            raise RuntimeError(
                "unsupported ONNX input: "
                f"{input_info.name}"
            )
            
        inputs[input_info.name] = availiable[input_info.name]
        
    return inputs

def encode_documents(
    texts: List[str],
    model_dir: Path,
    batch_size: int,
    max_length: int,
) -> np.ndarray:
    if batch_size <= 0:
        raise ValueError("batch_size must be positive")
    
    if max_length <= 0:
        raise ValueError("max_length must be positive")
    
    model_path = model_dir / "model.onnx"
    
    if not model_path.exists():
        raise FileNotFoundError(
            f"ONNX model not found: {model_path}"
        )
        
    tokenizer = AutoTokenizer.from_pretrained(
        model_dir,
        use_fast=True,
        local_files_only=True,
    )
    
    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = (
        ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    )
    
    session = ort.InferenceSession(
        str(model_path),
        sess_options=session_options,
        providers=["CPUExecutionProvider"],
    )
    
    embedding_batches: List[np.ndarray] = []

    for start in range(
        0,
        len(texts),
        batch_size,
    ):
        batch_texts = texts[
            start : start + batch_size
        ]

        encoded = tokenizer(
            batch_texts,
            padding=True,
            truncation=True,
            max_length=max_length,
            return_tensors="np",
        )

        session_inputs = build_session_inputs(
            dict(encoded),
            session,
        )

        batch_embeddings = session.run(
            ["embeddings"],
            session_inputs,
        )[0]

        if batch_embeddings.ndim != 2:
            raise RuntimeError(
                "embedding output must be rank 2, "
                f"got shape {batch_embeddings.shape}"
            )

        if not np.all(
            np.isfinite(batch_embeddings)
        ):
            raise RuntimeError(
                "embedding output contains NaN or Inf"
            )
            
        norms = np.linalg.norm(
            batch_embeddings,
            axis=1,
            keepdims=True,
        )

        if np.any(norms <= 0.0):
            raise RuntimeError(
                "embedding output contains zero vector"
            )

        # 再归一化一次，消除保存前的微小浮点误差。
        batch_embeddings = (
            batch_embeddings / norms
        )

        embedding_batches.append(
            np.asarray(
                batch_embeddings,
                dtype=np.float32,
                order="C",
            )
        )

    return np.concatenate(
        embedding_batches,
        axis=0,
    )


def write_binary_embeddings(
    embeddings: np.ndarray,
    output_path: Path,
) -> None:
    little_endian_embeddings = np.asarray(
        embeddings,
        dtype=np.dtype("<f4"),
        order="C",
    )

    temporary_path = output_path.with_suffix(
        output_path.suffix + ".tmp"
    )

    with temporary_path.open("wb") as file:
        file.write(
            little_endian_embeddings.tobytes(
                order="C"
            )
        )

    temporary_path.replace(output_path)


def build_index(
    chunks_path: Path,
    model_dir: Path,
    output_dir: Path,
    model_id: str,
    batch_size: int,
    max_length: int,
) -> None:
    chunks = load_chunks(
        chunks_path
    )

    texts = [
        str(chunk["text"])
        for chunk in chunks
    ]

    embeddings = encode_documents(
        texts=texts,
        model_dir=model_dir,
        batch_size=batch_size,
        max_length=max_length,
    )

    if embeddings.shape[0] != len(chunks):
        raise RuntimeError(
            "embedding row count does not match chunks"
        )

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    embeddings_path = (
        output_dir / "embeddings.f32"
    )

    metadata_path = (
        output_dir / "index_meta.json"
    )

    write_binary_embeddings(
        embeddings,
        embeddings_path,
    )

    model_path = model_dir / "model.onnx"
    tokenizer_path = (
        model_dir / "tokenizer.json"
    )

    if not tokenizer_path.exists():
        raise FileNotFoundError(
            f"tokenizer.json not found: "
            f"{tokenizer_path}"
        )

    expected_bytes = (
        embeddings.shape[0]
        * embeddings.shape[1]
        * np.dtype("<f4").itemsize
    )

    actual_bytes = embeddings_path.stat().st_size

    if actual_bytes != expected_bytes:
        raise RuntimeError(
            "embedding file size mismatch: "
            f"expected={expected_bytes}, "
            f"actual={actual_bytes}"
        )

    metadata = {
        "format": "edge_voice_rag_dense_index",
        "version": 1,
        "model_id": model_id,
        "pooling": "cls",
        "normalized": True,
        "dtype": "float32_le",
        "dimension": int(
            embeddings.shape[1]
        ),
        "rows": int(
            embeddings.shape[0]
        ),
        "max_length": max_length,
        "query_instruction":
            DEFAULT_QUERY_INSTRUCTION,
        "chunk_ids": [
            int(chunk["chunk_id"])
            for chunk in chunks
        ],
        "chunks_sha256":
            sha256_file(chunks_path),
        "model_sha256":
            sha256_file(model_path),
        "tokenizer_sha256":
            sha256_file(tokenizer_path),
        "embeddings_sha256":
            sha256_file(embeddings_path),
    }

    temporary_metadata_path = (
        metadata_path.with_suffix(
            metadata_path.suffix + ".tmp"
        )
    )

    with temporary_metadata_path.open(
        "w",
        encoding="utf-8",
    ) as file:
        json.dump(
            metadata,
            file,
            ensure_ascii=False,
            indent=2,
        )

        file.write("\n")

    temporary_metadata_path.replace(
        metadata_path
    )

    print("Dense index built successfully.")
    print(f"Rows: {embeddings.shape[0]}")
    print(f"Dimension: {embeddings.shape[1]}")
    print(f"Binary size: {actual_bytes} bytes")
    print(f"Embeddings: {embeddings_path}")
    print(f"Metadata: {metadata_path}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Build normalized BGE document embeddings "
            "from chunks.json."
        )
    )

    parser.add_argument(
        "--chunks",
        type=Path,
        default=Path("vector_db/chunks.json"),
    )

    parser.add_argument(
        "--model-dir",
        type=Path,
        default=Path(
            "models/embedding/"
            "bge-small-zh-v1.5"
        ),
    )

    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(
            "vector_db/"
            "bge-small-zh-v1.5"
        ),
    )

    parser.add_argument(
        "--model-id",
        default=DEFAULT_MODEL_ID,
    )

    parser.add_argument(
        "--batch-size",
        type=int,
        default=8,
    )

    parser.add_argument(
        "--max-length",
        type=int,
        default=512,
    )

    arguments = parser.parse_args()

    build_index(
        chunks_path=arguments.chunks,
        model_dir=arguments.model_dir,
        output_dir=arguments.output_dir,
        model_id=arguments.model_id,
        batch_size=arguments.batch_size,
        max_length=arguments.max_length,
    )


if __name__ == "__main__":
    main()
