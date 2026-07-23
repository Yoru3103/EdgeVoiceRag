import json
import uuid
from dataclasses import dataclass
from typing import Any, Dict, Iterator

import zmq

PROTOCOL_VERSION = 1

@dataclass
class LlmZmqResult:
    answer: str
    backend: str
    elapsed_ms: float

@dataclass
class LlmZmqStreamEvent:
    type: str
    request_id: str
    sequence: int
    delta: str
    answer: str
    backend: str
    elapsed_ms: float
    finished: bool
    error: str

class LlmZmqClient:
    def __init__(
        self,
        endpoint: str,
        timeout_seconds: int = 60,
        stream_endpoint: str = (
            "tcp://127.0.0.1:8900"
        ),
    ) -> None:
        if not endpoint:
            raise ValueError("LLM endpoint must not be empty")

        if not stream_endpoint:
            raise ValueError("LLM stream endpoint must not be empty")

        if timeout_seconds <= 0:
            raise ValueError("LLM timeout must be greater than zero")

        self.endpoint = endpoint
        self.timeout_ms = timeout_seconds * 1000
        self.stream_endpoint = stream_endpoint

    def generate(self, prompt: str) -> LlmZmqResult:
        if not prompt.strip():
            raise ValueError("LLM prompt must not be empty")

        request_id = uuid.uuid4().hex

        request = {
            "version": PROTOCOL_VERSION,
            "type": "generate",
            "request_id": request_id,
            "prompt": prompt,
            "stream": False,
        }

        context = zmq.Context()
        socket = context.socket(zmq.REQ)

        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
        socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)

        try:
            socket.connect(self.endpoint)
            socket.send_string(
                json.dumps(request, ensure_ascii=False)
            )

            raw_response = socket.recv_string()
            response = self._decode_response(
                raw_response=raw_response,
                expected_request_id=request_id,
            )

            return LlmZmqResult(
                answer=response["answer"].strip(),
                backend=response["backend"],
                elapsed_ms=float(response["elapsed_ms"])
            )
        except zmq.Again as exc:
            raise RuntimeError(
                "LLM request timed out after "
                f"{self.timeout_ms} ms: {self.endpoint}"
            ) from exc
        except zmq.ZMQError as exc:
            raise RuntimeError(
                f"LLM ZeroMQ request failed: {exc}"
            ) from exc
        finally:
            socket.close(linger=0)
            context.term()

    def generate_stream(
        self,
        prompt: str
    ) -> Iterator[LlmZmqStreamEvent]:
        if not prompt.strip():
            raise ValueError("LLM prompt must not be empty")

        # 生成全局唯一标识符，.hex转换成不带字符的32位十六进制字符串
        request_id = uuid.uuid4().hex

        request = {
            "version": PROTOCOL_VERSION,
            "type": "generate",
            "request_id": request_id,
            "prompt": prompt,
            "stream": True,
        }

        context = zmq.Context()
        socket = context.socket(zmq.DEALER)

        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
        socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)

        expected_sequence = 0
        accumulated_answer = ""

        try:
            socket.connect(self.stream_endpoint)

            socket.send_string(
                json.dumps(
                    request,
                    ensure_ascii=False,
                )
            )

            while True:
                raw_response = socket.recv_string()

                event = self._decode_stream_event(
                    raw_response=raw_response,
                    expected_request_id=request_id,
                )

                if event.sequence != expected_sequence:
                    raise RuntimeError(
                        "LLM stream sequence mismatch: "
                        f"expected {expected_sequence}, "
                        f"received {event.sequence}"
                    )

                if event.type == "generation_error":
                    raise RuntimeError(event.error)

                if event.type == "generation_chunk":
                    accumulated_answer += event.delta
                    expected_sequence += 1

                    yield event
                    continue

                if event.type == "generation_finished":
                    if accumulated_answer != event.answer:
                        raise RuntimeError(
                            "LLM stream chunks do not "
                            "match final answer"
                        )

                    yield event
                    return

                raise RuntimeError(
                    "unsupported LLM stream event: "
                    f"{event.type}"
                )
        except zmq.Again as exc:
            raise RuntimeError(
                 "LLM stream request timed out after "
                f"{self.timeout_ms} ms: "
                f"{self.stream_endpoint}"
            ) from exc
        except zmq.ZMQError as exc:
            raise RuntimeError(
                "LLM stream ZeroMQ request failed: "
                f"{exc}"
            ) from exc
        finally:
            socket.close(linger=0)
            context.term()

    @staticmethod
    def _decode_response(
        raw_response: str,
        expected_request_id: str,
    ) -> Dict[str, Any]:
        try:
            response = json.loads(raw_response)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"LLM returned invalid JSON: {exc}"
            ) from exc

        if not isinstance(response, dict):
            raise RuntimeError("LLM response must be a JSON object")

        if response.get("version") != PROTOCOL_VERSION:
            raise RuntimeError("unsupported LLM protocol version")

        if response.get("type") != "generation_result":
            raise RuntimeError("unsupported LLM response type")

        request_id = response.get("request_id")

        if request_id != expected_request_id:
            raise RuntimeError(
                "LLM response request_id does not match request"
            )

        if response.get("finished") is not True:
            raise RuntimeError(
                "non-streaming LLM response is not finished"
            )

        if response.get("ok") is not True:
            error = response.get("error")

            if not isinstance(error, str) or not error.strip():
                error = "LLM server returned ok=false"

            raise RuntimeError(error)

        answer = response.get("answer")

        if not isinstance(answer, str) or not answer.strip():
            raise RuntimeError(
                "successful LLM response has empty answer"
            )

        backend = response.get("backend")

        if not isinstance(backend, str) or not backend.strip():
            raise RuntimeError(
                "LLM response has empty backend"
            )

        elapsed_ms = response.get("elapsed_ms", 0.0)

        if not isinstance(elapsed_ms, (int, float)):
            raise RuntimeError(
                "LLM response elapsed_ms must be numeric"
            )

        return response

    @staticmethod
    def _decode_stream_event(
        raw_response: str,
        expected_request_id: str,
    ) -> LlmZmqStreamEvent:
        try:
            response = json.loads(raw_response)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"LLM returned invalid JSON: {exc}"
            ) from exc

        if not isinstance(response, dict):
            raise RuntimeError(
                "LLM stream event must be "
                "a JSON object"
            )

        if response.get("version") != PROTOCOL_VERSION:
            raise RuntimeError(
                "unsupported LLM protocol version"
            )

        event_type = response.get("type")

        if event_type not in {
            "generation_chunk",
            "generation_finished",
            "generation_error",
        }:
            raise RuntimeError("unsupported LLM stream event type")

        if (response.get("request_id") != expected_request_id):
            raise RuntimeError(
                "LLM stream request_id does not "
                "match request"
            )

        sequence = response.get("sequence")

        if (
            not isinstance(sequence, int) or
            isinstance(sequence, bool) or
            sequence < 0
        ):
            raise RuntimeError(
                "LLM stream sequence must be "
                "a non-negative integer"
            )

        delta = response.get("delta", "")
        answer = response.get("answer", "")
        backend = response.get("backend", "")
        error = response.get("error", "")
        finished = response.get("finished")
        elapsed_ms = response.get(
            "elapsed_ms",
            0.0,
        )

        if (
            not isinstance(backend, str) or
            not backend.strip()
        ):
            raise RuntimeError(
                "LLM stream backend is empty"
            )

        if (
            not isinstance(elapsed_ms, (int, float)) or
            isinstance(elapsed_ms, bool)
        ):
            raise RuntimeError(
                "LLM stream elapsed_ms must "
                "be numeric"
            )

        if event_type == "generation_chunk":
            if (
                response.get("ok") is not True or
                not isinstance(delta, str) or
                not delta or
                finished is not False
            ):
                raise RuntimeError(
                    "invalid LLM stream chunk"
                )

        elif event_type == "generation_finished":
            if (
                response.get("ok") is not True or
                not isinstance(answer, str) or
                not answer or
                finished is not True
            ):
                raise RuntimeError(
                    "invalid LLM finished event"
                )

        else:
            if (
                response.get("ok") is not False or
                not isinstance(error, str) or
                not error or
                finished is not True
            ):
                raise RuntimeError(
                    "invalid LLM error event"
                )

        return LlmZmqStreamEvent(
            type=event_type,
            request_id=expected_request_id,
            sequence=sequence,
            delta=delta,
            answer=answer,
            backend=backend,
            elapsed_ms=float(elapsed_ms),
            finished=finished,
            error=error,
        )
