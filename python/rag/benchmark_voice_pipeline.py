import argparse
import json
import platform
import statistics
from datetime import datetime
from pathlib import Path
from time import perf_counter
from typing import Any, Dict, List

import soundfile as sf

from rag.voice_io import (
    AsrBackend,
    TtsBackend,
    create_asr_backend,
    create_tts_backend,
)
from rag.voice_pipeline import ZmqRagClient


DEFAULT_NORMAL_TEXT = (
    "可以通过中控屏点击空调按钮，"
    "也可以使用语音指令打开空调。"
)

DEFAULT_MULTI_SENTENCE_TEXT = (
    "可以通过中控屏点击空调按钮打开空调。"
    "温度可以通过中控屏滑动条进行调节。"
    "也可以按下方向盘语音键进行语音控制。"
)


def round_ms(value: float) -> float:
    return round(value, 2)


def build_statistics(
    values: List[float],
) -> Dict[str, float]:
    return {
        "min": round_ms(min(values)),
        "median": round_ms(
            statistics.median(values)
        ),
        "mean": round_ms(
            statistics.mean(values)
        ),
        "max": round_ms(max(values)),
    }


def get_audio_info(
    output_path: Path,
) -> Dict[str, Any]:
    info = sf.info(str(output_path))

    duration_seconds = (
        info.frames / info.samplerate
    )

    return {
        "duration_seconds": round(
            duration_seconds,
            3,
        ),
        "sample_rate": info.samplerate,
        "frames": info.frames,
        "channels": info.channels,
        "format": info.format,
        "subtype": info.subtype,
    }


def benchmark_tts_case(
    name: str,
    text: str,
    tts: TtsBackend,
    output_dir: Path,
    runs: int,
) -> Dict[str, Any]:
    run_results = []

    for run_index in range(1, runs + 1):
        output_path = (
            output_dir
            / f"{name}_{run_index}.wav"
        )

        wall_start = perf_counter()

        result = tts.synthesize(
            text=text,
            output_path=output_path,
        )

        wall_ms = (
            perf_counter() - wall_start
        ) * 1000

        audio_info = get_audio_info(
            output_path
        )

        audio_duration_seconds = (
            audio_info["duration_seconds"]
        )

        inference_seconds = (
            result.duration_ms / 1000
        )

        if audio_duration_seconds > 0:
            rtf = (
                inference_seconds
                / audio_duration_seconds
            )
        else:
            rtf = 0.0

        run_results.append(
            {
                "run": run_index,
                "text": result.text,
                "text_length": len(result.text),
                "output_path": str(output_path),
                "inference_ms": round_ms(
                    result.duration_ms
                ),
                "wall_ms": round_ms(wall_ms),
                "rtf": round(rtf, 4),
                "audio": audio_info,
            }
        )

    inference_values = [
        item["inference_ms"]
        for item in run_results
    ]

    wall_values = [
        item["wall_ms"]
        for item in run_results
    ]

    rtf_values = [
        item["rtf"]
        for item in run_results
    ]

    return {
        "name": name,
        "text": text,
        "text_length": len(text),
        "runs": run_results,
        "summary": {
            "inference_ms": build_statistics(
                inference_values
            ),
            "wall_ms": build_statistics(
                wall_values
            ),
            "rtf": {
                "min": round(
                    min(rtf_values),
                    4,
                ),
                "median": round(
                    statistics.median(
                        rtf_values
                    ),
                    4,
                ),
                "mean": round(
                    statistics.mean(
                        rtf_values
                    ),
                    4,
                ),
                "max": round(
                    max(rtf_values),
                    4,
                ),
            },
        },
    }


