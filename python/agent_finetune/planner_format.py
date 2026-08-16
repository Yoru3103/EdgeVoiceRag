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
        "description": (
            "仅用于读取当前车内实时温度和湿度。"
            "当用户询问温度、湿度、多少度、热不热时调用。"
            "不能用于查询空调是否开启，也不能用于打开或关闭空调。"
        ),
        "name": "get_cabin_environment",
        "parameters": {
            "additionalProperties": False,
            "properties": {},
            "type": "object",
        },
        "requires_confirmation": False,
    },
    {
        "description": (
            "仅用于查询空调当前是否开启以及指示灯状态。"
            "当用户询问空调开了吗、关了吗、当前状态时调用。"
            "不能用于读取温湿度，也不能改变空调状态。"
        ),
        "name": "get_air_conditioner_state",
        "parameters": {
            "additionalProperties": False,
            "properties": {},
            "type": "object",
        },
        "requires_confirmation": False,
    },
    {
        "description": (
            "仅用于执行打开或关闭空调的控制操作。"
            "用户要求打开、开启空调时，enabled必须为true；"
            "用户要求关闭、关掉空调时，enabled必须为false。"
            "不能用于查询温湿度或空调状态。"
        ),
        "name": "set_air_conditioner",
        "parameters": {
            "additionalProperties": False,
            "properties": {
                "enabled": {
                    "description": (
                        "true表示打开空调，"
                        "false表示关闭空调"
                    ),
                    "type": "boolean",
                }
            },
            "required": ["enabled"],
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
    ]

    selection_rules = [
        "询问温度或湿度：调用get_cabin_environment。",
        "询问空调是否开启或当前状态：调用get_air_conditioner_state。",
        "要求打开空调：调用set_air_conditioner，enabled为true。",
        "要求关闭空调：调用set_air_conditioner，enabled为false。",
        "不得因为示例中出现某个工具，就忽略用户当前任务。",
        "已经获得成功的只读工具结果后，不得重复调用该工具。",
        "工具执行失败后输出final_answer说明错误，不得反复调用。",
        "没有执行控制工具时，不得声称空调已经打开或关闭。",
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
            indent=2,
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
            indent=2,
        ),

        "\n\n输出规则：\n",
        "\n".join(
            f"{index}. {rule}"
            for index, rule in enumerate(
                output_rules,
                start=1,
            )
        ),

        # 把真正任务放在提示词末尾，避免被前面的示例覆盖。
        "\n\n用户原始任务：\n",
        user_input,

        "\n\n已经执行的步骤和观察结果：\n",
        json.dumps(
            observations,
            ensure_ascii=False,
            indent=2,
        ),

        "\n\n请根据用户原始任务和Observation选择下一步。",
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

