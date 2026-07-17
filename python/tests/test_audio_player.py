from pathlib import Path

import pytest

from rag.audio_player import (
    AudioPlayer,
    list_output_devices,
    parse_output_device,
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
