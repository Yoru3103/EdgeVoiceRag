import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Any, Dict

import zmq

from rag.voice_io import (
    create_asr_backend,
    create_tts_backend,
)

from rag.microphone_io import (
    MicrophoneRecorder,
    parse_device,
)

from rag.vad_recorder import (
    VadMicrophoneRecorder,
)


@dataclass
class RagClientResult:
    ok: bool
    answer: str
    raw_response: str
    error: str = ""


class ZmqRagClient:
    def __init__(self, endpoint: str, timeout_ms: int) -> None:
        self.endpoint = endpoint
        self.timeout_ms = timeout_ms

    def query(self, text: str) -> RagClientResult:
        context = zmq.Context()
        socket = context.socket(zmq.REQ)

        socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)
        socket.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
        socket.setsockopt(zmq.LINGER, 0)

        try:
            socket.connect(self.endpoint)
            socket.send_string(text)

            return self._parse_response(socket.recv_string())

        except zmq.Again:
            return RagClientResult(
                ok=False,
                answer="",
                raw_response="",
                error=f"RAG request timed out after {self.timeout_ms} ms: {self.endpoint}",
            )

        except Exception as exc:
            return RagClientResult(
                ok=False,
                answer="",
                raw_response="",
                error=f"RAG request failed: {exc}",
            )

        finally:
            socket.close()
            context.term()

    @staticmethod
    def _parse_response(raw_response: str) -> RagClientResult:
        try:
            data: Dict[str, Any] = json.loads(raw_response)

            if data.get("ok") is False:
                error = data.get("error")
                if not isinstance(error, str) or not error.strip():
                    error = "RAG server returned ok=false"
                return RagClientResult(
                    ok=False,
                    answer="",
                    raw_response=raw_response,
                    error=error.strip(),
                )

            generated_answer = data.get("generated_answer")
            if isinstance(generated_answer, str) and generated_answer.strip():
                return RagClientResult(True, generated_answer.strip(), raw_response)

            answer = data.get("answer")
            if isinstance(answer, str) and answer.strip():
                return RagClientResult(True, answer.strip(), raw_response)

            return RagClientResult(
                ok=False,
                answer="",
                raw_response=raw_response,
                error="RAG response does not contain a non-empty answer",
            )

        except json.JSONDecodeError:
            return RagClientResult(True, raw_response, raw_response)


def build_voice_pipeline_response(
    audio_path: Path,
    asr_backend: str,
    asr_text: str,
    rag_answer: str,
    rag_raw_response: str,
    tts_backend: str,
    tts_output_path: str,
    timings_ms: Dict[str, float],
) -> Dict[str, Any]:
    return {
        "ok": True,
        "audio_path": str(audio_path),
        "asr_backend": asr_backend,
        "asr_text": asr_text,
        "rag_answer": rag_answer,
        "rag_raw_response": rag_raw_response,
        "tts_backend": tts_backend,
        "tts_output_path": str(tts_output_path),
        "timings_ms": timings_ms,
    }


def build_error_response(audio_path: Path, error: str) -> Dict[str, Any]:
    return {
        "ok": False,
        "audio_path": str(audio_path),
        "error": error,
    }


