import argparse
import json
from pathlib import Path

from rag.voice_io import create_tts_backend



def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run Sherpa-ONNX offline TTS."
    )

    parser.add_argument(
        "--text",
        required=True,
        help="Text to synthesize.",
    )

    parser.add_argument(
        "--model-dir",
        default="models/tts/vits-melo-tts-zh_en",
        help="Sherpa-ONNX TTS model directory.",
    )

    parser.add_argument(
        "--output",
        default="voice_output/sherpa_answer.wav",
        help="Output WAV path.",
    )

    parser.add_argument(
        "--num-threads",
        type=int,
        default=2,
        help="Number of inference threads.",
    )

    parser.add_argument(
        "--speaker-id",
        type=int,
        default=0,
        help="TTS speaker ID.",
    )

    parser.add_argument(
        "--speed",
        type=float,
        default=1.0,
        help="Speech speed. Larger values are faster.",
    )

    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable Sherpa-ONNX debug output.",
    )

    args = parser.parse_args()

    output_path = Path(args.output)
    
    try:
        tts = create_tts_backend(
            backend="sherpa_onnx",
            model_dir=args.model_dir,
            num_threads=args.num_threads,
            speaker_id=args.speaker_id,
            speed=args.speed,
            debug=args.debug,
        )
        
        result = tts.synthesize(
            text=args.text,
            output_path=output_path,
        )
        
        print(
            json.dumps(
                {
                    "ok": True,
                    "backend": result.backend,
                    "text": result.text,
                    "output_path": result.output_path,
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
                    "text": args.text,
                    "output_path": str(output_path),
                    "error": str(exc),
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        
if __name__ == "__main__":
    main()