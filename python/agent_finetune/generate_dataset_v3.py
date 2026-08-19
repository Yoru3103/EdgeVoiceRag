from __future__ import annotations

import argparse
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from agent_finetune.generate_dataset_v2 import (
    add_metadata,
    belongs_to_validation,
    build_control_records,
    build_environment_records,
    build_state_records,
)
from agent_finetune.planner_format import (
    final_answer,
    make_record,
    observation,
    tool_call,
)


ATOMIC_TOOL = "set_air_conditioner_if_temperature"


@dataclass(frozen=True)
class AtomicConditionSpec:
    query: str
    operator: str
    threshold_c: float
    target_enabled: bool


def build_atomic_specs() -> list[AtomicConditionSpec]:
    raw_specs = [
        # gt：严格大于。既包含常见的开启，也包含反向关闭，
        # 防止模型把 gt 机械记忆成 enabled=true。
        ("如果车内温度超过二十七度就打开空调。", "gt", 27.0, True),
        ("当座舱高于28.5度时开启制冷。", "gt", 28.5, True),
        ("驾驶室大于25度就打开冷气。", "gt", 25.0, True),
        ("车厢温度超过30度请启动空调。", "gt", 30.0, True),
        ("温度高于二十六度时让空调运行。", "gt", 26.0, True),
        ("一旦车里超过24.5度就开启车载空调。", "gt", 24.5, True),
        ("如果座舱温度大于29度帮我开始制冷。", "gt", 29.0, True),
        ("温度超过三十二度时关闭空调以保护设备。", "gt", 32.0, False),
        ("车内高于31.5度就停止制冷。", "gt", 31.5, False),
        ("驾驶室超过三十度时把冷气关掉。", "gt", 30.0, False),

        # ge：大于等于，重点覆盖“不低于”和边界相等。
        ("车内至少二十五度时开启空调。", "ge", 25.0, True),
        ("座舱温度不低于26度就启动制冷。", "ge", 26.0, True),
        ("温度达到二十八度或更高时打开冷气。", "ge", 28.0, True),
        ("车厢27度及以上就开启车载空调。", "ge", 27.0, True),
        ("驾驶室大于等于24.5度时开始制冷。", "ge", 24.5, True),
        ("温度达到二十九度便打开空调。", "ge", 29.0, True),
        ("车内温度不少于23度就让空调运行。", "ge", 23.0, True),
        ("温度达到35度或更高时关闭空调。", "ge", 35.0, False),
        ("座舱不低于33.5度时停止制冷。", "ge", 33.5, False),
        ("车内32度及以上就把冷气关掉。", "ge", 32.0, False),

        # lt：严格小于，包含“不到/未达到”和等值不成立样本。
        ("车内低于二十度时关闭空调。", "lt", 20.0, False),
        ("温度小于18.5度就停止制冷。", "lt", 18.5, False),
        ("座舱不到22度时把空调关掉。", "lt", 22.0, False),
        ("驾驶室少于十九度就关闭冷气。", "lt", 19.0, False),
        ("车厢低于21.5度时退出制冷模式。", "lt", 21.5, False),
        ("温度未达到二十四度时关闭车载空调。", "lt", 24.0, False),
        ("车里低于二十三度就停止空调。", "lt", 23.0, False),
        ("温度低于十六度时打开空调。", "lt", 16.0, True),
        ("车内小于17.5度就启动空调。", "lt", 17.5, True),
        ("座舱不到十八度时开启冷气。", "lt", 18.0, True),

        # le：小于等于，重点覆盖本轮失败的“不高于”。
        ("温度不高于23.5度就关闭冷气。", "le", 23.5, False),
        ("车内温度至多二十二度时关闭空调。", "le", 22.0, False),
        ("座舱低于或等于20度就停止制冷。", "le", 20.0, False),
        ("车厢24度及以下时关掉车载空调。", "le", 24.0, False),
        ("驾驶室温度不超过21.5度就关闭冷气。", "le", 21.5, False),
        ("车内最高二十三度时停止空调。", "le", 23.0, False),
        ("温度小于等于19度时退出制冷模式。", "le", 19.0, False),
        ("座舱不高于十六度时打开空调。", "le", 16.0, True),
        ("车内至多17.5度就启动空调。", "le", 17.5, True),
        ("温度十八度及以下时开启冷气。", "le", 18.0, True),
    ]

    return [
        AtomicConditionSpec(*values)
        for values in raw_specs
    ]


