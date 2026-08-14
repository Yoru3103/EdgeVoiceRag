from __future__ import annotations

import argparse
import json
import random
from pathlib import Path
from typing import Any

from agent_finetune.planner_format import (
    final_answer,
    make_record,
    observation,
    tool_call,
)


ENVIRONMENT_QUERIES = [
    "车内温度是多少？",
    "车内的温度是多少？",
    "车里温度多少？",
    "车里的温度是多少？",
    "现在车内多少度？",
    "车里热不热？",
    "帮我读取车内温湿度。",
    "当前湿度是多少？",
    "车内的湿度有多高？",
    "现在车厢里的温度和湿度分别是多少？",
]

STATE_QUERIES = [
    "空调状态是什么？",
    "空调开了吗？",
    "空调现在开着吗？",
    "空调是否开启？",
    "帮我确认一下空调有没有开。",
]

OPEN_QUERIES = [
    "打开空调。",
    "请开启空调。",
    "把空调打开。",
    "帮我开一下空调。",
]

CLOSE_QUERIES = [
    "关闭空调。",
    "请关掉空调。",
    "把空调关掉。",
    "帮我关闭空调。",
]


def environment_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    readings = [(18.0, 35.0), (23.5, 48.0), (26.0, 55.5), (31.2, 72.0)]
    for query_index, query in enumerate(ENVIRONMENT_QUERIES):
        call_id = f"env-{query_index + 1}"
        records.append(make_record(
            f"environment-call-{query_index}", query, [],
            tool_call(call_id, "get_cabin_environment", {}),
        ))
        for reading_index, (temperature, humidity) in enumerate(readings):
            history = [observation(
                call_id, "get_cabin_environment", {}, ok=True,
                data={"temperature_c": temperature, "humidity_percent": humidity},
            )]
            records.append(make_record(
                f"environment-answer-{query_index}-{reading_index}", query, history,
                final_answer(
                    f"当前车内温度为{temperature:.1f}摄氏度，湿度为{humidity:.1f}%。"
                ),
            ))

        failure_history = [observation(
            call_id, "get_cabin_environment", {}, ok=False,
            error="IIO设备读取失败",
        )]
        records.append(make_record(
            f"environment-error-{query_index}", query, failure_history,
            final_answer("暂时无法读取车内温湿度：IIO设备读取失败。"),
        ))
    return records


def state_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for index, query in enumerate(STATE_QUERIES):
        call_id = f"state-{index + 1}"
        records.append(make_record(
            f"state-call-{index}", query, [],
            tool_call(call_id, "get_air_conditioner_state", {}),
        ))
        for enabled in (False, True):
            history = [observation(
                call_id, "get_air_conditioner_state", {}, ok=True,
                data={"enabled": enabled, "indicator_on": enabled},
            )]
            answer = "空调当前处于开启状态。" if enabled else "空调当前处于关闭状态。"
            records.append(make_record(
                f"state-answer-{index}-{int(enabled)}", query, history,
                final_answer(answer),
            ))
    return records


def control_records(queries: list[str], enabled: bool, prefix: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for index, query in enumerate(queries):
        call_id = f"{prefix}-{index + 1}"
        arguments = {"enabled": enabled}
        records.append(make_record(
            f"{prefix}-call-{index}", query, [],
            tool_call(call_id, "set_air_conditioner", arguments),
        ))
        history = [observation(
            call_id, "set_air_conditioner", arguments, ok=True,
            data={"enabled": enabled, "indicator_on": enabled},
        )]
        answer = "空调已经开启。" if enabled else "空调已经关闭。"
        records.append(make_record(
            f"{prefix}-answer-{index}", query, history, final_answer(answer)
        ))
    return records


def conditional_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    templates = [
        ("如果车内温度超过{threshold}度，就打开空调。", "high", True),
        ("如果车内温度低于{threshold}度，就关闭空调。", "low", False),
    ]
    cases = [(24.0, 26.0), (28.5, 26.0), (18.5, 20.0), (23.0, 20.0)]

    for template_index, (template, comparison, target_enabled) in enumerate(templates):
        for case_index, (temperature, threshold) in enumerate(cases):
            query = template.format(threshold=threshold)
            env_id = f"condition-env-{template_index}-{case_index}"
            control_id = f"condition-control-{template_index}-{case_index}"
            records.append(make_record(
                f"condition-call-env-{template_index}-{case_index}", query, [],
                tool_call(env_id, "get_cabin_environment", {}),
            ))

            env_history = [observation(
                env_id, "get_cabin_environment", {}, ok=True,
                data={"temperature_c": temperature, "humidity_percent": 50.0},
            )]
            condition_met = temperature > threshold if comparison == "high" else temperature < threshold
            if not condition_met:
                records.append(make_record(
                    f"condition-noop-{template_index}-{case_index}", query, env_history,
                    final_answer(
                        f"当前车内温度为{temperature:.1f}摄氏度，未满足设定条件，空调状态不变。"
                    ),
                ))
                continue

            arguments = {"enabled": target_enabled}
            records.append(make_record(
                f"condition-call-control-{template_index}-{case_index}", query, env_history,
                tool_call(control_id, "set_air_conditioner", arguments),
            ))
            complete_history = env_history + [observation(
                control_id, "set_air_conditioner", arguments, ok=True,
                data={"enabled": target_enabled, "indicator_on": target_enabled},
            )]
            answer = "空调已经开启。" if target_enabled else "空调已经关闭。"
            records.append(make_record(
                f"condition-answer-{template_index}-{case_index}", query, complete_history,
                final_answer(f"当前车内温度为{temperature:.1f}摄氏度，{answer}"),
            ))
    return records


def build_records() -> list[dict[str, Any]]:
    return (
        environment_records()
        + state_records()
        + control_records(OPEN_QUERIES, True, "open")
        + control_records(CLOSE_QUERIES, False, "close")
        + conditional_records()
    )


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(json.dumps(record, ensure_ascii=False) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate vehicle Agent SFT data")
    parser.add_argument("--output-dir", type=Path, default=Path("data/agent_planner"))
    parser.add_argument("--validation-ratio", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=3588)
    args = parser.parse_args()

    if not 0.0 < args.validation_ratio < 1.0:
        raise SystemExit("--validation-ratio must be between 0 and 1")

    records = build_records()
    random.Random(args.seed).shuffle(records)
    validation_count = max(1, round(len(records) * args.validation_ratio))
    validation = records[:validation_count]
    train = records[validation_count:]

    write_jsonl(args.output_dir / "train.jsonl", train)
    write_jsonl(args.output_dir / "validation.jsonl", validation)
    print(f"generated train={len(train)} validation={len(validation)} total={len(records)}")


if __name__ == "__main__":
    main()

