from dataclasses import dataclass
from pathlib import Path

@dataclass
class AsrResult:
    text: str
    backend: str
    source: str
    
@dataclass
class TtsResult:
    output_path: str
    backend: str
    text: str
    
class MockAsr:
    def __init__(self, mock_text: str) -> None:
        self.backend = "mock_asr"
        self.mock_text = mock_text
        
    def transcribe(self, audio_path: Path) -> AsrResult:
        return AsrResult(
            text=self.mock_text,
            backend=self.backend,
            source=str(audio_path)
        )
        
class MockTts:
    def __init__(self) -> None:
        self.backend = "mock_tts"
        
    def synthesize(self, text: str, output_path: Path) -> TtsResult:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with output_path.open("w", encoding="utf-8") as file:
            file.write(text)
            
        return TtsResult(
            output_path=output_path,
            backend=self.backend,
            text=text,
        )
        
def create_asr_backend(backend: str, mock_text: str):
    if backend == "mock":
        return MockAsr(mock_text=mock_text)
    
    raise ValueError(f"Unsupported ASR backend: {backend}")

def create_tts_backend(backend: str):
    if backend == "mock":
        return MockTts()

    raise ValueError(f"Unsupported TTS backend: {backend}")