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


CONDITION_TOOL = "check_cabin_temperature_condition"

def add_record(
    records: list[dict[str, Any]],
    *,
    record_id: str,
    query: str,
    history: list[dict[str, Any]],
    expected: dict[str, Any],
    scenario: str,
    required_terms: list[str] | None = None,
) -> None:
    record = make_record(
        record_id,
        query,
        history,
        expected,
    )

    record["scenario"] = scenario

    if required_terms:
        record["required_answer_terms"] = required_terms

    records.append(record)

def condition_arguments(
    comparison_operator: str,
    threshold_c: float,
) -> dict[str, Any]:
    return {
        "operator": comparison_operator,
        "threshold_c": threshold_c,
    }

def build_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    initial_cases = [
        (
            "gt-chinese",
            "座舱超过二十七度时开启制冷。",
            "gt",
            27.0,
        ),
        (
            "gt-decimal",
            "车内温度高于28.5度就打开空调。",
            "gt",
            28.5,
        ),
        (
            "ge-at-least",
            "车内至少达到二十五度就开启冷气。",
            "ge",
            25.0,
        ),
        (
            "ge-not-lower",
            "温度不低于26度时启动制冷。",
            "ge",
            26.0,
        ),
        (
            "lt-chinese",
            "车内低于二十度时关闭空调。",
            "lt",
            20.0,
        ),
        (
            "lt-decimal",
            "温度小于18.5度就停止制冷。",
            "lt",
            18.5,
        ),
        (
            "le-at-most",
            "温度至多二十二度时关闭车载空调。",
            "le",
            22.0,
        ),
        (
            "le-not-higher",
            "温度不高于23.5度就关闭冷气。",
            "le",
            23.5,
        ),
    ]

    for (case_id, query, comparison_operator, threshold_c) in initial_cases:
        call_id = f"{case_id}-check"

        add_record(
            records,
            record_id=f"compat-initial-{case_id}",
            query=query,
            history=[],
            expected=tool_call(
                call_id,
                CONDITION_TOOL,
                condition_arguments(
                    comparison_operator,
                    threshold_c,
                ),
            ),
            scenario="condition_initial",
        )

    matched_cases = [
        (
            "gt-open",
            "车内超过27度时打开空调。",
            "gt",
            27.0,
            29.5,
            True,
        ),
        (
            "ge-open",
            "车内至少26度时开启制冷。",
            "ge",
            26.0,
            26.0,
            True,
        ),
        (
            "lt-close",
            "车内低于20度时关闭空调。",
            "lt",
            20.0,
            18.5,
            False,
        ),
        (
            "le-close",
            "车内不高于22度时停止制冷。",
            "le",
            22.0,
            22.0,
            False,
        ),
    ]

    for (
        case_id,
        query,
        comparison_operator,
        threshold_c,
        temperature_c,
        target_enabled,
    ) in matched_cases:
        check_id = f"{case_id}-check"
        arguments = condition_arguments(
            comparison_operator,
            threshold_c,
        )

        history = [
            observation(
                check_id,
                CONDITION_TOOL,
                arguments,
                ok=True,
                data={
                    "temperature_c": temperature_c,
                    "operator": comparison_operator,
                    "threshold_c": threshold_c,
                    "matched": True,
                },
            )
        ]

        add_record(
            records,
            record_id=f"compat-matched-{case_id}",
            query=query,
            history=history,
            expected=tool_call(
                f"{case_id}-control",
                "set_air_conditioner",
                {
                    "enabled": target_enabled,
                },
            ),
            scenario="condition_matched",
        )

    unmatched_cases = [
        (
            "gt-open",
            "车内超过27度时打开空调。",
            "gt",
            27.0,
            24.0,
        ),
        (
            "ge-open",
            "车内至少26度时开启制冷。",
            "ge",
            26.0,
            25.5,
        ),
        (
            "lt-close",
            "车内低于20度时关闭空调。",
            "lt",
            20.0,
            22.0,
        ),
        (
            "le-close",
            "车内不高于22度时停止制冷。",
            "le",
            22.0,
            23.5,
        ),
    ]

    for (
        case_id,
        query,
        comparison_operator,
        threshold_c,
        temperature_c,
    ) in unmatched_cases:
        check_id = f"{case_id}-unmatched-check"
        arguments = condition_arguments(
            comparison_operator,
            threshold_c,
        )

        history = [
            observation(
                check_id,
                CONDITION_TOOL,
                arguments,
                ok=True,
                data={
                    "temperature_c": temperature_c,
                    "operator": comparison_operator,
                    "threshold_c": threshold_c,
                    "matched": False,
                },
            )
        ]

        add_record(
            records,
            record_id=f"compat-unmatched-{case_id}",
            query=query,
            history=history,
            expected=final_answer(
                f"当前车内温度为{temperature_c:.1f}摄氏度，"
                "条件未满足，空调状态不变。"
            ),
            scenario="condition_unmatched",
            required_terms=[
                f"{temperature_c:g}",
                "状态不变",
            ],
        )

    failure_query = (
        "座舱超过二十七度时开启制冷。"
    )

    failure_arguments = condition_arguments(
        "gt",
        27.0,
    )

    add_record(
        records,
        record_id="compat-condition-failure",
        query=failure_query,
        history=[
            observation(
                "failure-check",
                CONDITION_TOOL,
                failure_arguments,
                ok=False,
                error="温度传感器读取失败",
            )
        ],
        expected=final_answer(
            "暂时无法判断温度条件："
            "温度传感器读取失败。"
        ),
        scenario="condition_failure",
        required_terms=[
            "无法",
            "失败",
        ],
    )

    completed_cases = [
        (
            "open",
            "车内超过27度时打开空调。",
            "gt",
            27.0,
            29.5,
            True,
            "开启",
        ),
        (
            "close",
            "车内低于20度时关闭空调。",
            "lt",
            20.0,
            18.5,
            False,
            "关闭",
        ),
    ]

    for (
        case_id,
        query,
        comparison_operator,
        threshold_c,
        temperature_c,
        target_enabled,
        state_text,
    ) in completed_cases:
        condition_id = (
            f"{case_id}-completed-check"
        )
        control_id = (
            f"{case_id}-completed-control"
        )

        arguments = condition_arguments(
            comparison_operator,
            threshold_c,
        )

        history = [
            observation(
                condition_id,
                CONDITION_TOOL,
                arguments,
                ok=True,
                data={
                    "temperature_c": temperature_c,
                    "operator": comparison_operator,
                    "threshold_c": threshold_c,
                    "matched": True,
                },
            ),
            observation(
                control_id,
                "set_air_conditioner",
                {
                    "enabled": target_enabled,
                },
                ok=True,
                data={
                    "enabled": target_enabled,
                    "indicator_led_on": target_enabled,
                },
            ),
        ]

        add_record(
            records,
            record_id=(
                f"compat-completed-{case_id}"
            ),
            query=query,
            history=history,
            expected=final_answer(
                f"当前车内温度为{temperature_c:.1f}摄氏度，"
                f"空调已经{state_text}。"
            ),
            scenario="condition_completed",
            required_terms=[
                f"{temperature_c:g}",
                f"已经{state_text}",
            ],
        )

    ids = [
        record["id"]
        for record in records
    ]

    if len(ids) != len(set(ids)):
        raise ValueError(
            "duplicate compatibility record ids"
        )

    return records


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Generate deterministic condition "
            "compatibility evaluation data"
        )
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "data/agent_planner/"
            "condition_compat_v3.jsonl"
        ),
    )

    args = parser.parse_args()
    records = build_records()

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with args.output.open(
        "w",
        encoding="utf-8",
    ) as stream:
        for record in records:
            stream.write(
                json.dumps(
                    record,
                    ensure_ascii=False,
                )
                + "\n"
            )

    print(
        f"[OUTPUT] {args.output}"
    )

    print(
        f"[RECORDS] {len(records)}"
    )

if __name__ == "__main__":
    main()
