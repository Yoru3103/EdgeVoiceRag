#!/usr/bin/env python3
"""Capture one Linux/WSL retrieval run, immutable inputs, raw samples and RSS."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def cache_values(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line and ":" in line.split("=")[0]:
            key, value = line.split("=", 1)
            result[key.split(":", 1)[0]] = value
    return result


def check_build(cache):
    if cache.get("CMAKE_BUILD_TYPE") not in {"Release", "RelWithDebInfo"}:
        raise ValueError("baseline requires Release or RelWithDebInfo")
    if any(cache.get(k, "OFF").upper() in {"ON", "TRUE", "1", "YES"}
           for k in ("EDGE_ENABLE_ASAN", "EDGE_ENABLE_UBSAN", "EDGE_ENABLE_TSAN")):
        raise ValueError("baseline must use a non-sanitized build")
    if any("-fsanitize" in value for key, value in cache.items() if "FLAGS" in key):
        raise ValueError("sanitizer flags found in build cache")


def run_text(args):
    return subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout.strip()


def number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise ValueError("sample duration must be finite and nonnegative")
    return value


def distribution(values):
    values = sorted(values)
    if not values:
        raise ValueError("no measured samples")
    return {"sample_count": len(values), "mean_ms": statistics.fmean(values),
            "p50_ms": values[max(0, math.ceil(len(values) * .50) - 1)],
            "p95_ms": values[max(0, math.ceil(len(values) * .95) - 1)],
            "max_ms": values[-1], "percentile_method": "nearest_rank"}


def parse_output(text, mode, case_ids, runs):
    if mode == "benchmark":
        samples = [json.loads(line[len("LATENCY_SAMPLE "):]) for line in text.splitlines()
                   if line.startswith("LATENCY_SAMPLE ")]
        summaries = [json.loads(line[len("METRICS_JSON "):]) for line in text.splitlines()
                     if line.startswith("METRICS_JSON ")]
        if len(summaries) != 1:
            raise ValueError("expected one METRICS_JSON")
        summary = summaries[0]
        for sample in samples:
            number(sample["elapsed_us"])
    else:
        records = [json.loads(line) for line in text.splitlines() if line.strip()]
        configs = [r for r in records if r.get("type") == "dense_profile_config"]
        if len(configs) != 1:
            raise ValueError("expected one dense_profile_config")
        samples = [r for r in records if r.get("type") == "dense_profile_sample"]
        fields = ("lock_wait_ms", "prepare_ms", "inference_ms", "postprocess_ms",
                  "embedding_total_ms", "index_search_ms", "mapping_ms", "total_ms")
        summary = {"configuration": configs[0], "stages": {
            field: distribution([number(s[field]) for s in samples]) for field in fields}}
        if any(s["token_count"] <= 0 for s in samples):
            raise ValueError("invalid token count")
    if any(s.get("version") != 1 for s in samples):
        raise ValueError("unsupported or missing raw sample version")
    expected = {(i, case_id) for i in range(runs) for case_id in case_ids}
    found = {(s["run"], s["case_id"]) for s in samples}
    if len(samples) != len(expected) or found != expected:
        raise ValueError("missing, duplicate or unexpected run/case samples")
    return samples, summary


def timer_info():
    timer = Path("/usr/bin/time")
    if not timer.is_file():
        raise ValueError("time is required at /usr/bin/time")
    version = subprocess.run([str(timer), "--version"], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if version.returncode == 0 and "GNU" in version.stdout:
        implementation, text = "GNU time", version.stdout.strip()
    else:
        help_result = subprocess.run([str(timer), "--help"], text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if "BusyBox" not in help_result.stdout:
            raise ValueError("only GNU time and BusyBox time are supported")
        implementation, text = "BusyBox time", help_result.stdout.splitlines()[0]
    return {"path": str(timer), "implementation": implementation,
            "version": text, "sha256": sha256(timer)}


def board_environment():
    def read(path):
        try:
            return Path(path).read_text(errors="replace").strip()
        except OSError:
            return None
    thermal = {}
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        thermal[zone.name] = {"type": read(zone / "type"), "temp_millicelsius": read(zone / "temp")}
    frequency = {}
    for policy in sorted(Path("/sys/devices/system/cpu/cpufreq").glob("policy*")):
        frequency[policy.name] = {name: read(policy / name) for name in
            ("scaling_governor", "scaling_cur_freq", "scaling_min_freq", "scaling_max_freq")}
    return {"compatible": (read("/proc/device-tree/compatible") or "").split("\0"),
            "thermal": thermal, "cpu_frequency": frequency,
            "npu_driver_version": read("/sys/module/rknpu/version")
                                  or read("/sys/kernel/debug/rknpu/version")}


def capture(command, output):
    # Linux preserves resource high-water marks across exec. Launching the
    # benchmark directly from Python can therefore include Python's inherited
    # RSS. GNU time launches the native command from its small process and
    # reports that command's usage separately.
    timer = Path("/usr/bin/time")
    info = timer_info()
    usage_path = output / "resource.tsv"
    timed = [str(timer), "-f", "EDGE_USAGE\t%U\t%S\t%M", "-o", str(usage_path), "--", *command]
    start = time.monotonic()
    environment = dict(os.environ, LC_ALL="C")
    with (output / "stdout.log").open("w") as stdout, (output / "stderr.log").open("w") as stderr:
        child = subprocess.Popen(timed, cwd=ROOT, stdout=stdout, stderr=stderr,
                                 env=environment, start_new_session=True)
        try:
            child.wait()
        except BaseException:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
            raise
    records = [line.split("\t") for line in usage_path.read_text().splitlines()
               if line.startswith("EDGE_USAGE\t")]
    if len(records) != 1 or len(records[0]) != 4:
        raise RuntimeError("GNU time did not emit the resource record")
    _, user, system, rss = records[0]
    return {"exit_code": child.returncode, "wall_seconds": time.monotonic() - start,
            "user_cpu_seconds": number(float(user)), "system_cpu_seconds": number(float(system)),
            "max_rss_kib": int(rss), "rss_scope": "native_command_including_load_warmup_logging",
            "resource_source": info["implementation"], "cpu_time_resolution_seconds": 0.01}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, help="cross-build provenance for a deployed bundle")
    parser.add_argument("--mode", choices=("benchmark", "profile"), default="benchmark")
    parser.add_argument("--backend", choices=("bm25", "dense", "hybrid"), default="dense")
    parser.add_argument("--chunks", type=Path, default=ROOT / "vector_db/chunks.json")
    parser.add_argument("--cases", type=Path, default=ROOT / "benchmarks/retrieval_cases.json")
    parser.add_argument("--model-dir", type=Path, default=ROOT / "models/embedding/bge-small-zh-v1.5")
    parser.add_argument("--index-dir", type=Path, default=ROOT / "vector_db/bge-small-zh-v1.5")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--top-k", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=1, help="profile warmup passes; benchmark uses its fixed passes")
    parser.add_argument("--threads", type=int, default=4, help="profile intra-op threads; benchmark uses runtime defaults")
    parser.add_argument("--filter", action="store_true")
    args = parser.parse_args(argv)
    if not sys.platform.startswith("linux") or not hasattr(os, "wait4"):
        parser.error("run in Linux/WSL")
    if min(args.runs, args.top_k, args.threads) <= 0 or args.warmup < 0:
        parser.error("runs/top-k/threads must be positive; warmup must be nonnegative")
    if args.mode == "profile" and (args.backend != "dense" or args.filter):
        parser.error("profile measures unfiltered Dense only")
    if args.mode == "benchmark" and (args.warmup != 1 or args.threads != 4):
        parser.error("benchmark warmup/threads are fixed; use profile for configurable stage sampling")
    output = args.output.resolve()
    manifest = None
    owns_output = False
    try:
        build = args.build_dir.resolve()
        provenance = None
        if args.build_manifest:
            provenance = json.loads(args.build_manifest.resolve().read_text())
            if provenance.get("version") != 1 or provenance.get("type") != "edge_cross_build":
                raise ValueError("unsupported cross-build manifest")
            cache = provenance["build"]
        else:
            cache = cache_values(build / "CMakeCache.txt")
        check_build(cache)
        executable = build / ("bge_retrieval_profile" if args.mode == "profile" else "retrieval_benchmark")
        if not executable.is_file():
            raise ValueError("build the required tool first: " + str(executable))
        if provenance and sha256(executable) != provenance["binaries_sha256"].get(executable.name):
            raise ValueError("deployed binary does not match build manifest")
        chunks, cases = args.chunks.resolve(), args.cases.resolve()
        raw_cases = json.loads(cases.read_text())
        if not isinstance(raw_cases, list) or not raw_cases:
            raise ValueError("cases must be a nonempty array")
        ids = [c["id"] for c in raw_cases]
        if len(set(ids)) != len(ids) or any(not isinstance(i, str) or not i for i in ids):
            raise ValueError("case ids must be unique nonempty strings")
        model, index = args.model_dir.resolve(), args.index_dir.resolve()
        inputs = [executable, chunks, cases, args.build_manifest.resolve() if provenance else build / "CMakeCache.txt"]
        if args.backend != "bm25":
            inputs += [model / "model.onnx", model / "tokenizer.json",
                       index / "index_meta.json", index / "embeddings.f32"]
        input_hashes = {str(p): sha256(p) for p in inputs}
        sources = [p for folder in ("src", "include", "tools", "cmake")
                   for p in (ROOT / folder).rglob("*")
                   if p.is_file() and (p.suffix in {".cpp", ".h", ".py", ".cmake"} or p.name == "CMakeLists.txt")]
        if not provenance:
            sources += [ROOT / "CMakeLists.txt"]
        if provenance:
            source_hashes = provenance["source_sha256"]
            local_source_hashes = {name: sha256(ROOT / name) for name in source_hashes if (ROOT / name).is_file()}
            if any(source_hashes[name] != value for name, value in local_source_hashes.items()):
                raise ValueError("deployed source/script does not match build manifest")
        else:
            source_hashes = {str(p.relative_to(ROOT)): sha256(p) for p in sorted(sources)}
            local_source_hashes = source_hashes
        libraries = {}
        for library in run_text(["ldd", str(executable)]).splitlines():
            for token in library.split():
                if token.startswith("/") and Path(token).is_file():
                    libraries[token] = sha256(token)
        if args.mode == "profile":
            command = [str(executable), str(chunks), str(cases), str(model), str(index),
                       str(args.top_k), str(args.runs), str(args.warmup), str(args.threads)]
        else:
            command = [str(executable), args.backend, str(chunks), str(cases), str(args.top_k), str(args.runs)]
            if args.backend != "bm25":
                command += [str(model), str(index)]
            if args.filter:
                command += ["--filter"]
            command += ["--samples"]
        manifest = {"version": 1, "status": "running", "mode": args.mode,
                    "backend": args.backend, "command": command,
                    "git_head": provenance["git_head"] if provenance else run_text(["git", "rev-parse", "HEAD"]),
                    "git_status": provenance["git_status"] if provenance else run_text(["git", "status", "--short"]),
                    "host": {"system": platform.platform(), "machine": platform.machine(),
                             "cpu_info": Path("/proc/cpuinfo").read_text(),
                             "mem_info": Path("/proc/meminfo").read_text()},
                    "build": cache,
                    "compiler_version": provenance["compiler_version"] if provenance else run_text([cache["CMAKE_CXX_COMPILER"], "--version"]),
                    "source_origin": "cross_build_manifest" if provenance else "local_working_tree",
                    "source_files_present": sorted(local_source_hashes),
                    "board_environment_before": board_environment(),
                    "cpu_affinity": sorted(os.sched_getaffinity(0)),
                    "input_sha256": input_hashes, "source_sha256": source_hashes,
                    "shared_library_sha256": libraries,
                    "timer": timer_info(),
                    "process_cold_start": True, "filesystem_cache_flushed": False,
                    "warmup_policy": "profile explicit passes" if args.mode == "profile"
                                     else "one quality pass, then one warmup pass before measurement"}
        # Refuse to overwrite any prior run or local assets.
        output.mkdir(parents=True, exist_ok=False)
        owns_output = True
        (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        manifest["resource_usage"] = capture(command, output)
        if manifest["resource_usage"]["exit_code"] != 0:
            raise RuntimeError("child failed; inspect stderr.log")
        for name, value in input_hashes.items():
            if sha256(name) != value:
                raise RuntimeError("input changed during run: " + name)
        if any(sha256(ROOT / name) != value for name, value in local_source_hashes.items()):
            raise RuntimeError("source changed during run")
        if any(sha256(name) != value for name, value in libraries.items()):
            raise RuntimeError("shared library changed during run")
        manifest["board_environment_after"] = board_environment()
        samples, summary = parse_output((output / "stdout.log").read_text(), args.mode, ids, args.runs)
        (output / "samples.jsonl").write_text("".join(json.dumps(s, ensure_ascii=False) + "\n" for s in samples))
        (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
        manifest["sample_count"] = len(samples)
        manifest["status"] = "complete"
        (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps({"output": str(output), "sample_count": len(samples),
                          "max_rss_kib": manifest["resource_usage"]["max_rss_kib"]}, ensure_ascii=False))
        return 0
    except (Exception, KeyboardInterrupt) as error:
        # Only update a manifest created by this invocation.
        if owns_output and manifest is not None:
            manifest["status"] = "failed"
            manifest["error"] = str(error)
            (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print("baseline failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