def benchmark_full_pipeline(
    audio_path: Path,
    asr: AsrBackend,
    rag_client: ZmqRagClient,
    tts: TtsBackend,
    output_dir: Path,
    runs: int,
) -> Dict[str, Any]:
    run_results = []

    for run_index in range(1, runs + 1):
        output_path = (
            output_dir
            / f"full_pipeline_{run_index}.wav"
        )

        total_start = perf_counter()

        asr_start = perf_counter()
        asr_result = asr.transcribe(
            audio_path
        )
        asr_ms = (
            perf_counter() - asr_start
        ) * 1000

        rag_start = perf_counter()
        rag_result = rag_client.query(
            asr_result.text
        )
        rag_ms = (
            perf_counter() - rag_start
        ) * 1000

        if not rag_result.ok:
            run_results.append(
                {
                    "run": run_index,
                    "ok": False,
                    "asr_text": asr_result.text,
                    "error": rag_result.error,
                    "timings_ms": {
                        "asr": round_ms(asr_ms),
                        "rag": round_ms(rag_ms),
                    },
                }
            )
            continue

        tts_start = perf_counter()

        tts_result = tts.synthesize(
            text=rag_result.answer,
            output_path=output_path,
        )

        tts_wall_ms = (
            perf_counter() - tts_start
        ) * 1000

        total_ms = (
            perf_counter() - total_start
        ) * 1000

        audio_info = get_audio_info(
            output_path
        )

        audio_duration_seconds = (
            audio_info["duration_seconds"]
        )

        if audio_duration_seconds > 0:
            rtf = (
                tts_result.duration_ms
                / 1000
                / audio_duration_seconds
            )
        else:
            rtf = 0.0

        run_results.append(
            {
                "run": run_index,
                "ok": True,
                "audio_path": str(audio_path),
                "asr_backend": (
                    asr_result.backend
                ),
                "asr_text": asr_result.text,
                "rag_answer": (
                    rag_result.answer
                ),
                "rag_answer_length": len(
                    rag_result.answer
                ),
                "tts_backend": (
                    tts_result.backend
                ),
                "output_path": str(
                    output_path
                ),
                "rtf": round(rtf, 4),
                "audio": audio_info,
                "timings_ms": {
                    "asr": round_ms(asr_ms),
                    "rag": round_ms(rag_ms),
                    "tts_inference": round_ms(
                        tts_result.duration_ms
                    ),
                    "tts_wall": round_ms(
                        tts_wall_ms
                    ),
                    "total": round_ms(
                        total_ms
                    ),
                },
            }
        )

    successful_runs = [
        item
        for item in run_results
        if item.get("ok") is True
    ]

    summary: Dict[str, Any] = {
        "successful_runs": len(
            successful_runs
        ),
        "failed_runs": (
            len(run_results)
            - len(successful_runs)
        ),
    }

    if successful_runs:
        asr_values = [
            item["timings_ms"]["asr"]
            for item in successful_runs
        ]

        rag_values = [
            item["timings_ms"]["rag"]
            for item in successful_runs
        ]

        tts_values = [
            item["timings_ms"][
                "tts_inference"
            ]
            for item in successful_runs
        ]

        total_values = [
            item["timings_ms"]["total"]
            for item in successful_runs
        ]

        rtf_values = [
            item["rtf"]
            for item in successful_runs
        ]

        summary.update(
            {
                "asr_ms": build_statistics(
                    asr_values
                ),
                "rag_ms": build_statistics(
                    rag_values
                ),
                "tts_ms": build_statistics(
                    tts_values
                ),
                "total_ms": build_statistics(
                    total_values
                ),
                "rtf_median": round(
                    statistics.median(
                        rtf_values
                    ),
                    4,
                ),
            }
        )

    return {
        "name": "full_pipeline",
        "runs": run_results,
        "summary": summary,
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark normal TTS, "
            "multi-sentence TTS, and the "
            "full ASR-RAG-TTS pipeline."
        )
    )

    parser.add_argument(
        "--audio",
        default=(
            "voice_input/"
            "air_conditioner.wav"
        ),
        help="Audio used by full pipeline.",
    )

    parser.add_argument(
        "--asr-model",
        default="small",
    )

    parser.add_argument(
        "--asr-device",
        default="cpu",
        choices=["cpu", "cuda"],
    )

    parser.add_argument(
        "--asr-compute-type",
        default="int8",
    )

    parser.add_argument(
        "--tts-model-dir",
        default=(
            "models/tts/"
            "vits-melo-tts-zh_en"
        ),
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
        "--rag-endpoint",
        default="tcp://localhost:5556",
    )

    parser.add_argument(
        "--rag-timeout-ms",
        type=int,
        default=10000,
    )

    parser.add_argument(
        "--runs",
        type=int,
        default=3,
    )

    parser.add_argument(
        "--normal-text",
        default=DEFAULT_NORMAL_TEXT,
    )

    parser.add_argument(
        "--multi-text",
        default=(
            DEFAULT_MULTI_SENTENCE_TEXT
        ),
    )

    parser.add_argument(
        "--log-dir",
        default="perf_logs",
    )

    args = parser.parse_args()

    if args.runs <= 0:
        raise ValueError(
            f"--runs must be positive: "
            f"{args.runs}"
        )

    audio_path = Path(args.audio)

    if not audio_path.is_file():
        raise FileNotFoundError(
            f"Audio file not found: "
            f"{audio_path}"
        )

    timestamp = datetime.now().strftime(
        "%Y%m%d_%H%M%S"
    )

    log_dir = Path(args.log_dir)
    log_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    output_dir = (
        Path("voice_output")
        / f"perf_{timestamp}"
    )

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    asr_load_start = perf_counter()

    asr = create_asr_backend(
        backend="faster_whisper",
        model_name_or_path=args.asr_model,
        device=args.asr_device,
        compute_type=(
            args.asr_compute_type
        ),
        language="zh",
    )

    asr_load_ms = (
        perf_counter() - asr_load_start
    ) * 1000

    tts_load_start = perf_counter()

    tts = create_tts_backend(
        backend="sherpa_onnx",
        model_dir=args.tts_model_dir,
        num_threads=args.tts_num_threads,
        speaker_id=0,
        speed=args.tts_speed,
    )

    tts_load_ms = (
        perf_counter() - tts_load_start
    ) * 1000

    rag_client = ZmqRagClient(
        endpoint=args.rag_endpoint,
        timeout_ms=args.rag_timeout_ms,
    )

    # 模型预热，结果不计入性能统计
    tts.synthesize(
        text=args.normal_text,
        output_path=(
            output_dir / "warmup_tts.wav"
        ),
    )

    asr.transcribe(audio_path)

    normal_result = benchmark_tts_case(
        name="normal_answer",
        text=args.normal_text,
        tts=tts,
        output_dir=output_dir,
        runs=args.runs,
    )

    multi_result = benchmark_tts_case(
        name="multi_sentence_answer",
        text=args.multi_text,
        tts=tts,
        output_dir=output_dir,
        runs=args.runs,
    )

    pipeline_result = benchmark_full_pipeline(
        audio_path=audio_path,
        asr=asr,
        rag_client=rag_client,
        tts=tts,
        output_dir=output_dir,
        runs=args.runs,
    )

    log_data = {
        "timestamp": timestamp,
        "environment": {
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "configuration": {
            "runs": args.runs,
            "audio_path": str(audio_path),
            "asr_model": args.asr_model,
            "asr_device": args.asr_device,
            "asr_compute_type": (
                args.asr_compute_type
            ),
            "tts_model_dir": (
                args.tts_model_dir
            ),
            "tts_num_threads": (
                args.tts_num_threads
            ),
            "tts_speed": args.tts_speed,
            "rag_endpoint": (
                args.rag_endpoint
            ),
        },
        "model_load_ms": {
            "asr": round_ms(asr_load_ms),
            "tts": round_ms(tts_load_ms),
        },
        "results": {
            "normal_answer": normal_result,
            "multi_sentence_answer": (
                multi_result
            ),
            "full_pipeline": (
                pipeline_result
            ),
        },
    }

    log_path = (
        log_dir
        / f"voice_perf_{timestamp}.json"
    )

    with log_path.open(
        "w",
        encoding="utf-8",
    ) as file:
        json.dump(
            log_data,
            file,
            ensure_ascii=False,
            indent=2,
        )

    console_summary = {
        "log_path": str(log_path),
        "output_dir": str(output_dir),
        "model_load_ms": (
            log_data["model_load_ms"]
        ),
        "normal_answer": (
            normal_result["summary"]
        ),
        "multi_sentence_answer": (
            multi_result["summary"]
        ),
        "full_pipeline": (
            pipeline_result["summary"]
        ),
    }

    print(
        json.dumps(
            console_summary,
            ensure_ascii=False,
            indent=2,
        )
    )
    
    failed_pipeline_runs = (
        pipeline_result["summary"][
            "failed_runs"
        ]
    )
    
    if failed_pipeline_runs > 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()