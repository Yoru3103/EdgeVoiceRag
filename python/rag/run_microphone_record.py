import argparse
import json
from pathlib import Path

from rag.microphone_io import (
    MicrophoneRecorder,
    parse_device,
)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Record a fixed-duration WAV "
            "from the microphone."
        )
    )

    parser.add_argument(
        "--output",
        default=(
            "voice_input/"
            "microphone_input.wav"
        ),
    )

    parser.add_argument(
        "--duration",
        type=float,
        default=5.0,
    )

    parser.add_argument(
        "--sample-rate",
        type=int,
        default=16000,
    )

    parser.add_argument(
        "--channels",
        type=int,
        default=1,
    )

    parser.add_argument(
        "--device",
        default=None,
        help=(
            "Input device index or "
            "device name substring."
        ),
    )

    parser.add_argument(
        "--minimum-rms",
        type=float,
        default=0.001,
    )

    args = parser.parse_args()

    output_path = Path(args.output)

    try:
        recorder = MicrophoneRecorder(
            sample_rate=args.sample_rate,
            channels=args.channels,
            device=parse_device(
                args.device
            ),
            minimum_rms=args.minimum_rms,
        )

        result = recorder.record(
            duration_seconds=args.duration,
            output_path=output_path,
        )

        print(
            json.dumps(
                {
                    "ok": True,
                    "output_path": (
                        result.output_path
                    ),
                    "sample_rate": (
                        result.sample_rate
                    ),
                    "channels": (
                        result.channels
                    ),
                    "duration_seconds": (
                        result.duration_seconds
                    ),
                    "recording_ms": round(
                        result.recording_ms,
                        2,
                    ),
                    "rms": round(
                        result.rms,
                        6,
                    ),
                    "peak": round(
                        result.peak,
                        6,
                    ),
                    "device": result.device,
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
                    "output_path": str(
                        output_path
                    ),
                    "error": str(exc),
                },
                ensure_ascii=False,
                indent=2,
            )
        )


if __name__ == "__main__":
    main()