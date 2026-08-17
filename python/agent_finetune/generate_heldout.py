from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from agent_finetune.planner_format import (
    final_answer,
    make_record,
    observation,
    tool_call,
)


def build_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    def add(
        record_id: str,
        query: str,
        observations: list[dict[str, Any]],
        expected: dict[str, Any],
        required_answer_terms: list[str] | None = None,
    ) -> None:
        record = make_record(
            record_id,
            query,
            observations,
            expected,
        )

        if required_answer_terms:
            record["required_answer_terms"] = required_answer_terms

        records.append(record)

    environment_queries = [
        "现在座舱里面有多热？",
        "告诉我车厢当前温湿度。",
        "车里面现在几度？",
        "帮我看看座舱湿度。",
        "现在车里冷不冷？",
        "读一下当前环境传感器。",
        "驾驶室温度现在是多少？",
        "座舱里面潮不潮？",
    ]

    for index, query in enumerate(environment_queries):
        add(
            f"heldout-environment-{index}",
            query,
            [],
            tool_call(
                f"heldout-env-{index}",
                "get_cabin_environment",
                {},
            ),
        )

    state_queries = [
        "帮我看看制冷开没开。",
        "现在空调是不是还运行着？",
        "确认一下空调当前状态。",
        "制冷现在关着吗？",
        "空调指示灯亮了吗？",
        "查一下空调有没有开启。",
    ]

    for index, query in enumerate(state_queries):
        add(
            f"heldout-state-{index}",
            query,
            [],
            tool_call(
                f"heldout-state-{index}",
                "get_air_conditioner_state",
                {},
            ),
        )

    open_queries = [
        "有点热，帮我开一下冷气。",
        "给车里降降温，把空调开起来。",
        "启动一下制冷。",
        "请把车载空调开启。",
        "太闷了，开一下空调吧。",
        "我要开启制冷功能。",
    ]

    for index, query in enumerate(open_queries):
        add(
            f"heldout-open-{index}",
            query,
            [],
            tool_call(
                f"heldout-open-{index}",
                "set_air_conditioner",
                {"enabled": True},
            ),
        )

    close_queries = [
        "已经不热了，把冷气关掉。",
        "停止制冷。",
        "请关闭车载空调。",
        "空调不用继续运行了。",
        "帮我把冷气停下来。",
        "现在可以关空调了。",
    ]

    for index, query in enumerate(close_queries):
        add(
            f"heldout-close-{index}",
            query,
            [],
            tool_call(
                f"heldout-close-{index}",
                "set_air_conditioner",
                {"enabled": False},
            ),
        )

    # 已经成功读取温湿度后，必须回答，不能重复查询。
    for index, (temperature, humidity) in enumerate(
        [
            (19.5, 38.0),
            (25.8, 52.5),
            (30.2, 70.0),
        ]
    ):
        query = "告诉我现在座舱里的环境情况。"
        history = [
            observation(
                "existing-env-call",
                "get_cabin_environment",
                {},
                ok=True,
                data={
                    "temperature_c": temperature,
                    "humidity_percent": humidity,
                },
            )
        ]

        add(
            f"heldout-environment-result-{index}",
            query,
            history,
            final_answer(
                f"当前车内温度为{temperature:.1f}摄氏度，"
                f"湿度为{humidity:.1f}%。"
            ),
            ["温度", "湿度"],
        )

    # 空调状态Observation。
    for enabled in (True, False):
        history = [
            observation(
                "existing-state-call",
                "get_air_conditioner_state",
                {},
                ok=True,
                data={
                    "enabled": enabled,
                    "indicator_on": enabled,
                },
            )
        ]

        add(
            f"heldout-state-result-{int(enabled)}",
            "告诉我空调现在是什么状态。",
            history,
            final_answer(
                "空调当前处于开启状态。"
                if enabled
                else "空调当前处于关闭状态。"
            ),
            ["开启" if enabled else "关闭"],
        )

    # 工具失败后应回答错误，不能无限重试。
    failure_cases = [
        (
            "environment",
            "读取一下驾驶室温度。",
            "get_cabin_environment",
            {},
            "IIO设备读取失败",
        ),
        (
            "state",
            "空调目前打开了吗？",
            "get_air_conditioner_state",
            {},
            "空调状态接口不可用",
        ),
        (
            "control",
            "请开启冷气。",
            "set_air_conditioner",
            {"enabled": True},
            "设备控制失败",
        ),
    ]

    for name, query, tool_name, arguments, error in failure_cases:
        history = [
            observation(
                f"failed-{name}-call",
                tool_name,
                arguments,
                ok=False,
                error=error,
            )
        ]

        add(
            f"heldout-failure-{name}",
            query,
            history,
            final_answer(f"暂时无法完成操作：{error}。"),
            ["无法"],
        )

    # 条件工作流：第一步必须读取温度。
    conditional_query = "座舱超过二十七度的话就开启制冷。"

    add(
        "heldout-condition-initial",
        conditional_query,
        [],
        tool_call(
            "condition-read",
            "get_cabin_environment",
            {},
        ),
    )

    # 条件满足，应进入控制步骤。
    hot_history = [
        observation(
            "condition-read",
            "get_cabin_environment",
            {},
            ok=True,
            data={
                "temperature_c": 29.5,
                "humidity_percent": 55.0,
            },
        )
    ]

    add(
        "heldout-condition-hot",
        conditional_query,
        hot_history,
        tool_call(
            "condition-control",
            "set_air_conditioner",
            {"enabled": True},
        ),
    )

    # 条件不满足，应直接回答，不得控制。
    cool_history = [
        observation(
            "condition-read",
            "get_cabin_environment",
            {},
            ok=True,
            data={
                "temperature_c": 24.0,
                "humidity_percent": 50.0,
            },
        )
    ]

    add(
        "heldout-condition-cool",
        conditional_query,
        cool_history,
        final_answer(
            "当前温度为24.0摄氏度，未超过27度，不需要开启空调。"
        ),
        ["24", "状态不变"],
    )

    # 控制执行完成后必须结束，不能再次调用工具。
    complete_history = hot_history + [
        observation(
            "condition-control",
            "set_air_conditioner",
            {"enabled": True},
            ok=True,
            data={
                "enabled": True,
                "indicator_on": True,
            },
        )
    ]

    add(
        "heldout-condition-completed",
        conditional_query,
        complete_history,
        final_answer(
            "当前温度超过27度，空调已经开启。"
        ),
        ["29.5", "已经开启"],
    )

    return records


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate held-out Agent planner evaluation data"
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/agent_planner/heldout.jsonl"),
    )

    args = parser.parse_args()

    records = build_records()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(
                json.dumps(record, ensure_ascii=False) + "\n"
            )

    print(f"generated heldout records={len(records)}")
    print(f"output={args.output}")


if __name__ == "__main__":
    main()
