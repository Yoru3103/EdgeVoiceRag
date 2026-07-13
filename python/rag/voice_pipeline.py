import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter
from typing import Any, Dict

import zmq

from rag.voice_io import create_asr_backend, create_tts_backend


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
        "--mock-asr-text",
        required=True,
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
        choices=["mock"],
        help="ASR backend.",
    )
    parser.add_argument(
        "--tts-backend",
        default="mock",
        choices=["mock"],
        help="TTS backend.",
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
        asr = create_asr_backend(
            backend=args.asr_backend,
            mock_text=args.mock_asr_text,
        )

        tts = create_tts_backend(
            backend=args.tts_backend,
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
