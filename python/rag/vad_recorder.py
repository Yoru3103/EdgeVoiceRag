import queue
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Optional, Union

import numpy as np
import sherpa_onnx
import sounddevice as sd
import soundfile as sf


MicrophoneDevice = Optional[Union[int, str]]


@dataclass
class VadRecordingResult:
    output_path: str
    duration_seconds: float
    waiting_ms: float
    recording_ms: float
    sample_rate: int
    rms: float
    peak: float


class VadMicrophoneRecorder:
    def __init__(
        self,
        model_path: str,
        device: MicrophoneDevice = None,
        sample_rate: int = 16000,
        threshold: float = 0.25,
        min_silence_duration: float = 0.8,
        min_speech_duration: float = 0.25,
        max_speech_duration: float = 15.0,
        max_wait_seconds: float = 10.0,
    ) -> None:
        if not 0 < threshold < 1:
            raise ValueError(
                f"threshold must be between 0 and 1: "
                f"{threshold}"
            )

        if min_silence_duration <= 0:
            raise ValueError(
                "min_silence_duration must be positive"
            )

        if min_speech_duration <= 0:
            raise ValueError(
                "min_speech_duration must be positive"
            )

        if max_speech_duration <= 0:
            raise ValueError(
                "max_speech_duration must be positive"
            )

        if max_wait_seconds <= 0:
            raise ValueError(
                "max_wait_seconds must be positive"
            )

        self.model_path = Path(model_path)
        self.device = device
        self.sample_rate = sample_rate
        self.threshold = threshold
        self.min_silence_duration = (
            min_silence_duration
        )
        self.min_speech_duration = (
            min_speech_duration
        )
        self.max_speech_duration = (
            max_speech_duration
        )
        self.max_wait_seconds = max_wait_seconds
        self.window_size = 512

        if not self.model_path.is_file():
            raise FileNotFoundError(
                f"VAD model not found: "
                f"{self.model_path}"
            )

        if self.sample_rate != 16000:
            raise ValueError(
                "Silero VAD requires 16000 Hz"
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
            self.min_silence_duration
        )
        config.silero_vad.min_speech_duration = (
            self.min_speech_duration
        )
        # 单个语音片段允许的最大长度
        config.silero_vad.max_speech_duration = (
            self.max_speech_duration
        )
        # 每次送入模型的采样点
        config.silero_vad.window_size = (
            self.window_size
        )

        config.sample_rate = self.sample_rate
        config.num_threads = 1

        return sherpa_onnx.VoiceActivityDetector(
            config,
            buffer_size_in_seconds=30,  # VAD内部音频缓冲区最多保存多久的音频数据
        )

    def record(
        self,
        output_path: Path,
    ) -> VadRecordingResult:
        self.vad.reset()

        if output_path.suffix.lower() != ".wav":
            raise ValueError(
                "VAD recording output must be WAV"
            )

        output_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        # ndarray为多维数组
        audio_queue: queue.Queue[np.ndarray] = (
            queue.Queue()   # 线程安全的 适合 麦克风回调线程与主线程之间传递音频
        )

        def callback(
            indata: np.ndarray,
            frames: int,
            time_info: object,
            status: sd.CallbackFlags,   # 录音状态，可能包含输入溢出等异常。正常情况下它为空。
        ) -> None:
            if status:
                print(
                    f"[WARNING] Audio callback: "
                    f"{status}"
                )

            audio_queue.put(
                indata[:, 0].copy() # 取所有音频帧的第0个声道，将二维数组变成一维数组。然后复制音频数据
            )

        print("[INFO] Waiting for speech...")

        start = perf_counter()
        speech_started = False
        speech_start_time = 0.0
        speech_samples: Optional[np.ndarray] = (
            None
        )

        # 进入 with 时麦克风启动；离开 with 时自动停止并关闭，即使发生异常也会清理设备。
        with sd.InputStream(
            samplerate=self.sample_rate,
            channels=1,
            dtype="float32",
            blocksize=self.window_size,
            device=self.device,
            callback=callback,
        ):
            while True:
                elapsed = perf_counter() - start

                if (
                    not speech_started
                    and elapsed > self.max_wait_seconds
                ):
                    raise TimeoutError(
                        "No speech detected within "
                        f"{self.max_wait_seconds} seconds"
                    )
                # queue在队列空时会阻塞等待，timeout最多等待一秒
                try:
                    samples = audio_queue.get(
                        timeout=1.0
                    )
                except queue.Empty:
                    continue


                self.vad.accept_waveform(
                    samples
                )

                if self.vad.is_speech_detected():
                    if not speech_started:
                        speech_started = True
                        speech_start_time = (
                            perf_counter()
                        )
                        print(
                            "[INFO] Speech detected"
                        )

                # VAD内部维护了一个已完成语音段的队列
                if not self.vad.empty():
                    segment = self.vad.front
                    self.vad.pop()

                    speech_samples = np.asarray(
                        segment.samples,
                        dtype=np.float32,
                    )
                    break

        if (
            speech_samples is None
            or speech_samples.size == 0
        ):
            raise ValueError(
                "VAD returned empty speech"
            )

        end = perf_counter()

        waiting_ms = (
            speech_start_time - start
        ) * 1000

        recording_ms = (
            end - speech_start_time
        ) * 1000

        rms = float(
            np.sqrt(
                np.mean(
                    np.square(speech_samples)
                )
            )
        )

        peak = float(
            np.max(
                np.abs(speech_samples)
            )
        )

        sf.write(
            str(output_path),
            speech_samples,
            samplerate=self.sample_rate,
            subtype="PCM_16",
        )

        return VadRecordingResult(
            output_path=str(output_path),
            duration_seconds=(
                speech_samples.size
                / self.sample_rate
            ),
            waiting_ms=waiting_ms,
            recording_ms=recording_ms,
            sample_rate=self.sample_rate,
            rms=rms,
            peak=peak,
        )