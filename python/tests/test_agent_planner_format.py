import json

from agent_finetune.planner_format import (
    build_runtime_prompt,
    final_answer,
    parse_strict_action,
    tool_call,
)


def test_tool_call_is_valid() -> None:
    action = tool_call("call-1", "get_cabin_environment", {})
    result = parse_strict_action(json.dumps(action, ensure_ascii=False))
    assert result.ok


def test_extra_text_is_rejected() -> None:
    result = parse_strict_action(
        '好的，结果如下：{"type":"final_answer","answer":"完成"}'
    )
    assert not result.ok


def test_invalid_tool_arguments_are_rejected() -> None:
    action = tool_call("call-1", "get_cabin_environment", {"unused": True})
    result = parse_strict_action(json.dumps(action, ensure_ascii=False))
    assert not result.ok


def test_runtime_prompt_requires_json_only() -> None:
    prompt = build_runtime_prompt("车内温度是多少？", [])
    assert "只能输出一个JSON对象" in prompt
    assert "不输出思考过程" in prompt
    assert "get_cabin_environment" in prompt


def test_final_answer_is_valid() -> None:
    result = parse_strict_action(
        json.dumps(final_answer("当前车内温度为25.0摄氏度。"), ensure_ascii=False)
    )
    assert result.ok
