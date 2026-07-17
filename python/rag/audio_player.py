from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Union,
)

import numpy as np
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
    interrupted: bool = False


@dataclass
class _PlaybackSession:
    audio_path: Path
    samples: np.ndarray
    sample_rate: int
    channels: int
    duration_seconds: float
    device_name: str
    start_time: float


def parse_output_device(
    value: Optional[str],
) -> AudioDevice:
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

        # 内部播放上下文
        self._session: Optional[
            _PlaybackSession
        ] = None

        self._playing = False

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

    def _load_audio(
        self,
        audio_path: Path,
    ) -> _PlaybackSession:
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

        channels = int(samples.shape[1])

        device_name = self.validate_device(
            sample_rate=sample_rate,
            channels=channels,
        )

        duration_seconds = (
            samples.shape[0] / sample_rate
        )

        return _PlaybackSession(
            audio_path=audio_path,
            samples=samples,
            sample_rate=int(sample_rate),
            channels=channels,
            duration_seconds=(
                duration_seconds
            ),
            device_name=device_name,
            start_time=0.0,
        )

    # 开始播放，但不会阻塞主线程
    def start(
        self,
        audio_path: Path,
    ) -> None:
        if self._playing:
            raise RuntimeError(
                "Audio playback is already running"
            )

        session = self._load_audio(audio_path)
        session.start_time = perf_counter()

        self._session = session
        self._playing = True

        try:
            sd.play(
                session.samples,
                samplerate=session.sample_rate,
                device=self.device,
                blocking=False,
            )
        except Exception:
            self._session = None
            self._playing = False
            raise

    def is_playing(self) -> bool:
        if not self._playing:
            return False

        # 通过sd输出流的active状态判断TTS是否还在播放
        try:
            stream = sd.get_stream()
        except Exception as exc:
            print(
                f"[ERROR] Failed to inspect playback stream: "
                f"{exc}"
            )
            self._playing = False
            return False

        if not stream.active:
            self._playing = False
            return False

        return True

    def _build_result(
        self,
        interrupted: bool,
    ) -> PlaybackResult:
        if self._session is None:
            raise RuntimeError(
                "No active playback session"
            )

        playback_ms = (
            perf_counter()
            - self._session.start_time
        ) * 1000

        result = PlaybackResult(
            audio_path=str(
                self._session.audio_path
            ),
            sample_rate=(
                self._session.sample_rate
            ),
            channels=self._session.channels,
            duration_seconds=(
                self._session.duration_seconds
            ),
            playback_ms=playback_ms,
            device=self._session.device_name,
            interrupted=interrupted,
        )

        self._session = None
        self._playing = False

        return result

    def wait(self) -> PlaybackResult:
        if self._session is None:
            raise RuntimeError(
                "No active playback session"
            )

        sd.wait()

        return self._build_result(
            interrupted=False
        )

    def stop(
        self,
    ) -> Optional[PlaybackResult]:
        if self._session is None:
            sd.stop()
            self._playing = False
            return None

        sd.stop()

        return self._build_result(
            interrupted=True
        )

    def play(
        self,
        audio_path: Path,
    ) -> PlaybackResult:
        self.start(audio_path)
        return self.wait()