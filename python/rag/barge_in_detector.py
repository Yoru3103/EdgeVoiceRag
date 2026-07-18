import queue
import threading
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Optional

import numpy as np
import sherpa_onnx
import sounddevice as sd
import soundfile as sf

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
    speech_path: Optional[str] = None
    speech_duration_seconds: float = 0.0

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

    def _resample_for_vad(
        self,
        samples: np.ndarray,
        source_sample_rate: int,
    ) -> np.ndarray:
        samples = np.asarray(
            samples,
            dtype=np.float32,
        ).reshape(-1)

        if (
            source_sample_rate
            == self.sample_rate
        ):
            return samples.copy()

        if samples.size == 0:
            return samples

        output_size = max(
            1,
            int(
                round(
                    samples.size
                    * self.sample_rate
                    / source_sample_rate
                )
            ),
        )

        source_positions = np.arange(
            samples.size,
            dtype=np.float64,
        )
        target_positions = np.linspace(
            0,
            samples.size - 1,
            num=output_size,
            dtype=np.float64,
        )

        return np.interp(
            target_positions,
            source_positions,
            samples,
        ).astype(
            np.float32,
            copy=False,
        )

    def play_and_wait(
        self,
        player: AudioPlayer,
        audio_path: Path,
        speech_output_path: Path,
    ) -> BargeInResult:
        self.vad.reset()

        audio_queue: queue.Queue[
            np.ndarray
        ] = queue.Queue()

        session = player.prepare(audio_path)

        stream_finished = threading.Event()
        mute_output = threading.Event()

        playback_frame = 0

        def callback(
            indata: np.ndarray,
            outdata: np.ndarray,
            frames: int,
            time_info: object,
            status: sd.CallbackFlags,
        ) -> None:
            del time_info

            if status:
                print(
                    f"[WARNING] Barge-in audio "
                    f"callback: {status}"
                )

            nonlocal playback_frame

            outdata.fill(0)

            if not mute_output.is_set():
                remaining = (
                    session.samples.shape[0]
                    - playback_frame
                )

                output_frames = min(
                    frames,
                    remaining,
                )

                if output_frames > 0:
                    output_end = (
                        playback_frame
                        + output_frames
                    )

                    outdata[:output_frames] = (
                        session.samples[
                            playback_frame:output_end
                        ]
                    )

                    playback_frame = output_end

            audio_queue.put(
                indata[:, 0].copy()
            )

            if (
                not mute_output.is_set()
                and playback_frame
                >= session.samples.shape[0]
            ):
                raise sd.CallbackStop

        start = perf_counter()

        speech_frames = 0
        interrupted = False
        detection_ms = 0.0
        speech_samples: Optional[
            np.ndarray
        ] = None

        playback_result: Optional[
            PlaybackResult
        ] = None

        duplex_block_size = max(
            1,
            int(
                round(
                    self.window_size
                    * session.sample_rate
                    / self.sample_rate
                )
            ),
        )

        try:
            sd.check_input_settings(
                device=self.device,
                channels=1,
                samplerate=session.sample_rate,
                dtype="float32",
            )

            with sd.Stream(
                samplerate=session.sample_rate,
                blocksize=duplex_block_size,
                device=(
                    self.device,
                    player.device,
                ),
                channels=(
                    1,
                    session.channels,
                ),
                dtype="float32",
                callback=callback,
                finished_callback=(
                    stream_finished.set
                ),
            ) as stream:

                while not stream_finished.is_set():
                    try:
                        samples = audio_queue.get(
                            timeout=0.1
                        )
                    except queue.Empty:
                        continue

                    vad_samples = (
                        self._resample_for_vad(
                            samples,
                            source_sample_rate=(
                                session.sample_rate
                            ),
                        )
                    )

                    elapsed = perf_counter() - start

                    if (
                        not interrupted
                        and elapsed
                        < self.ignore_start_seconds
                    ):
                        continue

                    self.vad.accept_waveform(
                        vad_samples
                    )

                    if (
                        self.vad
                        .is_speech_detected()
                    ):
                        speech_frames += 1
                    elif not interrupted:
                        speech_frames = 0

                    if (
                        not interrupted
                        and speech_frames
                        >= self.required_speech_frames
                    ):
                        interrupted = True

                        detection_ms = (
                            perf_counter() - start
                        ) * 1000

                        # 回调继续采集输入，但停止输出回答音频。
                        mute_output.set()

                        playback_result = (
                            player.finish(
                                interrupted=True
                            )
                        )

                        print(
                            "[INFO] Barge-in detected, "
                            "capturing speech..."
                        )

                    if (
                        interrupted
                        and not self.vad.empty()
                    ):
                        segment = self.vad.front
                        self.vad.pop()

                        speech_samples = np.asarray(
                            segment.samples,
                            dtype=np.float32,
                        )

                        stream.abort()
                        stream_finished.set()
                        break

        except Exception:
            player.cancel()
            raise

        if not interrupted:
            playback_result = player.finish(
                interrupted=False
            )

            return BargeInResult(
                interrupted=interrupted,
                detection_ms=detection_ms,
                speech_frames=speech_frames,
                playback_result=playback_result,
            )

        if playback_result is None:
            raise RuntimeError(
                "Interrupted playback result "
                "was not created"
            )

        if (
            speech_samples is None
            or speech_samples.size == 0
        ):
            raise ValueError(
                "Barge-in VAD returned empty speech"
            )

        speech_output_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        sf.write(
            str(speech_output_path),
            speech_samples,
            samplerate=self.sample_rate,
            subtype="PCM_16",
        )

        speech_duration_seconds = (
            speech_samples.size
            / self.sample_rate
        )

        return BargeInResult(
            interrupted=True,
            detection_ms=detection_ms,
            speech_frames=speech_frames,
            playback_result=playback_result,
            speech_path=str(
                speech_output_path
            ),
            speech_duration_seconds=(
                speech_duration_seconds
            ),
        )

    def wait_for_interrupt(
        self,
        player: AudioPlayer,
    ) -> BargeInResult:
        """Deprecated two-stream API retained with a clear migration error."""
        del player
        raise RuntimeError(
            "wait_for_interrupt() requires a "
            "separately running output stream; use "
            "play_and_wait(player, audio_path) for "
            "full-duplex playback"
        )
