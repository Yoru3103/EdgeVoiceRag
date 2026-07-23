import argparse

from rag.voice_assistant import (
    VoiceAssistant,
    VoiceAssistantConfig,
)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Run persistent multi-turn "
            "voice assistant."
        )
    )

    parser.add_argument(
        "--vad-model",
        default="models/vad/silero_vad.onnx",
    )

    parser.add_argument(
        "--asr-model",
        default="small",
    )

    parser.add_argument(
        "--tts-model-dir",
        default=(
            "models/tts/"
            "vits-melo-tts-zh_en"
        ),
    )

    parser.add_argument(
        "--rag-endpoint",
        default="tcp://localhost:5556",
    )

    parser.add_argument(
        "--microphone-device",
        default=None,
    )

    parser.add_argument(
        "--output-device",
        default=None,
    )

    parser.add_argument(
        "--vad-threshold",
        type=float,
        default=0.25,
    )
    parser.add_argument(
        "--vad-min-silence",
        type=float,
        default=0.8,
    )
    parser.add_argument(
        "--vad-max-wait",
        type=float,
        default=10.0,
    )

    parser.add_argument(
        "--tts-num-threads",
        type=int,
        default=2,
    )
    parser.add_argument(
        "--tts-speed",
        type=float,
        default=1.0,
    )
    parser.add_argument(
        "--max-turns",
        type=int,
        default=0,
        help=(
            "0 means unlimited turns."
        ),
    )
    parser.add_argument(
        "--rag-stream-endpoint",
        default="tcp://localhost:5557",
    )
    parser.add_argument(
        "--rag-timeout-ms",
        type=int,
        default=120000,
    )
    parser.add_argument(
        "--stream-sentence-max-chars",
        type=int,
        default=60,
    )

    args = parser.parse_args()

    config = VoiceAssistantConfig(
        vad_model=args.vad_model,
        asr_model=args.asr_model,
        tts_model_dir=(
            args.tts_model_dir
        ),
        rag_endpoint=args.rag_endpoint,
        rag_stream_endpoint=args.rag_stream_endpoint,
        stream_sentence_max_chars=args.stream_sentence_max_chars,
        rag_timeout_ms=args.rag_timeout_ms,
        microphone_device=(
            args.microphone_device
        ),
        output_device=args.output_device,
        vad_threshold=args.vad_threshold,
        vad_min_silence=(
            args.vad_min_silence
        ),
        vad_max_wait=args.vad_max_wait,
        tts_num_threads=(
            args.tts_num_threads
        ),
        tts_speed=args.tts_speed,
    )

    assistant = VoiceAssistant(config)

    assistant.run_loop(
        max_turns=args.max_turns
    )


if __name__ == "__main__":
    main()