import argparse
import json
import statistics
from pathlib import Path
from time import perf_counter

import soundfile as sf

from rag.voice_io import create_tts_backend

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Benchmark Sherpa-ONNX TTS."
    )
    
    parser.add_argument(
        "--text",
        required=True,
        help="Benchmark Sherpa-ONNX TTS.",
    )
    parser.add_argument(
        "--model-dir",
        default="models/tts/vits-melo-tts-zh_en",
    )
    parser.add_argument(
        "--output",
        default="voice_output/benchmark.wav",
    )
    parser.add_argument(
        "--num-threads",
        type=int,
        default=2,
    )
    parser.add_argument(
        "--speaker-id",
        type=int,
        default=0,
    )
    parser.add_argument(
        "--speed",
        type=float,
        default=1.0,
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
    )
    
    args = parser.parse_args()
    
    if args.runs <= 0:
        raise ValueError(
            f"--runs must be positive: {args.runs}"
        )
        
    output_path = Path(args.output)
    
    model_start = perf_counter()
    
    tts = create_tts_backend(
        backend="sherpa_onnx",
        model_dir=args.model_dir,
        num_threads=args.num_threads,
        speaker_id=args.speaker_id,
        speed=args.speed,
    )
    
    model_load_ms = (
        perf_counter() - model_start
    ) * 1000
    
    # 预热
    tts.synthesize(
        text=args.text,
        output_path=output_path,
    )
    
    inference_times_ms = []
    wall_times_ms = []
    rtfs = []
    
    for _ in range(args.runs):
        wall_start = perf_counter()
        
        result = tts.synthesize(
            text=args.text,
            output_path=output_path,
        )
        
        wall_ms = (
            perf_counter() - wall_start
        ) * 1000
        
        audio_info = sf.info(
            str(output_path)
        )
        
        audio_duration_seconds = (
            audio_info.frames
            / audio_info.samplerate
        )
        
        inference_seconds = (
            result.duration_ms / 1000
        )
        
        rtf = (
            inference_seconds
            / audio_duration_seconds
        )
        
        inference_times_ms.append(
            result.duration_ms
        )
        
        wall_times_ms.append(
            wall_ms
        )
        
        rtfs.append(rtf)
        
    response = {
        "model_load_ms": round(
            model_load_ms,
            2,
        ),
        "runs": args.runs,
        "num_threads": args.num_threads,
        "speed": args.speed,
        "text_length": len(args.text),
        "audio_duration_seconds": round(
            audio_duration_seconds,
            3,
        ),
        "inference_ms": {
            "min": round(
                min(inference_times_ms),
                2,
            ),
            "median": round(
                statistics.median(
                    inference_times_ms
                ),
                2,
            ),
            "mean": round(
                statistics.mean(
                    inference_times_ms
                ),
                2,
            ),
            "max": round(
                max(inference_times_ms),
                2,
            ),
        },
        "wall_ms": {
            "median": round(
                statistics.median(
                    wall_times_ms
                ),
                2,
            ),
            "mean": round(
                statistics.mean(
                    wall_times_ms
                ),
                2,
            ),
        },
        "rtf": {
            "median": round(
                statistics.median(rtfs),
                4,
            ),
            "mean": round(
                statistics.mean(rtfs),
                4,
            ),
        },
        "output_path": str(output_path),
    }
    
    print(
        json.dumps(
            response,
            ensure_ascii=False,
            indent=2,
        )
    )
    
if __name__ == "__main__":
    main()
        
    