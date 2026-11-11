#!/usr/bin/env python3
"""Manual DHT11 A/B probe: baseline versus a temporary CPU latency request.

Uses only the standard library. It does not load modules or write CPU governors.
The QoS request exists only while its character-device file descriptor is open.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import stat
import struct
import sys
import time

QOS_DEVICE = Path("/dev/cpu_dma_latency")


def emit(record):
    print(json.dumps(record, ensure_ascii=False, allow_nan=False), flush=True)


def optional_text(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def environment():
    cpu = Path("/sys/devices/system/cpu")
    frequencies = {}
    for policy in sorted((cpu / "cpufreq").glob("policy*")):
        frequencies[policy.name] = {
            name: optional_text(policy / name)
            for name in ("scaling_governor", "scaling_cur_freq", "cpuinfo_cur_freq")}
    idle = {}
    for state in sorted((cpu / "cpu0/cpuidle").glob("state*")):
        idle[state.name] = {name: optional_text(state / name)
                            for name in ("name", "latency", "disable")}
    return {"cpufreq": frequencies, "cpu0_idle_states": idle}


def find_device(root):
    matches = [d for d in sorted(Path(root).glob("iio:device*"))
               if optional_text(d / "name") == "edge_dht11"]
    if len(matches) != 1:
        raise ValueError("expected exactly one edge_dht11 IIO device, found " + str(len(matches)))
    path = matches[0] / "in_temp_input"
    if not path.is_file():
        raise ValueError("temperature attribute missing: " + str(path))
    return path


def require_qos_device(path=QOS_DEVICE):
    if not stat.S_ISCHR(path.stat().st_mode):
        raise ValueError("CPU latency interface is not a character device: " + str(path))


@contextmanager
def cpu_latency_request(enabled):
    if not enabled:
        yield
        return
    require_qos_device()
    # No O_CREAT: never create or truncate a file under /dev.
    fd = os.open(QOS_DEVICE, os.O_RDWR | os.O_CLOEXEC)
    try:
        data = struct.pack("=i", 0)
        if os.write(fd, data) != len(data):
            raise OSError("short CPU latency request write")
        yield
    finally:
        os.close(fd)


def read_temperature(path):
    value = int(path.read_text().strip())
    if not 0 <= value <= 80000:
        raise ValueError("temperature outside current driver's supported range")
    return value


def run_mode(path, mode, samples, interval, output=emit):
    if mode not in ("baseline", "qos0"):
        raise ValueError("unknown mode")
    success = 0
    before = environment()
    with cpu_latency_request(mode == "qos0"):
        output({"type": "mode_start", "mode": mode,
                "qos_requested_us": 0 if mode == "qos0" else None})
        for index in range(1, samples + 1):
            # Also wait before the first read so a previous mode's 2s cache expires.
            time.sleep(interval)
            start = time.monotonic_ns()
            record = {"type": "sample", "mode": mode, "sample": index,
                      "ok": False, "temperature_mC": None}
            try:
                record["temperature_mC"] = read_temperature(path)
                record["ok"] = True
                success += 1
            except (OSError, ValueError) as error:
                record.update(error=str(error), errno=getattr(error, "errno", None))
            record["elapsed_ms"] = (time.monotonic_ns() - start) / 1_000_000
            output(record)
        after = environment()
    summary = {"type": "summary", "mode": mode, "samples": samples,
               "success": success, "failure": samples - success,
               "environment_before": before, "environment_after": after}
    if mode == "qos0":
        summary["qos_request_released"] = True
    output(summary)
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("compare", "baseline", "qos0"), default="compare")
    parser.add_argument("--samples", type=int, default=5, help="reads per mode")
    parser.add_argument("--interval", type=float, default=3.0, help="seconds before each read")
    parser.add_argument("--iio-root", type=Path, default=Path("/sys/bus/iio/devices"))
    args = parser.parse_args(argv)
    if args.samples < 1 or not math.isfinite(args.interval) or args.interval < 2.0:
        parser.error("samples must be positive and interval at least 2 seconds")
    try:
        path = find_device(args.iio_root)
        if args.mode != "baseline":
            require_qos_device()  # Fail before spending time on baseline if unavailable.
        source = Path(__file__)
        emit({"type": "config", "version": 1, "device": str(path),
              "kernel": platform.release(), "module_version":
              optional_text("/sys/module/edge_dht11/version"),
              "mode": args.mode, "samples_per_mode": args.samples,
              "interval_seconds": args.interval,
              "script_sha256": hashlib.sha256(source.read_bytes()).hexdigest()
              if source.is_file() else None})
        modes = ("baseline", "qos0") if args.mode == "compare" else (args.mode,)
        for mode in modes:
            run_mode(path, mode, args.samples, args.interval)
        return 0  # Completed experiment; individual failures remain in samples/summaries.
    except KeyboardInterrupt:
        emit({"type": "error", "error": "interrupted; open descriptors are released"})
        return 130
    except (OSError, ValueError) as error:
        emit({"type": "error", "error": str(error)})
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
