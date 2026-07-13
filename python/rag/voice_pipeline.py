import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict

import zmq

from voice_io import create_asr_backend, create_tts_backend


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

            raw_response = socket.recv_string()
            answer = self._extract_answer(raw_response)

            return RagClientResult(
                ok=True,
                answer=answer,
                raw_response=raw_response,
            )

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
    def _extract_answer(raw_response: str) -> str:
        try:
            data: Dict[str, Any] = json.loads(raw_response)

            generated_answer = data.get("generated_answer")
            if isinstance(generated_answer, str) and generated_answer.strip():
                return generated_answer.strip()

            answer = data.get("answer")
            if isinstance(answer, str) and answer.strip():
                return answer.strip()

            error = data.get("error")
            if isinstance(error, str) and error.strip():
                return f"Python RAG 检索失败：{error.strip()}"

            return raw_response

        except json.JSONDecodeError:
            return raw_response


def build_voice_pipeline_response(
    audio_path: Path,
    asr_backend: str,
    asr_text: str,
    rag_answer: str,
    rag_raw_response: str,
    tts_backend: str,
    tts_output_path: str,
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

        asr_result = asr.transcribe(audio_path)
        rag_result = rag_client.query(asr_result.text)

        if not rag_result.ok:
            print_json(build_error_response(audio_path, rag_result.error))
            return

        tts_result = tts.synthesize(
            text=rag_result.answer,
            output_path=tts_output_path,
        )

        response = build_voice_pipeline_response(
            audio_path=audio_path,
            asr_backend=asr_result.backend,
            asr_text=asr_result.text,
            rag_answer=rag_result.answer,
            rag_raw_response=rag_result.raw_response,
            tts_backend=tts_result.backend,
            tts_output_path=tts_result.output_path,
        )

        print_json(response)

    except Exception as exc:
        print_json(build_error_response(audio_path, str(exc)))


if __name__ == "__main__":
    main()
