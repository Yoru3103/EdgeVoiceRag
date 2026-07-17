from pathlib import Path

from rag.microphone_io import parse_device
from rag.vad_recorder import (
    VadMicrophoneRecorder,
)


def main() -> None:
    recorder = VadMicrophoneRecorder(
        model_path=(
            "models/vad/silero_vad.onnx"
        ),
        device=parse_device(None),
        threshold=0.25,
        min_silence_duration=0.8,
        max_wait_seconds=10.0,
    )

    result = recorder.record(
        Path("voice_input/vad_input.wav")
    )

    print(result)


if __name__ == "__main__":
    main()