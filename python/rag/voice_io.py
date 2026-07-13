from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter


@dataclass
class AsrResult:
    text: str
    backend: str
    source: str
    duration_ms: float = 0.0


@dataclass
class TtsResult:
    output_path: str
    backend: str
    text: str
    duration_ms: float = 0.0


class AsrBackend(ABC):
    @abstractmethod
    def transcribe(self, audio_path: Path) -> AsrResult:
        raise NotImplementedError


class TtsBackend(ABC):
    @abstractmethod
    def synthesize(self, text: str, output_path: Path) -> TtsResult:
        raise NotImplementedError


class MockAsr(AsrBackend):
    def __init__(self, mock_text: str) -> None:
        self.backend = "mock_asr"
        self.mock_text = mock_text

    def transcribe(self, audio_path: Path) -> AsrResult:
        start = perf_counter()
        return AsrResult(
            text=self.mock_text,
            backend=self.backend,
            source=str(audio_path),
            duration_ms=(perf_counter() - start) * 1000,
        )


class MockTts(TtsBackend):
    def __init__(self) -> None:
        self.backend = "mock_tts"

    def synthesize(self, text: str, output_path: Path) -> TtsResult:
        start = perf_counter()
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with output_path.open("w", encoding="utf-8") as file:
            file.write(text)

        return TtsResult(
            output_path=str(output_path),
            backend=self.backend,
            text=text,
            duration_ms=(perf_counter() - start) * 1000,
        )


def create_asr_backend(backend: str, mock_text: str = "") -> AsrBackend:
    if backend == "mock":
        return MockAsr(mock_text=mock_text)

    raise ValueError(f"Unsupported ASR backend: {backend}")


def create_tts_backend(backend: str) -> TtsBackend:
    if backend == "mock":
        return MockTts()

    raise ValueError(f"Unsupported TTS backend: {backend}")