def print_json(data: Dict[str, Any]) -> None:
    print(json.dumps(data, ensure_ascii=False, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock offline voice pipeline: ASR -> RAG -> TTS.")
    parser.add_argument(
        "--audio",
        default="voice_input/mock.wav",
        help="Input audio path. In mock mode, this file does not need to exist.",
    )
    parser.add_argument(
        "--input-mode",
        default="file",
        choices=[
            "file",
            "microphone",
            "vad_microphone",
        ],
        help=(
            "Use an existing audio file "
            "or record from microphone."
        ),
    )
    parser.add_argument(
        "--record-duration",
        type=float,
        default=5.0,
        help="Microphone recording duration.",
    )
    parser.add_argument(
        "--record-sample-rate",
        type=int,
        default=16000,
    )
    parser.add_argument(
         "--record-channels",
         type=int,
         default=1,
    )
    parser.add_argument(
        "--microphone-device",
        default=None,
        help=(
            "Microphone device index "
            "or name substring."
        ),
    )
    parser.add_argument(
        "--microphone-minimum-rms",
        type=float,
        default=0.001,
    )
    parser.add_argument(
        "--vad-model",
        default="models/vad/silero_vad.onnx",
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
        "--mock-asr-text",
        default="",
        help="Text returned by mock ASR.",
    )
    parser.add_argument(
        "--rag-endpoint",
        default="tcp://localhost:5556",
        help="Python RAG server ZeroMQ endpoint.",
    )
    parser.add_argument(
        "--rag-timeout-ms",
        type=int,
        default=5000,
        help="RAG request timeout in milliseconds.",
    )
    parser.add_argument(
        "--asr-backend",
        default="mock",
        choices=["mock", "faster_whisper"],
        help="ASR backend.",
    )
    parser.add_argument(
        "--asr-model",
        default="small",
        help=(
            "Faster-Whisper model name or local model path. "
            "Examples: tiny, base, small, models/asr/whisper-small."
        ),
    )
    parser.add_argument(
        "--asr-device",
        default="cpu",
        choices=["cpu", "cuda"],
        help="Device used by the ASR model.",
    )
    parser.add_argument(
        "--asr-compute-type",
        default="int8",
        help=(
            "Faster-Whisper compute type. "
            "CPU usually uses int8; CUDA can use float16."
        ),
    )
    parser.add_argument(
        "--asr-language",
        default="zh",
        help="ASR language code.",
    )
    parser.add_argument(
        "--tts-backend",
        default="mock",
        choices=["mock", "sherpa_onnx"],
        help="TTS backend.",
    )
    parser.add_argument(
        "--tts-model-dir",
        default="models/tts/vits-melo-tts-zh_en",
        help="Sherpa-ONNX TTS model directory.",
    )
    parser.add_argument(
        "--tts-num-threads",
        type=int,
        default=2,
        help="Number of TTS inference threads.",
    )
    parser.add_argument(
        "--tts-speaker-id",
        type=int,
        default=0,
        help="TTS speaker ID.",
    )
    parser.add_argument(
        "--tts-speed",
        type=float,
        default=1.0,
        help="TTS speech speed.",
    )
    parser.add_argument(
        "--tts-debug",
        action="store_true",
        help="Enable TTS debug output.",
    )
    parser.add_argument(
        "--tts-output",
        default="voice_output/answer.txt",
        help="Mock TTS output path.",
    )

    args = parser.parse_args()

    audio_path = Path(args.audio)
    tts_output_path = Path(args.tts_output)

    try:
        pipeline_start = perf_counter()
        recording_ms = 0.0

        if args.input_mode == "microphone":
            recorder = MicrophoneRecorder(
                sample_rate=args.record_sample_rate,
                channels=args.record_channels,
                device=parse_device(args.microphone_device),
                minimum_rms=args.microphone_minimum_rms,
            )

            recording_result = recorder.record(
                duration_seconds=args.record_duration,
                output_path=audio_path,
            )

            recording_ms = recording_result.recording_ms
        elif args.input_mode == "vad_microphone":
            recorder = VadMicrophoneRecorder(
                model_path=args.vad_model,
                device=parse_device(
                    args.microphone_device
                ),
                threshold=args.vad_threshold,
                min_silence_duration=(
                    args.vad_min_silence
                ),
                max_wait_seconds=args.vad_max_wait,
            )

            recording_result = recorder.record(
                output_path=audio_path,
            )

            recording_ms = recording_result.recording_ms

        asr = create_asr_backend(
            backend=args.asr_backend,
            mock_text=args.mock_asr_text,
            model_name_or_path=args.asr_model,
            device=args.asr_device,
            compute_type=args.asr_compute_type,
            language=args.asr_language,
        )

        tts = create_tts_backend(
            backend=args.tts_backend,
            model_dir=args.tts_model_dir,
            num_threads=args.tts_num_threads,
            speaker_id=args.tts_speaker_id,
            speed=args.tts_speed,
            debug=args.tts_debug,
        )

        rag_client = ZmqRagClient(
            endpoint=args.rag_endpoint,
            timeout_ms=args.rag_timeout_ms,
        )

        asr_start = perf_counter()
        asr_result = asr.transcribe(audio_path)
        asr_ms = (perf_counter() - asr_start) * 1000

        rag_start = perf_counter()
        rag_result = rag_client.query(asr_result.text)
        rag_ms = (perf_counter() - rag_start) * 1000

        if not rag_result.ok:
            print_json(build_error_response(audio_path, rag_result.error))
            return

        tts_start = perf_counter()
        tts_result = tts.synthesize(
            text=rag_result.answer,
            output_path=tts_output_path,
        )
        tts_ms = (perf_counter() - tts_start) * 1000
        total_ms = (perf_counter() - pipeline_start) * 1000

        response = build_voice_pipeline_response(
            audio_path=audio_path,
            asr_backend=asr_result.backend,
            asr_text=asr_result.text,
            rag_answer=rag_result.answer,
            rag_raw_response=rag_result.raw_response,
            tts_backend=tts_result.backend,
            tts_output_path=tts_result.output_path,
            timings_ms={
                "recording": round(
                    recording_ms,
                    2,
                ),
                "asr": round(asr_ms, 2),
                "rag": round(rag_ms, 2),
                "tts": round(tts_ms, 2),
                "total": round(total_ms, 2),
            },
        )

        print_json(response)

    except Exception as exc:
        print_json(build_error_response(audio_path, str(exc)))


if __name__ == "__main__":
    main()
