from __future__ import annotations

import json
import math
from dataclasses import dataclass
from typing import Any


SYSTEM_PROMPT = (
    "你是一个车载设备Agent的任务规划器。"
    "你的职责只是决定调用哪个工具，或者直接回答用户。"
    "你不能声称已经执行尚未调用的工具。"
    "你不能生成Shell命令。"
    "你必须只输出一个JSON对象，不能输出Markdown、解释或其他文字。"
)

TOOLS = [
    {
        "description": (
            "读取当前车内温度，并由C++确定性判断温度条件。"
            "仅用于用户提出“温度满足某个条件时执行操作”的任务。"
            "operator只能是gt、ge、lt、le，分别表示大于、"
            "大于等于、小于、小于等于。"
            "普通温湿度查询应使用get_cabin_environment。"
        ),
        "name": "check_cabin_temperature_condition",
        "parameters": {
            "additionalProperties": False,
            "properties": {
                "operator": {
                    "description": (
                        "gt大于，ge大于等于，"
                        "lt小于，le小于等于"
                    ),
                    "enum": ["gt", "ge", "lt", "le"],
                    "type": "string",
                },
                "threshold_c": {
                    "description": "摄氏温度阈值",
                    "maximum": 100.0,
                    "minimum": -50.0,
                    "type": "number",
                },
            },
            "required": [
                "operator",
                "threshold_c",
            ],
            "type": "object",
        },
        "requires_confirmation": False,
    },
    {
        "description": "查询模拟空调和指示灯的当前状态",
        "name": "get_air_conditioner_state",
        "parameters": {
            "additionalProperties": False,
            "properties": {},
            "type": "object",
        },
        "requires_confirmation": False,
    },
    {
        "description": "读取当前车内温度和湿度",
        "name": "get_cabin_environment",
        "parameters": {
            "additionalProperties": False,
            "properties": {},
            "type": "object",
        },
        "requires_confirmation": False,
    },
    {
        "description": "打开或关闭模拟空调，并同步改变指示灯状态",
        "name": "set_air_conditioner",
        "parameters": {
            "additionalProperties": False,
            "properties": {
                "enabled": {
                    "description": (
                        "true 表示开启，false 表示关闭"
                    ),
                    "type": "boolean",
                }
            },
            "required": ["enabled"],
            "type": "object",
        },
        "requires_confirmation": True,
    },
    {
        "description": (
            "原子执行温度条件空调控制。"
            "C++读取当前车内温度并确定性比较，"
            "条件成立时设置空调状态并进行写后验证。"
            "operator只能是gt、ge、lt、le；"
            "enabled为条件成立时希望设置的空调状态。"
            "该工具可能改变设备状态，因此需要用户确认。"
        ),
        "name": "set_air_conditioner_if_temperature",
        "parameters": {
            "additionalProperties": False,
            "properties": {
                "enabled": {
                    "description": "条件成立时设置的空调状态",
                    "type": "boolean",
                },
                "operator": {
                    "description": (
                        "gt大于，ge大于等于，"
                        "lt小于，le小于等于"
                    ),
                    "enum": ["gt", "ge", "lt", "le"],
                    "type": "string",
                },
                "threshold_c": {
                    "description": "摄氏温度阈值",
                    "maximum": 100.0,
                    "minimum": -50.0,
                    "type": "number",
                },
            },
            "required": [
                "operator",
                "threshold_c",
                "enabled",
            ],
            "type": "object",
        },
        "requires_confirmation": True,
    },
]

TOOL_NAMES = {tool["name"] for tool in TOOLS}


@dataclass(frozen=True)
class ValidationResult:
    ok: bool
    value: dict[str, Any] | None = None
    error: str = ""


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)


