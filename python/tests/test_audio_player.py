from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

from rag.audio_player import (
    AudioPlayer,
    list_output_devices,
    parse_output_device,
)


def _create_test_audio(
    output_path: Path,
    sample_rate: int = 16000,
    duration_seconds: float = 0.1,
) -> None:
    frame_count = int(
        sample_rate * duration_seconds
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


def _mock_output_device(
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


def test_list_output_devices(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    devices = [
        {
            "name": "Input Only",
            "max_output_channels": 0,
            "default_samplerate": 16000.0,
            "hostapi": 0,
        },
        {
            "name": "Speaker One",
            "max_output_channels": 2,
            "default_samplerate": 44100.0,
            "hostapi": 0,
        },
        {
            "name": "Speaker Two",
            "max_output_channels": 2,
            "default_samplerate": 48000.0,
            "hostapi": 1,
        },
    ]

    monkeypatch.setattr(
        "rag.audio_player.sd.query_devices",
        lambda: devices,
    )

    result = list_output_devices()

    assert len(result) == 2
    assert result[0]["name"] == "Speaker One"
    assert result[1]["name"] == "Speaker Two"


def test_list_output_devices_empty(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        "rag.audio_player.sd.query_devices",
        lambda: [],
    )

    assert list_output_devices() == []


def test_parse_numeric_output_device() -> None:
    assert parse_output_device("3") == 3


def test_parse_output_device_name() -> None:
    assert (
        parse_output_device("USB Speaker")
        == "USB Speaker"
    )


def test_parse_empty_output_device() -> None:
    assert parse_output_device(None) is None
    assert parse_output_device("") is None
    assert parse_output_device("   ") is None


def test_playback_rejects_missing_file(
    tmp_path: Path,
) -> None:
    player = AudioPlayer()

    with pytest.raises(
        FileNotFoundError,
        match="Playback audio not found",
    ):
        player.play(
            tmp_path / "missing.wav"
        )


def test_prepare_creates_playback_session(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"
    _create_test_audio(audio_path)
    _mock_output_device(monkeypatch)

    player = AudioPlayer(
        device="Mock Speaker"
    )

    session = player.prepare(audio_path)

    assert session.audio_path == audio_path
    assert session.sample_rate == 16000
    assert session.channels == 1
    assert session.samples.ndim == 2
    assert session.samples.shape[1] == 1
    assert session.device_name == "Mock Speaker"


def test_prepare_rejects_second_session(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"
    _create_test_audio(audio_path)
    _mock_output_device(monkeypatch)

    player = AudioPlayer()
    player.prepare(audio_path)

    with pytest.raises(
        RuntimeError,
        match="already running",
    ):
        player.prepare(audio_path)

    player.cancel()


def test_finish_returns_normal_result(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"
    _create_test_audio(audio_path)
    _mock_output_device(monkeypatch)

    player = AudioPlayer()
    player.prepare(audio_path)

    result = player.finish(
        interrupted=False
    )

    assert result.audio_path == str(
        audio_path
    )
    assert result.sample_rate == 16000
    assert result.channels == 1
    assert result.device == "Mock Speaker"
    assert result.interrupted is False
    assert result.playback_ms >= 0


def test_finish_returns_interrupted_result(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"
    _create_test_audio(audio_path)
    _mock_output_device(monkeypatch)

    player = AudioPlayer()
    player.prepare(audio_path)

    result = player.finish(
        interrupted=True
    )

    assert result.interrupted is True
    assert result.playback_ms >= 0


def test_cancel_allows_new_session(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    audio_path = tmp_path / "answer.wav"
    _create_test_audio(audio_path)
    _mock_output_device(monkeypatch)

    player = AudioPlayer()

    player.prepare(audio_path)
    player.cancel()

    session = player.prepare(audio_path)

    assert session.audio_path == audio_path

    player.cancel()


def test_finish_rejects_missing_session() -> None:
    player = AudioPlayer()

    with pytest.raises(
        RuntimeError,
        match="No active playback session",
    ):
        player.finish(
            interrupted=False
        )