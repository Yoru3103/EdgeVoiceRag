import json

import pytest

from rag.rag_stream_protocol import (
    RagStreamEvent,
    decode_rag_stream_request,
    encode_rag_stream_event,
)


def test_decode_stream_request() -> None:
    request = decode_rag_stream_request(
        json.dumps(
            {
                "version": 1,
                "type": "rag_query",
                "request_id": "rag-1",
                "query": "空调怎么调节",
                "stream": True,
            },
            ensure_ascii=False,
        )
    )

    assert request.request_id == "rag-1"
    assert request.query == "空调怎么调节"


def test_reject_invalid_version() -> None:
    with pytest.raises(
        ValueError,
        match="protocol version",
    ):
        decode_rag_stream_request(
            json.dumps(
                {
                    "version": 99,
                    "type": "rag_query",
                    "request_id": "rag-1",
                    "query": "测试",
                    "stream": True,
                }
            )
        )


def test_encode_chunk_event() -> None:
    message = encode_rag_stream_event(
        RagStreamEvent(
            type="rag_chunk",
            request_id="rag-1",
            sequence=0,
            delta="空调",
            answer="",
            backend="python_tfidf",
            llm_backend="cpp_mock",
            error="",
            elapsed_ms=2.5,
            finished=False,
        )
    )

    data = json.loads(message)

    assert data["type"] == "rag_chunk"
    assert data["delta"] == "空调"
    assert data["sequence"] == 0
    assert data["finished"] is False


def test_encode_finished_event() -> None:
    message = encode_rag_stream_event(
        RagStreamEvent(
            type="rag_finished",
            request_id="rag-1",
            sequence=1,
            delta="",
            answer="空调温度已调节",
            backend="python_tfidf",
            llm_backend="cpp_mock",
            error="",
            elapsed_ms=10.0,
            finished=True,
        )
    )

    data = json.loads(message)

    assert data["type"] == "rag_finished"
    assert data["answer"] == "空调温度已调节"
    assert data["finished"] is True
