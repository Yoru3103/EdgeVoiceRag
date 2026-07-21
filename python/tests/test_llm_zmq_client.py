import json

import pytest

from rag.llm_zmq_client import LlmZmqClient

def test_reject_empty_endpoint() -> None:
    with pytest.raises(
        ValueError,
        match="endpoint must not be empty",
    ):
        LlmZmqClient(endpoint="")
        
def test_reject_invalid_timeout() -> None:
    with pytest.raises(
        ValueError,
        match="timeout must be greater than zero",
    ):
        LlmZmqClient(
            endpoint="tcp://127.0.0.1:8899",
            timeout_seconds=0,
        )
        
def test_decode_success_response() -> None:
    raw_response = json.dumps(
        {
            "version": 1,
            "type": "generation_result",
            "ok": True,
            "request_id": "request-1",
            "answer": "请将空调温度设置为二十四度。",
            "backend": "cpp_mock",
            "error": "",
            "elapsed_ms": 12.5,
            "finished": True,
        },
        ensure_ascii=False,
    )

    response = LlmZmqClient._decode_response(
        raw_response=raw_response,
        expected_request_id="request-1",
    )

    assert response["answer"] == "请将空调温度设置为二十四度。"
    assert response["backend"] == "cpp_mock"


def test_reject_mismatched_request_id() -> None:
    raw_response = json.dumps(
        {
            "version": 1,
            "type": "generation_result",
            "ok": True,
            "request_id": "another-request",
            "answer": "answer",
            "backend": "cpp_mock",
            "error": "",
            "elapsed_ms": 1.0,
            "finished": True,
        }
    )

    with pytest.raises(
        RuntimeError,
        match="request_id does not match",
    ):
        LlmZmqClient._decode_response(
            raw_response=raw_response,
            expected_request_id="request-1",
        )


def test_reject_server_error() -> None:
    raw_response = json.dumps(
        {
            "version": 1,
            "type": "generation_result",
            "ok": False,
            "request_id": "request-1",
            "answer": "",
            "backend": "rkllm",
            "error": "RKLLM generation failed",
            "elapsed_ms": 10.0,
            "finished": True,
        }
    )

    with pytest.raises(
        RuntimeError,
        match="RKLLM generation failed",
    ):
        LlmZmqClient._decode_response(
            raw_response=raw_response,
            expected_request_id="request-1",
        )


def test_reject_empty_success_answer() -> None:
    raw_response = json.dumps(
        {
            "version": 1,
            "type": "generation_result",
            "ok": True,
            "request_id": "request-1",
            "answer": "",
            "backend": "cpp_mock",
            "error": "",
            "elapsed_ms": 1.0,
            "finished": True,
        }
    )

    with pytest.raises(
        RuntimeError,
        match="empty answer",
    ):
        LlmZmqClient._decode_response(
            raw_response=raw_response,
            expected_request_id="request-1",
        )
        
def test_generate_rejects_empty_prompt() -> None:
    client = LlmZmqClient(
        endpoint="tcp://127.0.0.1:8899",
        timeout_seconds=1,
    )
    
    with pytest.raises(
        ValueError,
        match="prompt must not be empty",
    ):
        client.generate("   ")