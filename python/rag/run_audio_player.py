import argparse
import json
from pathlib import Path

from rag.audio_player import (
    AudioPlayer,
    parse_output_device,
)

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Play a WAV audio file."
    )

    parser.add_argument(
        "--audio",
        required=True,
    )

    parser.add_argument(
        "--device",
        default=None,
        help=(
            "Output device index or "
            "device name substring."
        ),
    )

    args = parser.parse_args()

    audio_path = Path(args.audio)

    try:
        player = AudioPlayer(
            device=parse_output_device(
                args.device
            )
        )

        result = player.play(audio_path)

        print(
            json.dumps(
                {
                    "ok": True,
                    "audio_path": (
                        result.audio_path
                    ),
                    "sample_rate": (
                        result.sample_rate
                    ),
                    "channels": (
                        result.channels
                    ),
                    "duration_seconds": round(
                        result.duration_seconds,
                        3,
                    ),
                    "playback_ms": round(
                        result.playback_ms,
                        2,
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
                    "audio_path": str(audio_path),
                    "error": str(exc),
                },
                ensure_ascii=False,
                indent=2,
            )
        )

if __name__ == "__main__":
    main()