from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as file:
        while True:
            block = file.read(1024 * 1024)

            if not block:
                break

            digest.update(block)

    return digest.hexdigest()


def load_json(path: Path):
    with path.open(
        "r",
        encoding="utf-8",
    ) as file:
        return json.load(file)


def verify_index(
    chunks_path: Path,
    model_dir: Path,
    index_dir: Path,
) -> None:
    metadata_path = (
        index_dir / "index_meta.json"
    )

    embeddings_path = (
        index_dir / "embeddings.f32"
    )

    model_path = (
        model_dir / "model.onnx"
    )

    tokenizer_path = (
        model_dir / "tokenizer.json"
    )

    for path in (
        chunks_path,
        metadata_path,
        embeddings_path,
        model_path,
        tokenizer_path,
    ):
        if not path.exists():
            raise FileNotFoundError(
                f"required file not found: {path}"
            )

    metadata = load_json(
        metadata_path
    )

    chunks = load_json(
        chunks_path
    )

    if (
        metadata.get("format")
        != "edge_voice_rag_dense_index"
    ):
        raise ValueError(
            "unsupported dense index format"
        )

    if metadata.get("version") != 1:
        raise ValueError(
            "unsupported dense index version"
        )

    rows = int(metadata["rows"])
    dimension = int(metadata["dimension"])

    if rows <= 0 or dimension <= 0:
        raise ValueError(
            "invalid dense index shape"
        )

    if rows != len(chunks):
        raise ValueError(
            "metadata rows do not match chunks"
        )

    chunk_ids = [
        int(chunk["chunk_id"])
        for chunk in chunks
    ]

    if chunk_ids != metadata["chunk_ids"]:
        raise ValueError(
            "chunk ID order does not match metadata"
        )

    expected_bytes = (
        rows *
        dimension *
        np.dtype("<f4").itemsize
    )

    actual_bytes = (
        embeddings_path.stat().st_size
    )

    if actual_bytes != expected_bytes:
        raise ValueError(
            "invalid embeddings file size: "
            f"expected={expected_bytes}, "
            f"actual={actual_bytes}"
        )

    expected_hashes = {
        "chunks_sha256":
            sha256_file(chunks_path),
        "model_sha256":
            sha256_file(model_path),
        "tokenizer_sha256":
            sha256_file(tokenizer_path),
        "embeddings_sha256":
            sha256_file(embeddings_path),
    }

    for field, actual_hash in expected_hashes.items():
        if metadata.get(field) != actual_hash:
            raise ValueError(
                f"SHA-256 mismatch: {field}"
            )

    embeddings = np.fromfile(
        embeddings_path,
        dtype=np.dtype("<f4"),
    ).reshape(rows, dimension)

    if not np.all(np.isfinite(embeddings)):
        raise ValueError(
            "embeddings contain NaN or Inf"
        )

    norms = np.linalg.norm(
        embeddings,
        axis=1,
    )

    if not np.allclose(
        norms,
        np.ones_like(norms),
        atol=1.0e-4,
    ):
        raise ValueError(
            f"embeddings are not normalized: {norms}"
        )

    print("Dense index verification passed.")
    print(f"Rows: {rows}")
    print(f"Dimension: {dimension}")
    print(f"Binary bytes: {actual_bytes}")
    print(
        "Norm range: "
        f"{float(norms.min()):.6f} - "
        f"{float(norms.max()):.6f}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Verify EdgeVoiceRAG dense index files."
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
        "--index-dir",
        type=Path,
        default=Path(
            "vector_db/"
            "bge-small-zh-v1.5"
        ),
    )

    arguments = parser.parse_args()

    verify_index(
        chunks_path=arguments.chunks,
        model_dir=arguments.model_dir,
        index_dir=arguments.index_dir,
    )


if __name__ == "__main__":
    main()
