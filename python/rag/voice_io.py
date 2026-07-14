from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter

import soundfile as sf
import sherpa_onnx
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

class SherpaOnnxTts(TtsBackend):
    def __init__(
        self,
        model_dir: str,
        num_threads: int = 2,
        speaker_id: int = 0,
        speed: float = 1.0,
        debug: bool = False,
    ) -> None:
        if num_threads <= 0:
            raise ValueError(
                f"num_threads must be positive: {num_threads}"
            )

        if speaker_id < 0:
            raise ValueError(
                f"speaker_id must be non-negative: {speaker_id}"
            )

        if speed <= 0:
            raise ValueError(
                f"speed must be positive: {speed}"
            )

        self.backend = "sherpa_onnx_tts"
        self.model_dir = Path(model_dir)
        self.num_threads = num_threads
        self.speaker_id = speaker_id
        self.speed = speed
        self.debug = debug

        self.model_path = self.model_dir / "model.onnx"
        self.lexicon_path = self.model_dir / "lexicon.txt"
        self.tokens_path = self.model_dir / "tokens.txt"
        self.date_fst_path = self.model_dir / "date.fst"
        self.number_fst_path = self.model_dir / "number.fst"

        self._validate_files()

        vits_config = sherpa_onnx.OfflineTtsVitsModelConfig(
            model=str(self.model_path),
            lexicon=str(self.lexicon_path),
            tokens=str(self.tokens_path),
        )

        model_config = sherpa_onnx.OfflineTtsModelConfig(
            vits=vits_config,
            num_threads=self.num_threads,
            debug=self.debug,
            provider="cpu",
        )

        rule_fsts = ",".join(
            [
                str(self.date_fst_path),
                str(self.number_fst_path),
            ]
        )

        tts_config = sherpa_onnx.OfflineTtsConfig(
            model=model_config,
            rule_fsts=rule_fsts,
            max_num_sentences=2,
        )

        if not tts_config.validate():
            raise ValueError(
                f"Invalid Sherpa-ONNX TTS configuration: {self.model_dir}"
            )

        self.tts = sherpa_onnx.OfflineTts(tts_config)

    def _validate_files(self) -> None:
        required_files = [
            self.model_path,
            self.lexicon_path,
            self.tokens_path,
            self.date_fst_path,
            self.number_fst_path,
        ]

        missing_files = [
            str(path)
            for path in required_files
            if not path.is_file()
        ]

        if missing_files:
            missing_text = ", ".join(missing_files)

            raise FileNotFoundError(
                f"TTS model files not found: {missing_text}"
            )

    def synthesize(
        self,
        text: str,
        output_path: Path,
    ) -> TtsResult:
        normalized_text = text.strip()

        if not normalized_text:
            raise ValueError("TTS input text is empty")

        if output_path.suffix.lower() != ".wav":
            raise ValueError(
                f"TTS output must be a WAV file: {output_path}"
            )

        output_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        generation_config = sherpa_onnx.GenerationConfig()
        generation_config.sid = self.speaker_id
        generation_config.speed = self.speed

        start = perf_counter()

        audio = self.tts.generate(
            normalized_text,
            generation_config,
        )

        duration_ms = (
            perf_counter() - start
        ) * 1000

        if len(audio.samples) == 0:
            raise ValueError(
                "Sherpa-ONNX TTS returned empty audio"
            )

        if audio.sample_rate <= 0:
            raise ValueError(
                f"Invalid TTS sample rate: {audio.sample_rate}"
            )

        sf.write(
            str(output_path),
            audio.samples,
            samplerate=audio.sample_rate,
            subtype="PCM_16",
        )

        return TtsResult(
            output_path=str(output_path),
            backend=self.backend,
            text=normalized_text,
            duration_ms=duration_ms,
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


def create_tts_backend(
    backend: str,
    model_dir: str = "",
    num_threads: int = 2,
    speaker_id: int = 0,
    speed: float = 1.0,
    debug: bool = False,
) -> TtsBackend:
    if backend == "mock":
        return MockTts()

    if backend == "sherpa_onnx":
        return SherpaOnnxTts(
            model_dir=model_dir,
            num_threads=num_threads,
            speaker_id=speaker_id,
            speed=speed,
            debug=debug,
        )

    raise ValueError(
        f"Unsupported TTS backend: {backend}"
    )
