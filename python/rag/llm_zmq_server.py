import argparse
import json
import signal
from time import perf_counter
from typing import Any, Dict

import requests
import zmq

PROTOCOL_VERSION = 1

class LlmZmqServer:
    def __init__(
        self,
        endpoint: str,
        backend: str,
        model: str,
        ollama_url: str,
        timeout_seconds: int,
    ) -> None:
        self.endpoint = endpoint
        self.backend = backend
        self.model = model
        self.ollama_url = ollama_url.rstrip("/")
        self.timeout_seconds = timeout_seconds

        self.running = False

        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REP)

        self.socket.setsockopt(
            zmq.LINGER,
            0,
        )

    def start(self) -> None:
        self.socket.bind(self.endpoint)
        self.running = True

        print("[INFO] LLM ZeroMQ server started")
        print(f"[INFO] Endpoint: {self.endpoint}")
        print(f"[INFO] Backend: {self.backend}")
        print(f"[INFO] Model: {self.model}")

        while self.running:
            try:
                message = self.socket.recv_string()

                response = self.handle_message(message)

                self.socket.send_string(
                    json.dumps(
                        response,
                        ensure_ascii=False,
                    )
                )
            except KeyboardInterrupt:
                break
            except zmq.ZMQError as exc:
                if self.running:
                    print(
                        "[ERROR] ZeroMQ failure: "
                        f"{exc}"
                    )
                break

        self.stop()

    def handle_message(
        self,
        message: str,
    ) -> Dict[str, Any]:
        request_id = ""

        try:
            request = json.loads(message)

            request_id = str(
                request.get("request_id", "")
            )

            self.validate_request(request)

            prompt = str(request["prompt"])

            start = perf_counter()

            if self.backend == "mock":
                answer = self.generate_mock(prompt)
            elif self.backend == "ollama":
                answer = self.generate_ollama(prompt)
            else:
                raise ValueError(
                    "unsupported backend: "
                    f"{self.backend}"
                )

            elapsed_ms = (
                perf_counter() - start
            ) * 1000

            return {
                "version": PROTOCOL_VERSION,
                "type": "generation_result",
                "ok": True,
                "request_id": request_id,
                "answer": answer,
                "backend": self.backend,
                "error": "",
                "elapsed_ms": round(
                    elapsed_ms,
                    2,
                ),
                "finished": True,
            }

        except Exception as exc:
            return {
                "version": PROTOCOL_VERSION,
                "type": "generation_result",
                "ok": False,
                "request_id": request_id,
                "answer": "",
                "backend": self.backend,
                "error": str(exc),
                "elapsed_ms": 0.0,
                "finished": True,
            }

    @staticmethod
    def validate_request(
        request: Dict[str, Any]
    ) -> None:
        if request.get("version") != PROTOCOL_VERSION:
            raise ValueError(
                "unsupported protocol version"
            )

        if request.get("type") != "generate":
            raise ValueError(
                "unsupported request type"
            )

        request_id = request.get("request_id")

        if not isinstance(request_id, str) or not request_id:
            raise ValueError(
                "missing request_id"
            )

        prompt = request.get("prompt")

        if (
            not isinstance(prompt, str)
            or not prompt.strip()
        ):
            raise ValueError(
                "missing prompt"
            )

        if request.get("stream", False):
            raise ValueError(
                "streaming is not supported yet"
            )

    @staticmethod
    def generate_mock(prompt: str) -> str:
        preview = prompt.strip()

        if len(preview) > 60:
            preview = preview[:60] + "..."

        return (
            "这是Mock LLM回答，收到的问题是："
            + preview
        )

    def generate_ollama(
        self,
        prompt: str,
    ) -> str:
        url = (
            f"{self.ollama_url}"
            "/api/generate"
        )

        response = requests.post(
            url,
            json={
                "model": self.model,
                "prompt": prompt,
                "stream": False,
                "options": {
                    "temperature": 0.2,
                },
            },
            timeout=self.timeout_seconds,
        )

        response.raise_for_status()

        payload = response.json()

        answer = str(
            payload.get(
                "response",
                "",
            )
        ).strip()

        if not answer:
            raise RuntimeError(
                "Ollama returned empty answer"
            )

        return answer

    def stop(self) -> None:
        if not self.running:
            return

        self.running = False

        self.socket.close(linger=0)
        self.context.term()

        print("[INFO] LLM ZeroMQ server stopped")

def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Run independent LLM ZeroMQ "
            "server."
        )
    )

    parser.add_argument(
        "--endpoint",
        default="tcp://*:8899",
    )
    parser.add_argument(
        "--backend",
        choices=["mock", "ollama"],
        default="mock",
    )
    parser.add_argument(
        "--model",
        default="qwen2.5:3b",
    )
    parser.add_argument(
        "--ollama-url",
        default="http://localhost:11434",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=60,
    )

    args = parser.parse_args()

    server = LlmZmqServer(
        endpoint=args.endpoint,
        backend=args.backend,
        model=args.model,
        ollama_url=args.ollama_url,
        timeout_seconds=(
            args.timeout_seconds
        ),
    )

    def stop_server(
        signal_number: int,
        frame: object,
    ) -> None:
        del signal_number
        del frame

        server.stop()

    signal.signal(
        signal.SIGINT,
        stop_server,
    )

    signal.signal(
        signal.SIGTERM,
        stop_server,
    )

    server.start()

if __name__ == "__main__":
    main()