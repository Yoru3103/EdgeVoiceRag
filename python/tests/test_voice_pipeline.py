import json

from rag.voice_pipeline import ZmqRagClient


def test_parse_response_prefers_generated_answer() -> None:
    raw = json.dumps(
        {"ok": True, "answer": "检索答案", "generated_answer": "生成答案"},
        ensure_ascii=False,
    )

    result = ZmqRagClient._parse_response(raw)

    assert result.ok is True
    assert result.answer == "生成答案"


def test_parse_response_propagates_server_failure() -> None:
    raw = json.dumps(
        {"ok": False, "error": "模型暂不可用"},
        ensure_ascii=False,
    )

    result = ZmqRagClient._parse_response(raw)

    assert result.ok is False
    assert result.answer == ""
    assert result.error == "模型暂不可用"
    assert result.raw_response == raw


def test_parse_response_rejects_empty_answer() -> None:
    result = ZmqRagClient._parse_response('{"ok": true, "answer": ""}')

    assert result.ok is False
    assert "non-empty answer" in result.error


def test_parse_response_keeps_plain_text_compatibility() -> None:
    result = ZmqRagClient._parse_response("普通文本答案")

    assert result.ok is True
    assert result.answer == "普通文本答案"
