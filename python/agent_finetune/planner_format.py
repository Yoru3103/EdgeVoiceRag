from __future__ import annotations

import json
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
        "description": "打开或关闭空调，并同步改变指示灯状态",
        "name": "set_air_conditioner",
        "parameters": {
            "additionalProperties": False,
            "properties": {
                "enabled": {
                    "description": "true 表示开启，false 表示关闭",
                    "type": "boolean",
                }
            },
            "required": ["enabled"],
            "type": "object",
        },
        "requires_confirmation": True,
    },
    {
        "description": "查询空调和指示灯的当前状态",
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

    if call["name"] == "set_air_conditioner":
        if set(call["arguments"]) != {"enabled"}:
            return ValidationResult(False, error="set_air_conditioner requires enabled only")
        if not isinstance(call["arguments"]["enabled"], bool):
            return ValidationResult(False, error="enabled must be boolean")
    elif call["arguments"]:
        return ValidationResult(False, error="read-only tools require empty arguments")

    return ValidationResult(True, value=value)


def parse_strict_action(text: str) -> ValidationResult:
    stripped = text.strip()
    try:
        value = json.loads(stripped)
    except json.JSONDecodeError as error:
        return ValidationResult(False, error=f"invalid JSON: {error.msg}")
    return validate_action(value)


def build_runtime_prompt(user_input: str, observations: list[dict[str, Any]]) -> str:
    tool_example = tool_call("call-1", "get_cabin_environment", {})
    answer_example = final_answer("任务已经完成。")

    parts = [
        SYSTEM_PROMPT,
        "\n\n可用工具：\n" + json.dumps(TOOLS, ensure_ascii=False, indent=2),
        "\n\n用户原始任务：\n" + user_input,
        "\n\n已经执行的步骤和观察结果：\n"
        + json.dumps(observations, ensure_ascii=False, indent=2),
        "\n\n调用工具时输出：\n" + canonical_json(tool_example),
        "\n\n任务完成时输出：\n" + canonical_json(answer_example),
        (
            "\n\n规则："
            "\n1. 如果没有足够信息，选择一个工具。"
            "\n2. 如果Observation已经足够，输出final_answer。"
            "\n3. 可以根据Observation继续调用其他工具。"
            "\n4. 不得重复调用已经获得有效结果的只读工具。"
            "\n5. 工具执行失败时应根据错误生成简短回答，不要假装成功。"
            "\n6. 不得声称未执行的控制操作已经完成。"
            "\n7. 只能输出一个JSON对象。"
            "\n8. 不输出思考过程。"
        ),
        "\n\n下一步JSON输出：",
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

