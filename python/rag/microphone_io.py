from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Any, Dict, List, Optional, Union

import numpy as np
import sounddevice as sd
import soundfile as sf

MicrophoneDevice = Optional[Union[int, str]]

@dataclass
class RecordingResult:
    output_path: str
    sample_rate: int
    channels: int
    duration_seconds: float
    recording_ms: float
    rms: float
    peak: float
    device: str

def list_input_devices() -> List[Dict[str, Any]]:
    devices = sd.query_devices()
    result = []

    for index, device in enumerate(devices):
        max_input_channels = int(
            device["max_input_channels"]
        )

        if max_input_channels <= 0:
            continue

        result.append(
            {
                "index": index,
                "name": str(device["name"]),
                "max_input_channels": (
                    max_input_channels
                ),
                "default_sample_rate": float(
                    device["default_samplerate"]
                ),
                "hostapi": int(
                    device["hostapi"]
                ),
            }
        )

    return result

def parse_device(
    value: Optional[str],
) -> MicrophoneDevice:
    if value is None:
        return None

    stripped = value.strip()

    if not stripped:
        return None

    if stripped.isdigit():
        return int(stripped)

    return stripped


class MicrophoneRecorder:
    def __init__(
        self,
        sample_rate: int = 16000,
        channels: int = 1,
        device: MicrophoneDevice = None,
        minimum_rms: float = 0.001,
    ) -> None:
        if sample_rate <= 0:
            raise ValueError(
                f"sample_rate must be positive: "
                f"{sample_rate}"
            )

        if channels <= 0:
            raise ValueError(
                f"channels must be positive: "
                f"{channels}"
            )

        if minimum_rms < 0:
            raise ValueError(
                f"minimum_rms must be non-negative: "
                f"{minimum_rms}"
            )

        self.sample_rate = sample_rate
        self.channels = channels
        self.device = device
        self.minimum_rms = minimum_rms

    def validate_device(self) -> Dict[str, Any]:
        sd.check_input_settings(
            device=self.device,
            channels=self.channels,
            samplerate=self.sample_rate,
            dtype="float32",
        )

        device_info = sd.query_devices(
            self.device,
            kind="input",
        )

        return {
            "name": str(device_info["name"]),
            "max_input_channels": int(
                device_info[
                    "max_input_channels"
                ]
            ),
            "default_sample_rate": float(
                device_info[
                    "default_samplerate"
                ]
            ),
        }

    def record(
        self,
        duration_seconds: float,
        output_path: Path,
    ) -> RecordingResult:
        if duration_seconds <= 0:
            raise ValueError(
                "duration_seconds must be positive: "
                f"{duration_seconds}"
            )

        if output_path.suffix.lower() != ".wav":
            raise ValueError(
                f"Recording output must be WAV: "
                f"{output_path}"
            )

        device_info = self.validate_device()

        output_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        frame_count = int(
            duration_seconds
            * self.sample_rate
        )

        print(
            f"[INFO] Recording for "
            f"{duration_seconds:.1f} seconds..."
        )

        start = perf_counter()

        samples = sd.rec(
            frames=frame_count,
            samplerate=self.sample_rate,
            channels=self.channels,
            dtype="float32",
            device=self.device,
            blocking=True,
        )

        recording_ms = (
            perf_counter() - start
        ) * 1000

        samples = np.asarray(
            samples,
            dtype=np.float32,
        )

        if samples.size == 0:
            raise ValueError(
                "Microphone returned empty audio"
            )

        rms = float(
            np.sqrt(
                np.mean(
                    np.square(samples)
                )
            )
        )

        peak = float(
            np.max(
                np.abs(samples)
            )
        )

        if rms < self.minimum_rms:
            raise ValueError(
                "Recorded audio is too quiet: "
                f"rms={rms:.6f}, "
                f"minimum={self.minimum_rms:.6f}"
            )

        sf.write(
            str(output_path),
            samples,
            samplerate=self.sample_rate,
            subtype="PCM_16",
        )

        print(
            f"[INFO] Recording saved: "
            f"{output_path}"
        )

        return RecordingResult(
            output_path=str(output_path),
            sample_rate=self.sample_rate,
            channels=self.channels,
            duration_seconds=(
                duration_seconds
            ),
            recording_ms=recording_ms,
            rms=rms,
            peak=peak,
            device=device_info["name"],
        )