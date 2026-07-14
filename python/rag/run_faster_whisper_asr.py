import argparse
import json
from pathlib import Path

from rag.voice_io import create_asr_backend

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Test Faster-Whisper ASR backend."
    )

    parser.add_argument(
        "--audio",
        required=True,
        help="Input audio file.",
    )

    parser.add_argument(
        "--model",
        default="small",
        help="Model name or local model path.",
    )

    parser.add_argument(
        "--device",
        default="cpu",
        choices=["cpu", "cuda"],
    )

    parser.add_argument(
        "--compute-type",
        default="int8",
    )

    args = parser.parse_args()

    audio_path = Path(args.audio)

    try:
        asr = create_asr_backend(
            backend="faster_whisper",
            model_name_or_path=args.model,
            device=args.device,
            compute_type=args.compute_type,
            language="zh",
        )

        result = asr.transcribe(audio_path)

        print(
            json.dumps(
                {
                    "ok": True,
                    "backend": result.backend,
                    "audio_path": result.source,
                    "text": result.text,
                    "duration_ms": round(
                        result.duration_ms,
                        2,
                    ),
                },
                ensure_ascii=False,
                indent=2,
            )
        )

    except Exception as exc:
        print(
            json.dumps(
                {
                    "ok": False,
                    "audio_path": str(audio_path),
                    "error": str(exc),
                },
                ensure_ascii=False,
                indent=2,
            )
        )

if __name__ == "__main__":
    main()