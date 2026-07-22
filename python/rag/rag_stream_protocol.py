import json
from dataclasses import dataclass
from typing import Any, Dict

PROTOCOL_VERSION = 1

@dataclass
class RagStreamRequest:
    request_id: str
    query: str

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

def decode_rag_stream_request(
    message: str,
) -> RagStreamRequest:
    try:
        data: Dict[str, Any] = json.loads(message)
    except json.JSONDecodeError as exc:
        raise ValueError(
            f"invalid RAG request JSON: {exc}"
        ) from exc

    if not isinstance(data, dict):
        raise ValueError(
            "RAG stream request must be an object"
        )

    if data.get("type") != "rag_query":
        raise ValueError(
            "unsupported RAG request type"
        )

    if data.get("stream") is not True:
        raise ValueError(
            "RAG stream request must set stream=true"
        )

    if data.get("version") != PROTOCOL_VERSION:
        raise ValueError("unsupported RAG protocol version")

    request_id = data.get("request_id")
    query = data.get("query")

    if (
        not isinstance(request_id, str) or
        not request_id.strip()
    ):
        raise ValueError(
            "missing RAG request_id"
        )

    if (
        not isinstance(query, str) or
        not query.strip()
    ):
        raise ValueError(
            "missing RAG query"
        )

    return RagStreamRequest(
        request_id=request_id,
        query=query.strip(),
    )

def encode_rag_stream_event(
    event: RagStreamEvent,
) -> str:
    if not event.request_id:
        raise ValueError(
            "RAG event request_id must not be empty"
        )

    if event.sequence < 0:
        raise ValueError(
            "RAG event sequence must not be negative"
        )

    if (
        event.type == "rag_chunk" and
        (
            not event.delta or
            event.finished
        )
    ):
        raise ValueError(
            "invalid RAG chunk event"
        )

    if (
        event.type == "rag_finished" and
        (
            not event.answer or
            not event.finished
        )
    ):
        raise ValueError(
            "invalid RAG finished event"
        )

    if (
        event.type == "rag_error" and
        (
            not event.error or
            not event.finished
        )
    ):
        raise ValueError(
            "invalid RAG error event"
        )

    if event.type not in {
        "rag_chunk",
        "rag_finished",
        "rag_error",
    }:
        raise ValueError(
            f"unsupported RAG event: {event.type}"
        )

    return json.dumps(
        {
            "version": PROTOCOL_VERSION,
            "type": event.type,
            "ok": event.type != "rag_error",
            "request_id": event.request_id,
            "sequence": event.sequence,
            "delta": event.delta,
            "answer": event.answer,
            "backend": event.backend,
            "llm_backend": event.llm_backend,
            "error": event.error,
            "elapsed_ms": event.elapsed_ms,
            "finished": event.finished,
        },
        ensure_ascii=False,
    )
    