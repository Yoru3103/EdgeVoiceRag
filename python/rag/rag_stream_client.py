import json
import uuid
from dataclasses import dataclass
from typing import Any, Dict, Iterator

import zmq

PROTOCOL_VERSION = 1

@dataclass
class RagStreamEvent:
    type: str
    request_id: str
    sequence: int
    delta: str
    answer: str
    backend: str
    llm_backend: str
    error: str
    elapsed_ms: float
    finished: bool

# 生成唯一request_id，使用DEALER连接RAG ROUTER， 校验检查，拼接chunk并检查
class RagStreamClient:
    def __init__(
        self,
        endpoint: str,
        control_endpoint: str = "tcp://localhost:5558",
        timeout_ms: int = 120000,
        control_timeout_ms: int = 2000,
    ) -> None:
        if not endpoint.strip():
            raise ValueError("RAG stream endpoint must not be empty")

        if timeout_ms <= 0:
            raise ValueError("RAG stream timeout must be greater than zero")

        if not control_endpoint.strip():
            raise ValueError("RAG control endpoint must not be empty")

        if control_timeout_ms <= 0:
            raise ValueError(
                "RAG control timeout must be greater than zero"
            )

        self.endpoint = endpoint
        self.control_endpoint = control_endpoint
        self.timeout_ms = timeout_ms
        self.control_timeout_ms = control_timeout_ms

    def query(
        self,
        query: str,
        request_id: str | None = None,
    ) -> Iterator[RagStreamEvent]:
        normalized_query = query.strip()

        if not normalized_query:
            raise ValueError("RAG stream query must not be empty")

        if request_id is None:
            request_id = uuid.uuid4().hex
        else:
            request_id = request_id.strip()

            if not request_id:
                raise ValueError(
                    "RAG stream request_id must not be empty"
                )

        request = {
            "version": PROTOCOL_VERSION,
            "type": "rag_query",
            "request_id": request_id,
            "query": normalized_query,
            "stream": True,
        }

        # 创建一个异步客户端。（发送请求不需要停在那里等待响应，之后通过接收或poll获取响应）
        context = zmq.Context()
        socket = context.socket(zmq.DEALER)

        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(
            zmq.SNDTIMEO,
            self.timeout_ms,
        )
        socket.setsockopt(
            zmq.RCVTIMEO,
            self.timeout_ms,
        )

        expected_sequence = 0
        accumulated_answer = ""

        try:
            socket.connect(self.endpoint)

            socket.send_string(
                json.dumps(
                    request,
                    ensure_ascii=False,
                )
            )

            while True:
                raw_response = socket.recv_string()

                event = self.decode_event(
                    raw_response=raw_response,
                    expected_request_id=request_id,
                )

                if event.sequence != expected_sequence:
                    raise RuntimeError(
                        "RAG stream sequence mismatch: "
                        f"expected {expected_sequence}, "
                        f"received {event.sequence}"
                    )

                if event.type == "rag_error":
                    raise RuntimeError(event.error)

                if event.type == "rag_chunk":
                    accumulated_answer += event.delta
                    expected_sequence += 1

                    yield event
                    continue

                if event.type == "rag_finished":
                    if accumulated_answer != event.answer:
                        raise RuntimeError(
                            "RAG stream chunks do not "
                            "match final answer"
                        )

                    yield event
                    return

                raise RuntimeError(
                    "unsupported RAG stream event: "
                    f"{event.type}"
                )
        except zmq.Again as exc:
            raise RuntimeError(
                "RAG stream request timed out after "
                f"{self.timeout_ms} ms: "
                f"{self.endpoint}"
            ) from exc

        except zmq.ZMQError as exc:
            raise RuntimeError(
                "RAG stream ZeroMQ request failed: "
                f"{exc}"
            ) from exc
        finally:
            socket.close(linger=0)
            context.term()

    def cancel(self, request_id: str) -> bool:
        normalized_request_id = request_id.strip()

        if not normalized_request_id:
            raise ValueError("RAG cancel request_id must not be empty")

        context = zmq.Context()
        socket = context.socket(zmq.REQ)
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, self.control_timeout_ms)
        socket.setsockopt(zmq.RCVTIMEO, self.control_timeout_ms)

        try:
            socket.connect(self.control_endpoint)
            socket.send_string(
                json.dumps(
                    {
                        "version": PROTOCOL_VERSION,
                        "type": "cancel",
                        "request_id": normalized_request_id,
                    },
                    ensure_ascii=False,
                )
            )

            return self.decode_cancel_response(
                raw_response=socket.recv_string(),
                expected_request_id=normalized_request_id,
            )
        except zmq.Again as exc:
            raise RuntimeError(
                "RAG cancellation timed out after "
                f"{self.control_timeout_ms} ms: "
                f"{self.control_endpoint}"
            ) from exc
        except zmq.ZMQError as exc:
            raise RuntimeError(
                "RAG cancellation ZeroMQ request failed: "
                f"{exc}"
            ) from exc
        finally:
            socket.close(linger=0)
            context.term()

    @staticmethod
    def decode_cancel_response(
        raw_response: str,
        expected_request_id: str,
    ) -> bool:
        try:
            response = json.loads(raw_response)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"RAG returned invalid cancel JSON: {exc}"
            ) from exc

        if not isinstance(response, dict):
            raise RuntimeError(
                "RAG cancel response must be a JSON object"
            )

        if response.get("version") != PROTOCOL_VERSION:
            raise RuntimeError(
                "unsupported RAG control protocol version"
            )

        if response.get("type") != "cancel_result":
            raise RuntimeError("unsupported RAG cancel response type")

        if response.get("ok") is not True:
            raise RuntimeError(
                response.get("error", "RAG cancellation failed")
            )

        if response.get("request_id") != expected_request_id:
            raise RuntimeError("RAG cancel response request_id mismatch")

        cancelled = response.get("cancelled")

        if not isinstance(cancelled, bool):
            raise RuntimeError("RAG cancelled must be boolean")

        return cancelled

    @staticmethod
    def decode_event(
        raw_response: str,
        expected_request_id: str,
    ) -> RagStreamEvent:
        try:
            data: Dict[str, Any] = json.loads(raw_response)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"RAG returned invalid JSON: {exc}"
            ) from exc

        if not isinstance(data, dict):
            raise RuntimeError(
                "RAG stream event must be "
                "a JSON object"
            )

        if data.get("version") != PROTOCOL_VERSION:
            raise RuntimeError("unsupported RAG protocol version")

        event_type = data.get("type")

        if event_type not in {
            "rag_chunk",
            "rag_finished",
            "rag_error",
        }:
            raise RuntimeError("unsupported RAG stream event type")

        request_id = data.get("request_id")

        if request_id != expected_request_id:
            raise RuntimeError(
                "RAG stream request_id does not "
                "match request"
            )

        sequence = data.get("sequence")

        if (
            not isinstance(sequence, int) or
            isinstance(sequence, bool) or
            sequence < 0
        ):
            raise RuntimeError(
                "RAG stream sequence must be "
                "a non-negative integer"
            )

        delta = data.get("delta", "")
        answer = data.get("answer", "")
        backend = data.get("backend", "")
        llm_backend = data.get("llm_backend", "")
        error = data.get("error", "")
        elapsed_ms = data.get("elapsed_ms", 0.0)
        finished = data.get("finished")

        if (
            not isinstance(backend, str) or
            not backend.strip()
        ):
            raise RuntimeError("RAG stream backend is empty")

        if (
            not isinstance(llm_backend, str) or
            not llm_backend.strip()
        ):
            raise RuntimeError("RAG stream LLM backend is empty")

        if (
            not isinstance(elapsed_ms, (int, float)) or
            isinstance(elapsed_ms, bool) or
            elapsed_ms < 0
        ):
            raise RuntimeError(
                "RAG stream elapsed_ms must be "
                "a non-negative number"
            )

        if event_type == "rag_chunk":
            if (
                data.get("ok") is not True or
                not isinstance(delta, str) or
                not delta or
                finished is not False
            ):
                raise RuntimeError("invalid RAG chunk event")

        elif event_type == "rag_finished":
            if (
                data.get("ok") is not True or
                not isinstance(answer, str) or
                not answer or
                finished is not True
            ):
                raise RuntimeError("invalid RAG finished event")

        else:
            if (
                data.get("ok") is not False or
                not isinstance(error, str) or
                not error or
                finished is not True
            ):
                raise RuntimeError("invalid RAG error event")

        return RagStreamEvent(
            type=event_type,
            request_id=request_id,
            sequence=sequence,
            delta=delta,
            answer=answer,
            backend=backend,
            llm_backend=llm_backend,
            error=error,
            elapsed_ms=float(elapsed_ms),
            finished=finished,
        )