def atomic_arguments(
    spec: AtomicConditionSpec,
) -> dict[str, Any]:
    return {
        "enabled": spec.target_enabled,
        "operator": spec.operator,
        "threshold_c": spec.threshold_c,
    }


def matched_temperature(
    spec: AtomicConditionSpec,
    index: int,
) -> float:
    if spec.operator == "gt":
        return spec.threshold_c + (0.5 if index % 2 else 2.0)
    if spec.operator == "ge":
        return spec.threshold_c if index % 2 == 0 else spec.threshold_c + 0.5
    if spec.operator == "lt":
        return spec.threshold_c - (0.5 if index % 2 else 2.0)
    return spec.threshold_c if index % 2 == 0 else spec.threshold_c - 0.5


def unmatched_temperature(
    spec: AtomicConditionSpec,
    index: int,
) -> float:
    if spec.operator == "gt":
        return spec.threshold_c if index % 2 == 0 else spec.threshold_c - 0.5
    if spec.operator == "ge":
        return spec.threshold_c - (0.5 if index % 2 else 2.0)
    if spec.operator == "lt":
        return spec.threshold_c if index % 2 == 0 else spec.threshold_c + 0.5
    return spec.threshold_c + (0.5 if index % 2 else 2.0)


def atomic_result(
    spec: AtomicConditionSpec,
    *,
    temperature_c: float,
    matched: bool,
) -> dict[str, Any]:
    state_before = not spec.target_enabled
    state_after = spec.target_enabled if matched else state_before

    return {
        "temperature_c": temperature_c,
        "operator": spec.operator,
        "threshold_c": spec.threshold_c,
        "matched": matched,
        "target_enabled": spec.target_enabled,
        "control_executed": matched,
        "state_before": state_before,
        "state_after": state_after,
        "state_changed": matched,
        "enabled": state_after,
        "indicator_led_on": state_after,
    }


def add_atomic_metadata(
    record: dict[str, Any],
    *,
    scenario: str,
    group_id: str,
    spec: AtomicConditionSpec,
) -> dict[str, Any]:
    add_metadata(
        record,
        scenario=scenario,
        group_id=group_id,
    )
    record["operator"] = spec.operator
    record["target_enabled"] = spec.target_enabled
    return record


def build_atomic_condition_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    operator_indexes: Counter[str] = Counter()

    for spec in build_atomic_specs():
        index = operator_indexes[spec.operator]
        operator_indexes[spec.operator] += 1

        group_id = f"atomic-{spec.operator}-{index}"
        call_id = f"{group_id}-call"
        arguments = atomic_arguments(spec)

        records.append(add_atomic_metadata(
            make_record(
                f"{group_id}-initial",
                spec.query,
                [],
                tool_call(
                    call_id,
                    ATOMIC_TOOL,
                    arguments,
                ),
            ),
            scenario="atomic_initial",
            group_id=group_id,
            spec=spec,
        ))

        matched_value = matched_temperature(spec, index)
        matched_history = [
            observation(
                call_id,
                ATOMIC_TOOL,
                arguments,
                ok=True,
                data=atomic_result(
                    spec,
                    temperature_c=matched_value,
                    matched=True,
                ),
            )
        ]
        state_text = "开启" if spec.target_enabled else "关闭"

        records.append(add_atomic_metadata(
            make_record(
                f"{group_id}-matched",
                spec.query,
                matched_history,
                final_answer(
                    f"当前车内温度为{matched_value:.1f}摄氏度，"
                    f"条件成立，空调已经{state_text}。"
                ),
            ),
            scenario="atomic_completed",
            group_id=group_id,
            spec=spec,
        ))

        unmatched_value = unmatched_temperature(spec, index)
        unmatched_history = [
            observation(
                call_id,
                ATOMIC_TOOL,
                arguments,
                ok=True,
                data=atomic_result(
                    spec,
                    temperature_c=unmatched_value,
                    matched=False,
                ),
            )
        ]

        records.append(add_atomic_metadata(
            make_record(
                f"{group_id}-unmatched",
                spec.query,
                unmatched_history,
                final_answer(
                    f"当前车内温度为{unmatched_value:.1f}摄氏度，"
                    "条件未满足，未执行空调控制。"
                ),
            ),
            scenario="atomic_unmatched",
            group_id=group_id,
            spec=spec,
        ))

        # 每种运算符在训练集和验证集中都包含传感器失败、控制失败。
        if index in {0, 1}:
            error = "environment sensor read failed"
            answer = "暂时无法读取温度并执行条件空调控制。"
            failure_kind = "sensor"
        elif index in {5, 6}:
            error = (
                "temperature condition matched, "
                "but air conditioner control failed"
            )
            answer = "温度条件成立，但空调控制失败。"
            failure_kind = "control"
        else:
            continue

        failure_record = add_atomic_metadata(
            make_record(
                f"{group_id}-failure-{failure_kind}",
                spec.query,
                [
                    observation(
                        call_id,
                        ATOMIC_TOOL,
                        arguments,
                        ok=False,
                        error=error,
                    )
                ],
                final_answer(answer),
            ),
            scenario=f"atomic_failure_{failure_kind}",
            group_id=group_id,
            spec=spec,
        )
        records.append(failure_record)

    return records


