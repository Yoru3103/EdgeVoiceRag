from pathlib import Path

import pytest

from rag.vad_recorder import (
    VadMicrophoneRecorder,
)


def test_vad_rejects_missing_model(
    tmp_path: Path,
) -> None:
    with pytest.raises(
        FileNotFoundError,
        match="VAD model not found",
    ):
        VadMicrophoneRecorder(
            model_path=str(
                tmp_path / "missing.onnx"
            )
        )


def test_vad_requires_16000_hz() -> None:
    with pytest.raises(
        ValueError,
        match="requires 16000 Hz",
    ):
        VadMicrophoneRecorder(
            model_path=(
                "models/vad/"
                "silero_vad.onnx"
            ),
            sample_rate=8000,
        )


def test_vad_rejects_invalid_threshold() -> None:
    with pytest.raises(
        ValueError,
        match="threshold must be",
    ):
        VadMicrophoneRecorder(
            model_path=(
                "models/vad/"
                "silero_vad.onnx"
            ),
            threshold=1.5,
        )


def test_vad_rejects_invalid_wait_time() -> None:
    with pytest.raises(
        ValueError,
        match="max_wait_seconds",
    ):
        VadMicrophoneRecorder(
            model_path=(
                "models/vad/"
                "silero_vad.onnx"
            ),
            max_wait_seconds=0,
        )