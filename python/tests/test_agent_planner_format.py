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
    assert "不能输出分析、解释、思考过程" in prompt
    assert "get_cabin_environment" in prompt


def test_final_answer_is_valid() -> None:
    result = parse_strict_action(
        json.dumps(final_answer("当前车内温度为25.0摄氏度。"), ensure_ascii=False)
    )
    assert result.ok

def test_temperature_condition_tool_is_valid() -> None:
    action = tool_call(
        "condition-check",
        "check_cabin_temperature_condition",
        {
            "operator": "gt",
            "threshold_c": 27.0,
        },
    )

    result = parse_strict_action(
        json.dumps(
            action,
            ensure_ascii=False,
        )
    )

    assert result.ok


def test_invalid_temperature_operator_is_rejected() -> None:
    action = tool_call(
        "condition-check",
        "check_cabin_temperature_condition",
        {
            "operator": "equal",
            "threshold_c": 27.0,
        },
    )

    result = parse_strict_action(
        json.dumps(
            action,
            ensure_ascii=False,
        )
    )

    assert not result.ok


def test_invalid_temperature_threshold_is_rejected() -> None:
    action = tool_call(
        "condition-check",
        "check_cabin_temperature_condition",
        {
            "operator": "gt",
            "threshold_c": "二十七",
        },
    )

    result = parse_strict_action(
        json.dumps(
            action,
            ensure_ascii=False,
        )
    )

    assert not result.ok


def test_atomic_temperature_condition_tool_is_valid() -> None:
    action = tool_call(
        "condition-control",
        "set_air_conditioner_if_temperature",
        {
            "enabled": True,
            "operator": "gt",
            "threshold_c": 27.0,
        },
    )

    result = parse_strict_action(
        json.dumps(action, ensure_ascii=False)
    )

    assert result.ok


def test_atomic_temperature_condition_requires_boolean_enabled() -> None:
    action = tool_call(
        "condition-control",
        "set_air_conditioner_if_temperature",
        {
            "enabled": "true",
            "operator": "gt",
            "threshold_c": 27.0,
        },
    )

    result = parse_strict_action(
        json.dumps(action, ensure_ascii=False)
    )

    assert not result.ok


def test_runtime_prompt_uses_atomic_condition_control() -> None:
    prompt = build_runtime_prompt(
        "座舱超过二十七度时开启制冷。",
        [],
    )

    assert (
        "set_air_conditioner_if_temperature"
        in prompt
    )

    assert (
        "不得拆分为先查询温度再调用控制工具"
        in prompt
    )

    assert (
        '"operator": "gt"'
        in prompt
    )

    assert (
        '"threshold_c": 27.0'
        in prompt
    )

    assert '"enabled": true' in prompt
