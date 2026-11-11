"""Standard-library checks for the offline retrieval recorder."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("retrieval_baseline", ROOT / "tools/run_retrieval_baseline.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RecorderTests(unittest.TestCase):
    def test_release_and_sanitizer_guard(self):
        module.check_build({"CMAKE_BUILD_TYPE": "Release"})
        for config in [{"CMAKE_BUILD_TYPE": "Debug"},
                       {"CMAKE_BUILD_TYPE": "Release", "EDGE_ENABLE_ASAN": "ON"},
                       {"CMAKE_BUILD_TYPE": "RelWithDebInfo", "CMAKE_CXX_FLAGS": "-fsanitize=undefined"}]:
            with self.assertRaises(ValueError):
                module.check_build(config)

    def benchmark_text(self, samples):
        return "\n".join(["LATENCY_SAMPLE " + json.dumps(s) for s in samples]
                         + ['METRICS_JSON {"backend":"bm25"}'])

    def test_raw_samples_require_exact_run_case_pairs(self):
        samples = [{"version": 1, "run": 0, "case_id": "a", "elapsed_us": 1.0},
                   {"version": 1, "run": 1, "case_id": "a", "elapsed_us": 2.0}]
        found, _ = module.parse_output(self.benchmark_text(samples), "benchmark", ["a"], 2)
        self.assertEqual(len(found), 2)
        for bad in [samples[:1], [samples[0], samples[0]], [dict(samples[0], elapsed_us=-1), samples[1]]]:
            with self.assertRaises(ValueError):
                module.parse_output(self.benchmark_text(bad), "benchmark", ["a"], 2)

    def test_profile_summary_and_zero(self):
        fields = ("lock_wait_ms", "prepare_ms", "inference_ms", "postprocess_ms",
                  "embedding_total_ms", "index_search_ms", "mapping_ms", "total_ms")
        row = {f: 0.0 for f in fields}
        row.update(version=1, type="dense_profile_sample", run=0, case_id="a", token_count=8)
        text = json.dumps({"type": "dense_profile_config"}) + "\n" + json.dumps(row)
        _, summary = module.parse_output(text, "profile", ["a"], 1)
        self.assertEqual(summary["stages"]["total_ms"]["sample_count"], 1)
        self.assertEqual(summary["stages"]["total_ms"]["mean_ms"], 0.0)

    def test_invalid_duration_and_missing_summary(self):
        for value in [True, float("nan"), float("inf"), -1.0]:
            with self.assertRaises(ValueError):
                module.number(value)
        with self.assertRaises(ValueError):
            module.parse_output("", "benchmark", ["a"], 1)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux/WSL recorder")
    def test_exact_child_resource_and_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            usage = module.capture([sys.executable, "-c", "raise SystemExit(7)"], Path(directory))
            self.assertEqual(usage["exit_code"], 7)
            self.assertGreater(usage["max_rss_kib"], 0)
            self.assertEqual(usage["rss_scope"], "native_command_including_load_warmup_logging")

    def fixture(self, root):
        build = root / "build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_COMPILER:FILEPATH=" + sys.executable + "\n")
        shutil.copy2("/bin/true", build / "retrieval_benchmark")
        chunks, cases = root / "chunks.json", root / "cases.json"
        chunks.write_text("[]")
        cases.write_text('[{"id":"a","query":"q","category":"test","relevant_chunk_ids":[]}]')
        return ["--build-dir", str(build), "--backend", "bm25",
                "--chunks", str(chunks), "--cases", str(cases), "--runs", "1"]

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux/WSL recorder")
    def test_existing_output_is_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.fixture(root)
            out = root / "existing"
            out.mkdir()
            target = out / "manifest.json"
            target.write_text('{"owned_by":"previous_run"}')
            with contextlib.redirect_stderr(io.StringIO()):
                code = module.main(args + ["--output", str(out)])
            self.assertEqual(code, 1)
            self.assertEqual(target.read_text(), '{"owned_by":"previous_run"}')

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux/WSL recorder")
    def test_bad_child_output_keeps_failed_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.fixture(root)
            out = root / "new-run"
            with contextlib.redirect_stderr(io.StringIO()):
                code = module.main(args + ["--output", str(out)])
            self.assertEqual(code, 1)
            manifest = json.loads((out / "manifest.json").read_text())
            self.assertEqual(manifest["status"], "failed")
            self.assertEqual(manifest["resource_usage"]["exit_code"], 0)
            self.assertTrue((out / "stdout.log").exists())


    def test_busybox_timer_detected(self):
        from unittest.mock import patch
        from subprocess import CompletedProcess
        replies = [CompletedProcess([], 1, "unrecognized option"),
                   CompletedProcess([], 0, "BusyBox v1.37.0 multi-call binary.\nUsage: time")]
        with patch.object(module.subprocess, "run", side_effect=replies), \
             patch.object(module, "sha256", return_value="timer-hash"):
            result = module.timer_info()
        self.assertEqual(result["implementation"], "BusyBox time")
        self.assertEqual(result["sha256"], "timer-hash")

    def test_unknown_timer_rejected(self):
        from unittest.mock import patch
        from subprocess import CompletedProcess
        with patch.object(module.subprocess, "run", return_value=CompletedProcess([], 1, "unknown")):
            with self.assertRaises(ValueError):
                module.timer_info()

    def prebuilt_fixture(self, root):
        args = self.fixture(root)
        (root / "build/CMakeCache.txt").unlink()
        manifest = root / "cross-build.json"
        manifest.write_text(json.dumps({
            "type": "edge_cross_build", "version": 1,
            "build": {"CMAKE_BUILD_TYPE": "Release"},
            "binaries_sha256": {"retrieval_benchmark": module.sha256(root / "build/retrieval_benchmark")},
            "source_sha256": {}, "git_head": "archived-head", "git_status": "archived-status",
            "compiler_version": "archived-cross-compiler"}))
        return args + ["--build-manifest", str(manifest)]

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux/WSL recorder")
    def test_prebuilt_success_needs_no_native_git_or_compiler(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.prebuilt_fixture(root)
            out = root / "result"
            def fake_capture(command, folder):
                samples = [{"version": 1, "run": 0, "case_id": "a", "elapsed_us": 0.0}]
                (folder / "stdout.log").write_text(self.benchmark_text(samples))
                (folder / "stderr.log").write_text("")
                return {"exit_code": 0, "max_rss_kib": 42}
            with patch.object(module, "ROOT", root), \
                 patch.object(module, "capture", side_effect=fake_capture), \
                 patch.object(module, "timer_info", return_value={"implementation": "BusyBox time"}), \
                 patch.object(module, "run_text", return_value="") as native, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.main(args + ["--output", str(out)]), 0)
            self.assertEqual([c.args[0][0] for c in native.call_args_list], ["ldd"])
            data = json.loads((out / "manifest.json").read_text())
            self.assertEqual(data["source_origin"], "cross_build_manifest")
            self.assertEqual(data["git_head"], "archived-head")
            self.assertEqual(data["status"], "complete")
            self.assertEqual(data["sample_count"], 1)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux/WSL recorder")
    def test_prebuilt_changed_binary_rejected_before_child(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = self.prebuilt_fixture(root)
            with (root / "build/retrieval_benchmark").open("ab") as stream:
                stream.write(b"changed")
            with patch.object(module, "capture") as child, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main(args + ["--output", str(root / "new")]), 1)
            child.assert_not_called()
            self.assertFalse((root / "new").exists())

    def packer(self):
        import importlib.util
        from unittest.mock import patch
        with patch.dict(sys.modules, {"run_retrieval_baseline": module}):
            spec = importlib.util.spec_from_file_location("prepare_rk3588_perf", ROOT / "tools/prepare_rk3588_perf.py")
            packer = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(packer)
        return packer

    def test_arm64_guard_rejects_host_and_truncated_elf(self):
        import struct
        packer = self.packer()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "program"
            header = bytearray(20)
            header[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", header, 18, 183)
            path.write_bytes(header)
            packer.arm64(path)
            struct.pack_into("<H", header, 18, 62)
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                packer.arm64(path)
            path.write_bytes(b"\x7fELF")
            with self.assertRaises(ValueError):
                packer.arm64(path)

    def test_packer_existing_output_is_preserved(self):
        packer = self.packer()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "bundle"
            target.mkdir()
            marker = target / "owned.txt"
            marker.write_text("previous")
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                packer.main(["--build-dir", str(root / "missing"), "--output", str(target)])
            self.assertEqual(marker.read_text(), "previous")


if __name__ == "__main__":
    unittest.main()
