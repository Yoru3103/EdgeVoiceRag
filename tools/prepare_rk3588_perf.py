#!/usr/bin/env python3
"""Prepare an isolated RK3588 performance bundle; never overwrite a deployment."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile

from run_retrieval_baseline import ROOT, cache_values, check_build, sha256

SYSTEM_LIBS = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
               "libutil.so.1", "libresolv.so.2", "ld-linux-aarch64.so.1", "libanl.so.1"}


def arm64(path):
    header = Path(path).read_bytes()[:20]
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[4:6] != b"\x02\x01":
        raise ValueError("not a little-endian ELF64 file: " + str(path))
    if struct.unpack_from("<H", header, 18)[0] != 183:
        raise ValueError("not an AArch64 binary: " + str(path))


def command(args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def needed(path):
    return re.findall(r"\(NEEDED\).*\[([^]]+)\]", command(["readelf", "-d", str(path)]))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    build, out = args.build_dir.resolve(), args.output.resolve()
    archive = out.with_suffix(".tar.gz")
    if out.exists() or archive.exists():
        parser.error("output directory/archive must not exist")
    cache = cache_values(build / "CMakeCache.txt")
    check_build(cache)
    names = ("board_voice_assistant", "retrieval_benchmark",
             "bge_retrieval_profile", "voice_performance_summary")
    for name in names:
        arm64(build / name)
    compiler = cache["CMAKE_CXX_COMPILER"] if "CMAKE_CXX_COMPILER" in cache else None
    if not compiler:
        candidates = sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
        text = candidates[-1].read_text()
        compiler = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', text).group(1)
    sysroot = Path(command([compiler, "-print-sysroot"]))
    search = []
    for key in ("ONNXRUNTIME_LIBRARY", "SHERPA_ONNX_C_LIBRARY", "RKLLM_RUNTIME_LIBRARY"):
        if cache.get(key):
            search.append(Path(cache[key]).parent)
    search += [sysroot / "lib", sysroot / "usr/lib"]
    out.mkdir(parents=True, exist_ok=False)
    (out / "bin").mkdir()
    (out / "lib").mkdir()
    binaries = {}
    queue = []
    for name in names:
        target = out / "bin" / name
        shutil.copy2(build / name, target)
        binaries[name] = sha256(target)
        queue.append(target)
    libraries = {}
    copied = {}
    while queue:
        binary = queue.pop()
        for name in needed(binary):
            if name in SYSTEM_LIBS or name in libraries:
                continue
            if not re.fullmatch(r"[A-Za-z0-9_.+\-]+", name):
                raise ValueError("unsafe DT_NEEDED: " + name)
            found = next((folder / name for folder in search if (folder / name).is_file()), None)
            if found is None:
                raise ValueError("cannot resolve ARM library: " + name)
            arm64(found)
            real = found.resolve()
            target = out / "lib" / name
            if real in copied:
                target.symlink_to(copied[real])
            else:
                shutil.copy2(found, target)
                copied[real] = name
                queue.append(target)
            libraries[name] = sha256(target)
    assets = {
        "vector_db/chunks.json": ROOT / "vector_db/chunks.json",
        "benchmarks/retrieval_cases.json": ROOT / "benchmarks/retrieval_cases.json",
        "models/embedding/bge-small-zh-v1.5/model.onnx": Path(cache["BGE_MODEL_DIR"]) / "model.onnx",
        "models/embedding/bge-small-zh-v1.5/tokenizer.json": Path(cache["BGE_MODEL_DIR"]) / "tokenizer.json",
        "vector_db/bge-small-zh-v1.5/index_meta.json": Path(cache["BGE_INDEX_DIR"]) / "index_meta.json",
        "vector_db/bge-small-zh-v1.5/embeddings.f32": Path(cache["BGE_INDEX_DIR"]) / "embeddings.f32",
        "config/board_rk3588.conf": ROOT / "config/board_rk3588.conf",
        "voice_input/air_conditioner.wav": ROOT / "voice_input/air_conditioner.wav",
        "tools/run_retrieval_baseline.py": ROOT / "tools/run_retrieval_baseline.py"
    }
    for name, source in assets.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    sources = [p for folder in ("src", "include", "tools", "cmake") for p in (ROOT / folder).rglob("*")
               if p.is_file() and (p.suffix in {".cpp", ".h", ".py", ".cmake"} or p.name == "CMakeLists.txt")]
    sources += [ROOT / "CMakeLists.txt"]
    provenance = {
        "version": 1, "type": "edge_cross_build",
        "git_head": command(["git", "rev-parse", "HEAD"]),
        "git_status": command(["git", "status", "--short"]),
        "build": cache, "compiler_version": command([compiler, "--version"]),
        "source_sha256": {str(p.relative_to(ROOT)): sha256(p) for p in sorted(sources)},
        "binaries_sha256": binaries, "bundled_libraries_sha256": libraries,
        "assets_sha256": {name: sha256(out / name) for name in assets},
        "voice_assets": "ASR/TTS/VAD/RKLLM reuse existing board deployment; not copied in this bundle"
    }
    (out / "build-manifest.json").write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n")
    files = sorted(p for p in out.rglob("*") if p.is_file())
    (out / "bundle.sha256").write_text("".join(
        sha256(p) + "  " + str(p.relative_to(out)) + "\n" for p in files))
    with tarfile.open(archive, "w:gz", compresslevel=1) as tar:
        tar.add(out, arcname=out.name)
    result = {"bundle": str(out), "archive": str(archive), "archive_sha256": sha256(archive),
              "archive_bytes": archive.stat().st_size, "library_count": len(libraries),
              "binaries": binaries}
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
