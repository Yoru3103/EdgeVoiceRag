import queue
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Optional

import numpy as np
import sherpa_onnx
import sounddevice as sd

from rag.audio_player import (
    AudioPlayer,
    PlaybackResult,
)
from rag.microphone_io import (
    MicrophoneDevice,
)

@dataclass
class BargeInResult:
    interrupted: bool
    detection_ms: float
    speech_frames: int
    playback_result: PlaybackResult

class BargeInDetector:
    def __init__(
        self,
        model_path: str,
        device: MicrophoneDevice = None,
        sample_rate: int = 16000,
        threshold: float = 0.6,
        min_speech_duration: float = 0.15,
        required_speech_frames: int = 3,
        ignore_start_seconds: float = 0.3,
    ) -> None:
        if sample_rate != 16000:
            raise ValueError(
                "Silero VAD requires 16000 Hz"
            )

        if not 0 < threshold < 1:
            raise ValueError(
                "threshold must be between 0 and 1"
            )

        if min_speech_duration <= 0:
            raise ValueError(
                "min_speech_duration must be "
                "positive"
            )

        if required_speech_frames <= 0:
            raise ValueError(
                "required_speech_frames must be "
                "positive"
            )

        if ignore_start_seconds < 0:
            raise ValueError(
                "ignore_start_seconds cannot be "
                "negative"
            )

        self.model_path = Path(model_path)
        self.device = device
        self.sample_rate = sample_rate
        self.threshold = threshold
        self.min_speech_duration = (
            min_speech_duration
        )
        self.required_speech_frames = (
            required_speech_frames
        )
        self.ignore_start_seconds = (
            ignore_start_seconds
        )
        self.window_size = 512

        if not self.model_path.is_file():
            raise FileNotFoundError(
                f"VAD model not found: "
                f"{self.model_path}"
            )

        self.vad = self._create_vad()

    def _create_vad(
        self,
    ) -> sherpa_onnx.VoiceActivityDetector:
        config = sherpa_onnx.VadModelConfig()

        config.silero_vad.model = str(
            self.model_path
        )

        config.silero_vad.threshold = (
            self.threshold
        )

        config.silero_vad.min_silence_duration = (
            0.2
        )

        config.silero_vad.min_speech_duration = (
            self.min_speech_duration
        )

        config.silero_vad.max_speech_duration = (
            5.0
        )

        config.silero_vad.window_size = (
            self.window_size
        )

        config.sample_rate = self.sample_rate
        config.num_threads = 1

        return sherpa_onnx.VoiceActivityDetector(
            config,
            buffer_size_in_seconds=5,
        )

    def wait_for_interrupt(
        self,
        player: AudioPlayer,
    ) -> BargeInResult:
        if not player.is_playing():
            raise RuntimeError(
                "Audio player is not playing"
            )

        self.vad.reset()

        audio_queue: queue.Queue[
            np.ndarray
        ] = queue.Queue()

        def callback(
            indata: np.ndarray,
            frames: int,
            time_info: object,
            status: sd.CallbackFlags,
        ) -> None:
            del frames
            del time_info

            if status:
                print(
                    f"[WARNING] Barge-in audio "
                    f"callback: {status}"
                )

            audio_queue.put(
                indata[:, 0].copy()
            )

        start = perf_counter()
        speech_frames = 0

        with sd.InputStream(
            samplerate=self.sample_rate,
            channels=1,
            dtype="float32",
            blocksize=self.window_size,
            device=self.device,
            callback=callback,
        ):
            while player.is_playing():  # 只在播放时监听
                try:
                    samples = audio_queue.get(
                        timeout=0.1
                    )
                except queue.Empty:
                    continue

                elapsed = perf_counter() - start

                if (
                    elapsed
                    < self.ignore_start_seconds # 播放开始一段时间不检测，避免刚启动时的噪声
                ):
                    continue

                self.vad.accept_waveform(
                    samples
                )

                if self.vad.is_speech_detected():
                    speech_frames += 1
                else:
                    speech_frames = 0

                if (
                    speech_frames
                    >= self.required_speech_frames  # 连续检测到多帧才算检测到语音
                ):
                    detection_ms = (
                        perf_counter() - start
                    ) * 1000

                    playback_result = (
                        player.stop()
                    )

                    if playback_result is None:
                        raise RuntimeError(
                            "Playback stopped before "
                            "barge-in result was built"
                        )

                    return BargeInResult(
                        interrupted=True,
                        detection_ms=detection_ms,
                        speech_frames=(
                            speech_frames
                        ),
                        playback_result=(
                            playback_result
                        ),
                    )

        playback_result = player.wait()

        detection_ms = (
            perf_counter() - start
        ) * 1000

        return BargeInResult(
            interrupted=False,
            detection_ms=detection_ms,
            speech_frames=speech_frames,
            playback_result=playback_result,
        )