from pathlib import Path

import pytest

from rag.microphone_io import (
    MicrophoneRecorder,
    parse_device,
)

def test_parse_numeric_device() -> None:
    assert parse_device("3") == 3

def test_parse_device_name() -> None:
    assert (
        parse_device("USB Microphone")
        == "USB Microphone"
    )

def test_parse_empty_device() -> None:
    assert parse_device(None) is None
    assert parse_device("") is None
    assert parse_device("   ") is None

def test_invalid_sample_rate() -> None:
    with pytest.raises(
        ValueError,
        match="sample_rate must be positive",
    ):
        MicrophoneRecorder(
            sample_rate=0,
        )

def test_invalid_channels() -> None:
    with pytest.raises(
        ValueError,
        match="channels must be positive",
    ):
        MicrophoneRecorder(
            channels=0,
        )


def test_invalid_duration(
    tmp_path: Path,
) -> None:
    recorder = MicrophoneRecorder()

    with pytest.raises(
        ValueError,
        match="duration_seconds must be positive",
    ):
        recorder.record(
            duration_seconds=0,
            output_path=(
                tmp_path / "audio.wav"
            ),
        )


def test_output_must_be_wav(
    tmp_path: Path,
) -> None:
    recorder = MicrophoneRecorder()

    with pytest.raises(
        ValueError,
        match="Recording output must be WAV",
    ):
        recorder.record(
            duration_seconds=1,
            output_path=(
                tmp_path / "audio.mp3"
            ),
        )
