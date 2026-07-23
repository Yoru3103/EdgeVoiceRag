import json

import pytest

from rag.rag_stream_client import RagStreamClient

def test_decode_chunk() -> None:
    event = RagStreamClient.decode_event(
        raw_response=json.dumps(
            {
                "version": 1,
                "type": "rag_chunk",
                "ok": True,
                "request_id": "rag-1",
                "sequence": 0,
                "delta": "空调温度",
                "answer": "",
                "backend": "python_tfidf",
                "llm_backend": "cpp_mock",
                "error": "",
                "elapsed_ms": 2.5,
                "finished": False,
            },
            ensure_ascii=False,
        ),
        expected_request_id="rag-1",
    )

    assert event.type == "rag_chunk"
    assert event.delta == "空调温度"
    assert event.sequence == 0
    assert not event.finished

def test_decode_finished() -> None:
    event = RagStreamClient.decode_event(
        raw_response=json.dumps(
            {
                "version": 1,
                "type": "rag_finished",
                "ok": True,
                "request_id": "rag-1",
                "sequence": 2,
                "delta": "",
                "answer": "空调温度可以调节。",
                "backend": "python_tfidf",
                "llm_backend": "cpp_mock",
                "error": "",
                "elapsed_ms": 20.0,
                "finished": True,
            },
            ensure_ascii=False,
        ),
        expected_request_id="rag-1",
    )

    assert event.finished
    assert event.answer == "空调温度可以调节。"

def test_reject_request_id_mismatch() -> None:
    with pytest.raises(
        RuntimeError,
        match="request_id",
    ):
        RagStreamClient.decode_event(
            raw_response=json.dumps(
                {
                    "version": 1,
                    "type": "rag_chunk",
                    "ok": True,
                    "request_id": "another",
                    "sequence": 0,
                    "delta": "测试",
                    "answer": "",
                    "backend": "python_tfidf",
                    "llm_backend": "cpp_mock",
                    "error": "",
                    "elapsed_ms": 1.0,
                    "finished": False,
                }
            ),
            expected_request_id="rag-1",
        )

def test_reject_error_event() -> None:
    event = RagStreamClient.decode_event(
        raw_response=json.dumps(
            {
                "version": 1,
                "type": "rag_error",
                "ok": False,
                "request_id": "rag-1",
                "sequence": 1,
                "delta": "",
                "answer": "",
                "backend": "python_tfidf",
                "llm_backend": "rkllm",
                "error": "generation failed",
                "elapsed_ms": 5.0,
                "finished": True,
            }
        ),
        expected_request_id="rag-1",
    )

    assert event.type == "rag_error"
    assert event.error == "generation failed"
