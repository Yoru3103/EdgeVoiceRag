from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Optional, Union, Any, Dict, List

import sounddevice as sd
import soundfile as sf

AudioDevice = Optional[Union[int, str]]

@dataclass
class PlaybackResult:
    audio_path: str
    sample_rate: int
    channels: int
    duration_seconds: float
    playback_ms: float
    device: str

def parse_output_device(value: Optional[str]) -> AudioDevice:
    if value is None:
        return None

    stripped = value.strip()

    if not stripped:
        return None

    if stripped.isdigit():
        return int(stripped)

    return stripped

def list_output_devices() -> List[Dict[str, Any]]:
    devices = sd.query_devices()
    result = []

    for index, device in enumerate(devices):
        max_output_channels = int(
            device["max_output_channels"]
        )

        if max_output_channels <= 0:
            continue

        result.append(
            {
                "index": index,
                "name": str(device["name"]),
                "max_output_channels": (
                    max_output_channels
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

class AudioPlayer:
    def __init__(
        self,
        device: AudioDevice = None,
    ) -> None:
        self.device = device

    def validate_device(
        self,
        sample_rate: int,
        channels: int,
    ) -> str:
        sd.check_output_settings(
            device=self.device,
            channels=channels,
            samplerate=sample_rate,
            dtype="float32",
        )

        device_info = sd.query_devices(
            self.device,
            kind="output",
        )

        return str(device_info["name"])

    def play(
        self,
        audio_path: Path,
    ) -> PlaybackResult:
        if not audio_path.is_file():
            raise FileNotFoundError(
                f"Playback audio not found: "
                f"{audio_path}"
            )

        samples, sample_rate = sf.read(
            str(audio_path),
            dtype="float32",
            always_2d=True,
        )

        if samples.size == 0:
            raise ValueError(
                f"Playback audio is empty: "
                f"{audio_path}"
            )

        channels = samples.shape[1]

        device_name = self.validate_device(
            sample_rate=sample_rate,
            channels=channels
        )

        duration_seconds = (
            samples.shape[0] / sample_rate
        )

        start = perf_counter()

        sd.play(
            samples,
            samplerate=sample_rate,
            device=self.device,
            blocking=True,
        )

        playback_ms = (
            perf_counter() - start
        ) * 1000

        return PlaybackResult(
            audio_path=str(audio_path),
            sample_rate=sample_rate,
            channels=channels,
            duration_seconds=(
                duration_seconds
            ),
            playback_ms=playback_ms,
            device=device_name,
        )

    def stop(self) -> None:
        sd.stop()