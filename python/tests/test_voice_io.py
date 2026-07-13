from pathlib import Path

import pytest

from rag.voice_io import create_asr_backend, create_tts_backend


def test_mock_asr_returns_configured_text() -> None:
    backend = create_asr_backend("mock", "空调怎么打开")

    result = backend.transcribe(Path("voice_input/mock.wav"))

    assert result.text == "空调怎么打开"
    assert result.backend == "mock_asr"
    assert result.source == "voice_input/mock.wav"
    assert result.duration_ms >= 0


def test_mock_tts_writes_answer(tmp_path: Path) -> None:
    backend = create_tts_backend("mock")
    output_path = tmp_path / "answer.txt"

    result = backend.synthesize("测试回答", output_path)

    assert output_path.read_text(encoding="utf-8") == "测试回答"
    assert result.output_path == str(output_path)
    assert result.backend == "mock_tts"
    assert result.duration_ms >= 0


@pytest.mark.parametrize("backend", ["unknown", "real"])
def test_unsupported_asr_backend_raises(backend: str) -> None:
    with pytest.raises(ValueError, match="Unsupported ASR backend"):
        create_asr_backend(backend)


def test_unsupported_tts_backend_raises() -> None:
    with pytest.raises(ValueError, match="Unsupported TTS backend"):
        create_tts_backend("unknown")