def tool_call(call_id: str, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    return {
        "type": "tool_call",
        "tool_call": {
            "id": call_id,
            "name": name,
            "arguments": arguments,
        },
    }


def final_answer(answer: str) -> dict[str, Any]:
    return {"type": "final_answer", "answer": answer}


def validate_action(value: Any) -> ValidationResult:
    if not isinstance(value, dict):
        return ValidationResult(False, error="root must be a JSON object")

    action_type = value.get("type")
    if action_type == "final_answer":
        if set(value) != {"type", "answer"}:
            return ValidationResult(False, error="final_answer has unexpected fields")
        if not isinstance(value.get("answer"), str) or not value["answer"].strip():
            return ValidationResult(False, error="final_answer.answer must be non-empty")
        return ValidationResult(True, value=value)

    if action_type != "tool_call":
        return ValidationResult(False, error="type must be tool_call or final_answer")
    if set(value) != {"type", "tool_call"}:
        return ValidationResult(False, error="tool_call action has unexpected fields")

    call = value.get("tool_call")
    if not isinstance(call, dict):
        return ValidationResult(False, error="tool_call must be an object")
    if set(call) != {"id", "name", "arguments"}:
        return ValidationResult(False, error="tool_call has unexpected fields")
    if not isinstance(call.get("id"), str) or not call["id"]:
        return ValidationResult(False, error="tool_call.id must be non-empty")
    if call.get("name") not in TOOL_NAMES:
        return ValidationResult(False, error="tool_call.name is not registered")
    if not isinstance(call.get("arguments"), dict):
        return ValidationResult(False, error="tool_call.arguments must be an object")

    tool_name = call["name"]
    arguments = call["arguments"]

    if tool_name == "set_air_conditioner":
        if set(arguments) != {"enabled"}:
            return ValidationResult(
                False,
                error="set_air_conditioner requires enabled only",
            )

        if not isinstance(arguments["enabled"], bool):
            return ValidationResult(False, error="enabled must be boolean")

    elif tool_name in {
        "check_cabin_temperature_condition",
        "set_air_conditioner_if_temperature",
    }:
        expected_arguments = {
            "operator",
            "threshold_c",
        }

        if tool_name == "set_air_conditioner_if_temperature":
            expected_arguments.add("enabled")

        if set(arguments) != expected_arguments:
            return ValidationResult(
                False,
                error=(
                    "temperature condition tool has "
                    "invalid arguments"
                ),
            )

        comparison_operator = arguments["operator"]

        if comparison_operator not in {
            "gt",
            "ge",
            "lt",
            "le",
        }:
            return ValidationResult(
                False,
                error=(
                    "operator must be one of "
                    "gt, ge, lt, le"
                ),
            )

        threshold_c = arguments["threshold_c"]

        if (
            isinstance(threshold_c, bool)
            or not isinstance(threshold_c, (int, float))
        ):
            return ValidationResult(
                False,
                error="threshold_c must be a number",
            )

        threshold_c = float(threshold_c)

        if not math.isfinite(threshold_c):
            return ValidationResult(
                False,
                error="threshold_c must be finite",
            )

        if not -50.0 <= threshold_c <= 100.0:
            return ValidationResult(
                False,
                error=(
                    "threshold_c is outside "
                    "the allowed range"
                ),
            )

        if (
            tool_name
            == "set_air_conditioner_if_temperature"
            and not isinstance(arguments["enabled"], bool)
        ):
            return ValidationResult(
                False,
                error="enabled must be boolean",
            )

    elif arguments:
        return ValidationResult(
            False,
            error="read-only query tools require empty arguments",
        )

    return ValidationResult(True, value=value)


def parse_strict_action(text: str) -> ValidationResult:
    stripped = text.strip()
    try:
        value = json.loads(stripped)
    except json.JSONDecodeError as error:
        return ValidationResult(False, error=f"invalid JSON: {error.msg}")
    return validate_action(value)


def build_runtime_prompt(user_input: str, observations: list[dict[str, Any]]) -> str:
    examples = [
        {
            "用户任务": "车内的温度是多少？",
            "输出": tool_call(
                "call-1",
                "get_cabin_environment",
                {},
            ),
        },
        {
            "用户任务": "空调现在开着吗？",
            "输出": tool_call(
                "call-1",
                "get_air_conditioner_state",
                {},
            ),
        },
        {
            "用户任务": "请打开空调。",
            "输出": tool_call(
                "call-1",
                "set_air_conditioner",
                {"enabled": True},
            ),
        },
        {
            "用户任务": "请关闭空调。",
            "输出": tool_call(
                "call-1",
                "set_air_conditioner",
                {"enabled": False},
            ),
        },
        {
            "用户任务": (
                "座舱超过二十七度时开启制冷。"
            ),
            "输出": tool_call(
                "call-1",
                "set_air_conditioner_if_temperature",
                {
                    "enabled": True,
                    "operator": "gt",
                    "threshold_c": 27.0,
                },
            ),
        },
    ]

    selection_rules = [
        (
            "仅询问当前温度或湿度时，"
            "调用get_cabin_environment。"
        ),
        (
            "询问空调当前状态时，"
            "调用get_air_conditioner_state。"
        ),
        (
            "用户直接要求打开空调时，"
            "调用set_air_conditioner，enabled为true。"
        ),
        (
            "用户直接要求关闭空调时，"
            "调用set_air_conditioner，enabled为false。"
        ),
        (
            "用户提出温度条件控制任务时，"
            "必须直接调用set_air_conditioner_if_temperature。"
            "operator和threshold_c表示条件，enabled表示条件成立时的目标状态。"
            "不得拆分为先查询温度再调用控制工具。"
        ),
        (
            "超过或高于映射为gt；"
            "至少、达到或不低于映射为ge；"
            "低于或小于映射为lt；"
            "至多或不高于映射为le。"
        ),
        (
            "set_air_conditioner_if_temperature成功后，"
            "根据Observation中的matched、control_executed和enabled输出final_answer，"
            "不得再次调用set_air_conditioner。"
        ),
        (
            "用户只要求判断温度条件但不要求控制设备时，"
            "才使用check_cabin_temperature_condition。"
        ),
        (
            "工具执行失败后输出final_answer说明错误，"
            "不得反复调用工具。"
        ),
        (
            "已经获得成功的只读工具结果后，"
            "不得重复调用相同工具。"
        ),
        (
            "没有成功执行控制工具时，"
            "不得声称空调已经打开或关闭。"
        ),
    ]

    output_rules = [
        "只能输出一个JSON对象。",
        "不能输出Markdown代码块。",
        "不能输出分析、解释、思考过程或额外文字。",
        "tool_call必须包含type和tool_call。",
        "final_answer必须包含type和answer。",
    ]

    parts = [
        SYSTEM_PROMPT,

        "\n\n可用工具：\n",
        json.dumps(
            TOOLS,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ),

        "\n\n工具选择规则：\n",
        "\n".join(
            f"{index}. {rule}"
            for index, rule in enumerate(
                selection_rules,
                start=1,
            )
        ),

        "\n\n平衡示例：\n",
        json.dumps(
            examples,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ),

        "\n\n输出规则：\n",
        "\n".join(
            f"{index}. {rule}"
            for index, rule in enumerate(
                output_rules,
                start=1,
            )
        ),

        "\n\n用户原始任务：\n",
        user_input,

        "\n\n已经执行的步骤和观察结果：\n",
        json.dumps(
            observations,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ),

        (
            "\n\n请根据用户原始任务和Observation"
            "选择下一步。"
        ),
        "\n下一步JSON输出：",
    ]

    return "".join(parts)

def make_record(
    record_id: str,
    user_input: str,
    observations: list[dict[str, Any]],
    expected: dict[str, Any],
) -> dict[str, Any]:
    validation = validate_action(expected)
    if not validation.ok:
        raise ValueError(f"invalid expected action for {record_id}: {validation.error}")
    return {
        "id": record_id,
        "messages": [
            {"role": "user", "content": build_runtime_prompt(user_input, observations)},
            {"role": "assistant", "content": canonical_json(expected)},
        ],
        "expected": expected,
    }


def observation(
    call_id: str,
    name: str,
    arguments: dict[str, Any],
    *,
    ok: bool,
    data: dict[str, Any] | None = None,
    error: str = "",
) -> dict[str, Any]:
    return {
        "tool_call": {"id": call_id, "name": name, "arguments": arguments},
        "observation": {"ok": ok, "data": data or {}, "error": error},
    }
