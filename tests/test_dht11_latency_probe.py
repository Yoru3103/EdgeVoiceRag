"""No board access: test IIO selection, result accounting and QoS fd cleanup."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("dht11_latency_probe", ROOT / "tools/dht11_latency_probe.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ProbeTests(unittest.TestCase):
    def fixture(self, root):
        adc = root / "iio:device0"
        adc.mkdir()
        (adc / "name").write_text("saradc\n")
        sensor = root / "iio:device7"
        sensor.mkdir()
        (sensor / "name").write_text("edge_dht11\n")
        (sensor / "in_temp_input").write_text("31900\n")
        return sensor / "in_temp_input"

    def test_selects_by_name_not_number(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = self.fixture(root)
            self.assertEqual(module.find_device(root), expected)
            (expected.parent / "name").write_text("other")
            with self.assertRaises(ValueError):
                module.find_device(root)

    def test_regular_qos_file_rejected_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cpu_dma_latency"
            path.write_bytes(b"keep")
            with self.assertRaises(ValueError):
                module.require_qos_device(path)
            self.assertEqual(path.read_bytes(), b"keep")

    def test_qos_zero_request_closed_on_interruption(self):
        with patch.object(module, "require_qos_device"), \
             patch.object(module.os, "open", return_value=123) as opening, \
             patch.object(module.os, "write", return_value=4) as writing, \
             patch.object(module.os, "close") as closing:
            with self.assertRaises(KeyboardInterrupt):
                with module.cpu_latency_request(True):
                    raise KeyboardInterrupt()
            opening.assert_called_once_with(module.QOS_DEVICE, module.os.O_RDWR | module.os.O_CLOEXEC)
            writing.assert_called_once_with(123, struct.pack("=i", 0))
            closing.assert_called_once_with(123)

    def test_short_write_still_closes_request(self):
        with patch.object(module, "require_qos_device"), \
             patch.object(module.os, "open", return_value=123), \
             patch.object(module.os, "write", return_value=2), \
             patch.object(module.os, "close") as closing:
            with self.assertRaises(OSError):
                with module.cpu_latency_request(True):
                    self.fail("must not enter sampling after an incomplete request")
            closing.assert_called_once_with(123)

    def test_baseline_never_opens_qos_device(self):
        with patch.object(module.os, "open") as opening:
            with module.cpu_latency_request(False):
                pass
            opening.assert_not_called()

    def test_failures_counted_without_reusing_previous_value(self):
        records = []
        with patch.object(module, "environment", return_value={}), \
             patch.object(module.time, "sleep") as sleeping, \
             patch.object(module, "read_temperature",
                          side_effect=[31900, OSError(110, "timeout"), ValueError("bad")]):
            summary = module.run_mode(Path("unused"), "baseline", 3, 3, records.append)
        self.assertEqual((summary["success"], summary["failure"]), (1, 2))
        samples = [r for r in records if r["type"] == "sample"]
        self.assertEqual([r["temperature_mC"] for r in samples], [31900, None, None])
        self.assertEqual(samples[1]["errno"], 110)
        self.assertEqual(sleeping.call_count, 3)

    def test_compare_order_and_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            output = io.StringIO()
            enabled = []
            @contextlib.contextmanager
            def fake_qos(value):
                enabled.append(value)
                yield
            with patch.object(module, "require_qos_device"), \
                 patch.object(module, "cpu_latency_request", side_effect=fake_qos), \
                 patch.object(module, "environment", return_value={}), \
                 patch.object(module.time, "sleep"), contextlib.redirect_stdout(output):
                self.assertEqual(module.main(["--iio-root", str(root), "--samples", "2"]), 0)
            records = [json.loads(line) for line in output.getvalue().splitlines()]
            summaries = [r for r in records if r["type"] == "summary"]
            self.assertEqual(enabled, [False, True])
            self.assertEqual([r["mode"] for r in summaries], ["baseline", "qos0"])
            self.assertEqual([r["success"] for r in summaries], [2, 2])
            self.assertTrue(summaries[1]["qos_request_released"])


if __name__ == "__main__":
    unittest.main()
