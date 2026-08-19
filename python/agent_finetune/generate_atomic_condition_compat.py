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


ATOMIC_TOOL = "set_air_conditioner_if_temperature"


def atomic_arguments(
    comparison_operator: str,
    threshold_c: float,
    enabled: bool,
) -> dict[str, Any]:
    return {
        "enabled": enabled,
        "operator": comparison_operator,
        "threshold_c": threshold_c,
    }


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


def atomic_result(
    *,
    temperature_c: float,
    comparison_operator: str,
    threshold_c: float,
    target_enabled: bool,
    matched: bool,
) -> dict[str, Any]:
    state_before = not target_enabled
    state_after = target_enabled if matched else state_before

    return {
        "temperature_c": temperature_c,
        "operator": comparison_operator,
        "threshold_c": threshold_c,
        "matched": matched,
        "target_enabled": target_enabled,
        "control_executed": matched,
        "state_before": state_before,
        "state_after": state_after,
        "state_changed": matched,
        "enabled": state_after,
        "indicator_led_on": state_after,
    }


def build_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    initial_cases = [
        (
            "gt-chinese-open",
            "座舱超过二十七度时开启制冷。",
            "gt",
            27.0,
            True,
        ),
        (
            "gt-decimal-open",
            "车内温度高于28.5度就打开空调。",
            "gt",
            28.5,
            True,
        ),
        (
            "ge-chinese-open",
            "车内至少达到二十五度就开启冷气。",
            "ge",
            25.0,
            True,
        ),
        (
            "ge-digit-open",
            "温度不低于26度时启动制冷。",
            "ge",
            26.0,
            True,
        ),
        (
            "lt-chinese-close",
            "车内低于二十度时关闭空调。",
            "lt",
            20.0,
            False,
        ),
        (
            "lt-decimal-close",
            "温度小于18.5度就停止制冷。",
            "lt",
            18.5,
            False,
        ),
        (
            "le-chinese-close",
            "温度至多二十二度时关闭车载空调。",
            "le",
            22.0,
            False,
        ),
        (
            "le-decimal-close",
            "温度不高于23.5度就关闭冷气。",
            "le",
            23.5,
            False,
        ),
    ]

    for (
        case_id,
        query,
        comparison_operator,
        threshold_c,
        target_enabled,
    ) in initial_cases:
        add_record(
            records,
            record_id=f"atomic-initial-{case_id}",
            query=query,
            history=[],
            expected=tool_call(
                f"{case_id}-atomic",
                ATOMIC_TOOL,
                atomic_arguments(
                    comparison_operator,
                    threshold_c,
                    target_enabled,
                ),
            ),
            scenario="atomic_initial",
        )

    result_cases = [
        (
            "gt-open-matched",
            "车内超过27度时打开空调。",
            "gt",
            27.0,
            29.5,
            True,
            True,
        ),
        (
            "ge-open-boundary",
            "车内至少26度时开启制冷。",
            "ge",
            26.0,
            26.0,
            True,
            True,
        ),
        (
            "lt-close-matched",
            "车内低于20度时关闭空调。",
            "lt",
            20.0,
            18.5,
            False,
            True,
        ),
        (
            "le-close-boundary",
            "车内不高于22度时停止制冷。",
            "le",
            22.0,
            22.0,
            False,
            True,
        ),
        (
            "gt-open-unmatched",
            "车内超过27度时打开空调。",
            "gt",
            27.0,
            24.0,
            True,
            False,
        ),
        (
            "ge-open-unmatched",
            "车内至少26度时开启制冷。",
            "ge",
            26.0,
            25.5,
            True,
            False,
        ),
        (
            "lt-close-unmatched",
            "车内低于20度时关闭空调。",
            "lt",
            20.0,
            22.0,
            False,
            False,
        ),
        (
            "le-close-unmatched",
            "车内不高于22度时停止制冷。",
            "le",
            22.0,
            23.5,
            False,
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
        matched,
    ) in result_cases:
        call_id = f"{case_id}-atomic"
        arguments = atomic_arguments(
            comparison_operator,
            threshold_c,
            target_enabled,
        )

        history = [
            observation(
                call_id,
                ATOMIC_TOOL,
                arguments,
                ok=True,
                data=atomic_result(
                    temperature_c=temperature_c,
                    comparison_operator=comparison_operator,
                    threshold_c=threshold_c,
                    target_enabled=target_enabled,
                    matched=matched,
                ),
            )
        ]

        if matched:
            state_text = (
                "开启" if target_enabled else "关闭"
            )
            answer = (
                f"当前车内温度为{temperature_c:.1f}摄氏度，"
                f"条件成立，空调已经{state_text}。"
            )
            required_terms = [state_text]
            scenario = "atomic_completed"
        else:
            answer = (
                f"当前车内温度为{temperature_c:.1f}摄氏度，"
                "条件未满足，未执行空调控制。"
            )
            required_terms = ["未满足"]
            scenario = "atomic_unmatched"

        add_record(
            records,
            record_id=f"atomic-result-{case_id}",
            query=query,
            history=history,
            expected=final_answer(answer),
            scenario=scenario,
            required_terms=required_terms,
        )

    failure_cases = [
        (
            "sensor",
            "座舱超过二十七度时开启制冷。",
            "environment sensor read failed",
            "无法",
        ),
        (
            "control",
            "车内超过27度时打开空调。",
            (
                "temperature condition matched, "
                "but air conditioner control failed"
            ),
            "失败",
        ),
    ]

    for case_id, query, error, required_term in failure_cases:
        arguments = atomic_arguments(
            "gt",
            27.0,
            True,
        )

        add_record(
            records,
            record_id=f"atomic-failure-{case_id}",
            query=query,
            history=[
                observation(
                    f"{case_id}-failure-atomic",
                    ATOMIC_TOOL,
                    arguments,
                    ok=False,
                    error=error,
                )
            ],
            expected=final_answer(
                "暂时无法完成温度条件空调控制。"
            ),
            scenario="atomic_failure",
            required_terms=[required_term],
        )

    ids = [record["id"] for record in records]

    if len(ids) != len(set(ids)):
        raise ValueError("duplicate compatibility record ids")

    return records


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Generate atomic temperature condition "
            "compatibility evaluation data"
        )
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "data/agent_planner/"
            "atomic_condition_compat_v3.jsonl"
        ),
    )
    args = parser.parse_args()
    records = build_records()

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with args.output.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(
                json.dumps(record, ensure_ascii=False)
                + "\n"
            )

    print(f"[OUTPUT] {args.output}")
    print(f"[RECORDS] {len(records)}")


if __name__ == "__main__":
    main()