def atomic_group_is_validation(group_id: str) -> bool:
    index = int(group_id.rsplit("-", 1)[1])
    return index % 5 == 0


def record_is_validation(record: dict[str, Any]) -> bool:
    group_id = record["group_id"]

    if group_id.startswith("atomic-"):
        return atomic_group_is_validation(group_id)

    return belongs_to_validation(group_id)


def validate_split(
    train_records: list[dict[str, Any]],
    validation_records: list[dict[str, Any]],
) -> None:
    train_groups = {record["group_id"] for record in train_records}
    validation_groups = {
        record["group_id"]
        for record in validation_records
    }
    overlap = train_groups & validation_groups

    if overlap:
        raise ValueError(f"train/validation group leakage: {sorted(overlap)}")

    all_ids = [
        record["id"]
        for record in train_records + validation_records
    ]
    if len(all_ids) != len(set(all_ids)):
        raise ValueError("duplicate record ids generated")

    for name, records in (
        ("train", train_records),
        ("validation", validation_records),
    ):
        atomic_records = [
            record
            for record in records
            if record["group_id"].startswith("atomic-")
        ]
        operators = {record["operator"] for record in atomic_records}
        targets = {
            record["target_enabled"]
            for record in atomic_records
        }

        if operators != {"gt", "ge", "lt", "le"}:
            raise ValueError(f"{name} lacks operator coverage: {operators}")
        if targets != {True, False}:
            raise ValueError(f"{name} lacks target-state coverage: {targets}")


def write_jsonl(
    path: Path,
    records: list[dict[str, Any]],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(
                json.dumps(record, ensure_ascii=False)
                + "\n"
            )


def print_distribution(
    name: str,
    records: list[dict[str, Any]],
) -> None:
    scenarios = Counter(record["scenario"] for record in records)
    operators = Counter(
        record["operator"]
        for record in records
        if "operator" in record
    )

    print(f"[{name}] total={len(records)}")
    for scenario in sorted(scenarios):
        print(f"  {scenario}: {scenarios[scenario]}")
    print(f"  atomic_operators: {dict(sorted(operators.items()))}")


def build_records() -> list[dict[str, Any]]:
    return (
        build_environment_records()
        + build_state_records()
        + build_control_records(enabled=True)
        + build_control_records(enabled=False)
        + build_atomic_condition_records()
    )


def split_records(
    records: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    train_records = [
        record
        for record in records
        if not record_is_validation(record)
    ]
    validation_records = [
        record
        for record in records
        if record_is_validation(record)
    ]
    validate_split(train_records, validation_records)
    return train_records, validation_records


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate atomic-condition vehicle Agent v3 SFT data"
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("data/agent_planner"),
    )
    args = parser.parse_args()

    records = build_records()
    train_records, validation_records = split_records(records)

    train_path = args.output_dir / "train_v3.jsonl"
    validation_path = args.output_dir / "validation_v3.jsonl"
    write_jsonl(train_path, train_records)
    write_jsonl(validation_path, validation_records)

    print_distribution("TRAIN", train_records)
    print_distribution("VALIDATION", validation_records)
    print("[OUTPUT]", train_path)
    print("[OUTPUT]", validation_path)


if __name__ == "__main__":
    main()
