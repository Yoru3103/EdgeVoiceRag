from pathlib import Path
from typing import Callable, Optional

import numpy as np
import pytest
import sounddevice as sd
import soundfile as sf

from rag.audio_player import AudioPlayer
from rag.barge_in_detector import (
    BargeInDetector,
)


class FakeVad:
    def __init__(
        self,
        speech_detected: bool,
    ) -> None:
        self.speech_detected = (
            speech_detected
        )
        self.accepted_frames = 0
        self.reset_count = 0

    def reset(self) -> None:
        self.reset_count += 1
        self.accepted_frames = 0

    def accept_waveform(
        self,
        samples: np.ndarray,
    ) -> None:
        self.accepted_frames += 1

    def is_speech_detected(self) -> bool:
        return self.speech_detected


class FakeDuplexStream:
    def __init__(
        self,
        callback: Callable,
        finished_callback: Callable,
        channels: tuple[int, int],
        finish_during_enter: bool,
    ) -> None:
        self.callback = callback
        self.finished_callback = (
            finished_callback
        )
        self.input_channels = channels[0]
        self.output_channels = channels[1]
        self.finish_during_enter = (
            finish_during_enter
        )
        self.aborted = False

    def __enter__(
        self,
    ) -> "FakeDuplexStream":
        input_samples = np.zeros(
            (512, self.input_channels),
            dtype=np.float32,
        )

        output_samples = np.zeros(
            (512, self.output_channels),
            dtype=np.float32,
        )

        try:
            self.callback(
                input_samples,
                output_samples,
                512,
                object(),
                None,
            )
        except sd.CallbackStop:
            self.finished_callback()

        if self.finish_during_enter:
            self.finished_callback()

        return self

    def __exit__(
        self,
        exc_type: object,
        exc_value: object,
        traceback: object,
    ) -> bool:
        return False

    def abort(self) -> None:
        self.aborted = True
        self.finished_callback()


def _create_audio(
    output_path: Path,
    duration_seconds: float,
    sample_rate: int = 16000,
) -> None:
    frame_count = int(
        duration_seconds * sample_rate
    )

    samples = np.zeros(
        (frame_count, 1),
        dtype=np.float32,
    )

    sf.write(
        str(output_path),
        samples,
        samplerate=sample_rate,
        subtype="PCM_16",
    )


def _create_detector(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    fake_vad: FakeVad,
    required_speech_frames: int = 1,
) -> BargeInDetector:
    model_path = tmp_path / "silero_vad.onnx"
    model_path.write_bytes(b"mock")

    monkeypatch.setattr(
        BargeInDetector,
        "_create_vad",
        lambda self: fake_vad,
    )

    return BargeInDetector(
        model_path=str(model_path),
        threshold=0.6,
        required_speech_frames=(
            required_speech_frames
        ),
        ignore_start_seconds=0,
    )


def _mock_audio_devices(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        "rag.audio_player.sd.check_output_settings",
        lambda **kwargs: None,
    )

    monkeypatch.setattr(
        "rag.audio_player.sd.query_devices",
        lambda device=None, kind=None: {
            "name": "Mock Speaker",
        },
    )

    monkeypatch.setattr(
        "rag.barge_in_detector.sd.check_input_settings",
        lambda **kwargs: None,
    )


def test_play_and_wait_finishes_normally(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "short.wav"

    _create_audio(
        audio_path,
        duration_seconds=0.01,
    )

    fake_vad = FakeVad(
        speech_detected=False
    )

    detector = _create_detector(
        tmp_path,
        monkeypatch,
        fake_vad,
    )

    _mock_audio_devices(monkeypatch)

    created_stream: Optional[
        FakeDuplexStream
    ] = None

    def create_stream(
        **kwargs: object,
    ) -> FakeDuplexStream:
        nonlocal created_stream

        created_stream = FakeDuplexStream(
            callback=kwargs["callback"],
            finished_callback=(
                kwargs["finished_callback"]
            ),
            channels=kwargs["channels"],
            finish_during_enter=False,
        )

        return created_stream

    monkeypatch.setattr(
        "rag.barge_in_detector.sd.Stream",
        create_stream,
    )

    player = AudioPlayer()

    result = detector.play_and_wait(
        player,
        audio_path,
    )

    assert result.interrupted is False
    assert (
        result.playback_result.interrupted
        is False
    )
    assert result.detection_ms >= 0
    assert created_stream is not None
    assert created_stream.aborted is False


def test_play_and_wait_stops_on_speech(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "long.wav"

    _create_audio(
        audio_path,
        duration_seconds=1.0,
    )

    fake_vad = FakeVad(
        speech_detected=True
    )

    detector = _create_detector(
        tmp_path,
        monkeypatch,
        fake_vad,
        required_speech_frames=1,
    )

    _mock_audio_devices(monkeypatch)

    created_stream: Optional[
        FakeDuplexStream
    ] = None

    def create_stream(
        **kwargs: object,
    ) -> FakeDuplexStream:
        nonlocal created_stream

        created_stream = FakeDuplexStream(
            callback=kwargs["callback"],
            finished_callback=(
                kwargs["finished_callback"]
            ),
            channels=kwargs["channels"],
            finish_during_enter=False,
        )

        return created_stream

    monkeypatch.setattr(
        "rag.barge_in_detector.sd.Stream",
        create_stream,
    )

    player = AudioPlayer()

    result = detector.play_and_wait(
        player,
        audio_path,
    )

    assert result.interrupted is True
    assert (
        result.playback_result.interrupted
        is True
    )
    assert result.speech_frames == 1
    assert fake_vad.accepted_frames == 1
    assert created_stream is not None
    assert created_stream.aborted is True


def test_stream_failure_cancels_player(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"

    _create_audio(
        audio_path,
        duration_seconds=1.0,
    )

    fake_vad = FakeVad(
        speech_detected=False
    )

    detector = _create_detector(
        tmp_path,
        monkeypatch,
        fake_vad,
    )

    _mock_audio_devices(monkeypatch)

    def create_failed_stream(
        **kwargs: object,
    ) -> object:
        raise RuntimeError(
            "cannot open duplex stream"
        )

    monkeypatch.setattr(
        "rag.barge_in_detector.sd.Stream",
        create_failed_stream,
    )

    player = AudioPlayer()

    with pytest.raises(
        RuntimeError,
        match="cannot open duplex stream",
    ):
        detector.play_and_wait(
            player,
            audio_path,
        )

    session = player.prepare(audio_path)

    assert session.audio_path == audio_path

    player.cancel()


def test_resample_for_vad() -> None:
    detector = object.__new__(
        BargeInDetector
    )
    detector.sample_rate = 16000

    source = np.linspace(
        -1.0,
        1.0,
        num=441,
        dtype=np.float32,
    )

    result = detector._resample_for_vad(
        source,
        source_sample_rate=44100,
    )

    assert result.dtype == np.float32
    assert result.ndim == 1
    assert result.size == 160


def test_resample_returns_copy_at_same_rate() -> None:
    detector = object.__new__(
        BargeInDetector
    )
    detector.sample_rate = 16000

    source = np.zeros(
        512,
        dtype=np.float32,
    )

    result = detector._resample_for_vad(
        source,
        source_sample_rate=16000,
    )

    assert result.size == 512
    assert result is not source