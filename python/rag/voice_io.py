from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from faster_whisper import WhisperModel


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

class FasterWhisperAsr(AsrBackend):
    def __init__(
        self,
        model_name_or_path: str,
        device: str = "cpu",
        compute_type: str = "int8",
        language: str = "zh",
        beam_size: int = 5,
        vad_filter: bool = True
    ) -> None:
        self.backend = "faster_whisper"
        self.model_name_or_path = model_name_or_path
        self.device = device
        self.compute_type = compute_type
        self.language = language
        self.beam_size = beam_size
        self.vad_filter = vad_filter

        # 加载模型
        self.model = WhisperModel(
            model_name_or_path,
            device=device,
            compute_type=compute_type,
        )

    def transcribe(self, audio_path: Path) -> AsrResult:
        if not audio_path.exists():
            raise FileNotFoundError(
                f"Audio file not found: {audio_path}"
            )

        if not audio_path.is_file():
            raise ValueError(
                f"Audio path is not a file: {audio_path}"
            )

        start = perf_counter()

        segments, _ = self.model.transcribe(
            str(audio_path),
            language=self.language,
            beam_size=self.beam_size,           # 搜索多个候选识别结果
            vad_filter=self.vad_filter,         # 过滤较长的静音部分
            condition_on_previous_text=False,   # 每个语音段减少对之前文本的依赖，短车载指令通常更稳定
        )

        texts = []

        for segment in segments:
            text = segment.text.strip()

            if text:
                texts.append(text)

        result_text = "".join(texts).strip()
        duration_ms = (perf_counter() - start) * 1000

        if not result_text:
            raise ValueError(
                f"ASR returned empty text: {audio_path}"
            )

        return AsrResult(
            text=result_text,
            source=str(audio_path),
            backend=self.backend,
            duration_ms=duration_ms,
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


def create_asr_backend(
    backend: str,
    mock_text: str = "",
    model_name_or_path: str = "small",
    device: str = "cpu",
    compute_type: str = "int8",
    language: str = "zh",
) -> AsrBackend:
    if backend == "mock":
        return MockAsr(
            mock_text=mock_text,
        )

    if backend == "faster_whisper":
        return FasterWhisperAsr(
            model_name_or_path=model_name_or_path,
            device=device,
            compute_type=compute_type,
            language=language,
        )

    raise ValueError(f"Unsupported ASR backend: {backend}")


def create_tts_backend(backend: str) -> TtsBackend:
    if backend == "mock":
        return MockTts()

    raise ValueError(f"Unsupported TTS backend: {backend}")
