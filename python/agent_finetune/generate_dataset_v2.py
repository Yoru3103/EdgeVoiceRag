from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any

from agent_finetune.planner_format import (
    final_answer,
    make_record,
    observation,
    tool_call,
)


def belongs_to_validation(group_id: str) -> bool:
    """
    普通样本按稳定哈希进行 8:2 划分。

    条件工作流按模板分层划分：
    - template 1：高温时开启空调
    - template 5：低温时关闭空调

    同一模板的所有执行阶段必须位于同一集合，
    防止 initial / observation / completed 泄漏。
    """
    if group_id.startswith("condition-template-"):
        validation_condition_groups = {
            "condition-template-1",
            "condition-template-5",
        }
        return group_id in validation_condition_groups

    digest = hashlib.sha256(
        group_id.encode("utf-8")
    ).digest()

    return digest[0] % 5 == 0


def add_metadata(
    record: dict[str, Any],
    *,
    scenario: str,
    group_id: str,
) -> dict[str, Any]:
    record["scenario"] = scenario
    record["group_id"] = group_id
    return record


def build_environment_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    subjects = [
        "车内",
        "车里",
        "座舱",
        "驾驶室",
        "车厢",
    ]

    requests = [
        "现在温度和湿度是多少？",
        "当前有多热，湿度多大？",
        "环境传感器读数是多少？",
        "现在的温湿度情况怎么样？",
    ]

    readings = [
        (18.5, 35.0),
        (23.0, 48.5),
        (27.5, 58.0),
        (31.0, 72.5),
    ]

    queries = [
        subject + request
        for subject in subjects
        for request in requests
    ]

    for index, query in enumerate(queries):
        group_id = f"environment-{index}"
        call_id = f"env-{index}"

        records.append(add_metadata(
            make_record(
                f"{group_id}-initial",
                query,
                [],
                tool_call(
                    call_id,
                    "get_cabin_environment",
                    {},
                ),
            ),
            scenario="environment_initial",
            group_id=group_id,
        ))

        temperature, humidity = readings[
            index % len(readings)
        ]

        success_history = [
            observation(
                call_id,
                "get_cabin_environment",
                {},
                ok=True,
                data={
                    "temperature_c": temperature,
                    "humidity_percent": humidity,
                },
            )
        ]

        records.append(add_metadata(
            make_record(
                f"{group_id}-success",
                query,
                success_history,
                final_answer(
                    f"当前车内温度为{temperature:.1f}摄氏度，"
                    f"湿度为{humidity:.1f}%。"
                ),
            ),
            scenario="environment_success",
            group_id=group_id,
        ))

        # 10条不同表达的传感器失败样本。
        if index < 10:
            failure_history = [
                observation(
                    call_id,
                    "get_cabin_environment",
                    {},
                    ok=False,
                    error="IIO温湿度设备读取失败",
                )
            ]

            records.append(add_metadata(
                make_record(
                    f"{group_id}-failure",
                    query,
                    failure_history,
                    final_answer(
                        "暂时无法读取车内温湿度："
                        "IIO温湿度设备读取失败。"
                    ),
                ),
                scenario="environment_failure",
                group_id=group_id,
            ))

    return records


def build_state_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    devices = [
        "空调",
        "车载空调",
        "制冷系统",
        "冷气",
        "座舱空调",
    ]

    requests = [
        "目前是不是开启状态？",
        "现在还在运行吗？",
        "当前是开着还是关着？",
        "指示灯现在亮着吗？",
    ]

    queries = [
        device + request
        for device in devices
        for request in requests
    ]

    for index, query in enumerate(queries):
        group_id = f"state-{index}"
        call_id = f"state-{index}"
        enabled = index % 2 == 0

        records.append(add_metadata(
            make_record(
                f"{group_id}-initial",
                query,
                [],
                tool_call(
                    call_id,
                    "get_air_conditioner_state",
                    {},
                ),
            ),
            scenario="state_initial",
            group_id=group_id,
        ))

        success_history = [
            observation(
                call_id,
                "get_air_conditioner_state",
                {},
                ok=True,
                data={
                    "enabled": enabled,
                    "indicator_led_on": enabled,
                },
            )
        ]

        records.append(add_metadata(
            make_record(
                f"{group_id}-success",
                query,
                success_history,
                final_answer(
                    "空调当前处于开启状态。"
                    if enabled
                    else "空调当前处于关闭状态。"
                ),
            ),
            scenario="state_success",
            group_id=group_id,
        ))

        if index < 10:
            failure_history = [
                observation(
                    call_id,
                    "get_air_conditioner_state",
                    {},
                    ok=False,
                    error="空调状态接口不可用",
                )
            ]

            records.append(add_metadata(
                make_record(
                    f"{group_id}-failure",
                    query,
                    failure_history,
                    final_answer(
                        "暂时无法查询空调状态："
                        "空调状态接口不可用。"
                    ),
                ),
                scenario="state_failure",
                group_id=group_id,
            ))

    return records


def build_control_records(
    *,
    enabled: bool,
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    if enabled:
        actions = [
            "打开空调",
            "开启空调",
            "启动制冷",
            "打开冷气",
            "开启制冷模式",
            "让空调运行",
            "把空调打开",
            "把冷气打开",
            "开始制冷",
            "启动空调",
            "打开车载空调",
            "开启车内制冷",
        ]

        final_text = "空调已经开启。"
        prefix = "open"
    else:
        actions = [
            "关闭空调",
            "关掉空调",
            "停止制冷",
            "关闭冷气",
            "退出制冷模式",
            "让空调停止运行",
            "把空调关掉",
            "把冷气关掉",
            "结束制冷",
            "停止空调",
            "关闭车载空调",
            "停止车内制冷",
        ]

        final_text = "空调已经关闭。"
        prefix = "close"

    polite_prefixes = [
        "请",
        "麻烦",
        "现在",
        "帮我",
        "立即",
    ]

    queries = [
        polite + action + "。"
        for polite in polite_prefixes
        for action in actions
    ]

    for index, query in enumerate(queries):
        group_id = f"{prefix}-{index}"
        call_id = f"{prefix}-{index}"
        arguments = {"enabled": enabled}

        records.append(add_metadata(
            make_record(
                f"{group_id}-initial",
                query,
                [],
                tool_call(
                    call_id,
                    "set_air_conditioner",
                    arguments,
                ),
            ),
            scenario=f"{prefix}_initial",
            group_id=group_id,
        ))

        success_history = [
            observation(
                call_id,
                "set_air_conditioner",
                arguments,
                ok=True,
                data={
                    "enabled": enabled,
                    "indicator_led_on": enabled,
                },
            )
        ]

        records.append(add_metadata(
            make_record(
                f"{group_id}-success",
                query,
                success_history,
                final_answer(final_text),
            ),
            scenario=f"{prefix}_success",
            group_id=group_id,
        ))

        # 控制失败后必须结束，不能再次调用工具。
        if index < 10:
            failure_history = [
                observation(
                    call_id,
                    "set_air_conditioner",
                    arguments,
                    ok=False,
                    error="空调控制接口执行失败",
                )
            ]

            records.append(add_metadata(
                make_record(
                    f"{group_id}-failure",
                    query,
                    failure_history,
                    final_answer(
                        "暂时无法完成空调控制："
                        "空调控制接口执行失败。"
                    ),
                ),
                scenario=f"{prefix}_failure",
                group_id=group_id,
            ))

    return records


def build_conditional_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    # 比较方式、目标状态、查询模板、阈值、测试温度。
    templates = [
        (
            "high",
            True,
            "如果车内温度高于{threshold}度，请打开空调。",
            26.0,
            [23.0, 28.0, 25.0, 30.0],
        ),
        (
            "high",
            True,
            "当座舱温度超过{threshold}度时启动制冷。",
            27.0,
            [24.0, 29.0, 26.0, 31.0],
        ),
        (
            "high",
            True,
            "检测到驾驶室高于{threshold}度就开启冷气。",
            25.0,
            [22.0, 27.0, 24.0, 29.0],
        ),
        (
            "high",
            True,
            "车厢温度超过{threshold}度的话把空调打开。",
            28.0,
            [25.0, 30.0, 27.0, 32.0],
        ),
        (
            "low",
            False,
            "如果车内温度低于{threshold}度，请关闭空调。",
            20.0,
            [18.0, 22.0, 19.0, 24.0],
        ),
        (
            "low",
            False,
            "当座舱温度小于{threshold}度时停止制冷。",
            21.0,
            [19.0, 23.0, 20.0, 25.0],
        ),
        (
            "low",
            False,
            "驾驶室低于{threshold}度就把冷气关掉。",
            19.0,
            [17.0, 21.0, 18.0, 23.0],
        ),
        (
            "low",
            False,
            "车厢温度低于{threshold}度时关闭车载空调。",
            22.0,
            [20.0, 24.0, 21.0, 26.0],
        ),
    ]

    for template_index, (
        comparison,
        target_enabled,
        template,
        threshold,
        temperatures,
    ) in enumerate(templates):
        query = template.format(
            threshold=f"{threshold:.0f}"
        )

        # 同一个自然语言模板的所有状态进入同一数据分区。
        group_id = f"condition-template-{template_index}"

        for case_index, temperature in enumerate(
            temperatures
        ):
            record_prefix = (
                f"condition-{template_index}-{case_index}"
            )

            environment_call_id = (
                f"{record_prefix}-environment"
            )

            records.append(add_metadata(
                make_record(
                    f"{record_prefix}-initial",
                    query,
                    [],
                    tool_call(
                        environment_call_id,
                        "get_cabin_environment",
                        {},
                    ),
                ),
                scenario="condition_initial",
                group_id=group_id,
            ))

            environment_history = [
                observation(
                    environment_call_id,
                    "get_cabin_environment",
                    {},
                    ok=True,
                    data={
                        "temperature_c": temperature,
                        "humidity_percent": 50.0,
                    },
                )
            ]

            condition_met = (
                temperature > threshold
                if comparison == "high"
                else temperature < threshold
            )

            if not condition_met:
                records.append(add_metadata(
                    make_record(
                        f"{record_prefix}-not-met",
                        query,
                        environment_history,
                        final_answer(
                            f"当前车内温度为{temperature:.1f}摄氏度，"
                            "未满足设定条件，空调状态不变。"
                        ),
                    ),
                    scenario="condition_not_met",
                    group_id=group_id,
                ))

                continue

            control_call_id = (
                f"{record_prefix}-control"
            )

            control_arguments = {
                "enabled": target_enabled
            }

            records.append(add_metadata(
                make_record(
                    f"{record_prefix}-control",
                    query,
                    environment_history,
                    tool_call(
                        control_call_id,
                        "set_air_conditioner",
                        control_arguments,
                    ),
                ),
                scenario="condition_control",
                group_id=group_id,
            ))

            completed_history = (
                environment_history
                + [
                    observation(
                        control_call_id,
                        "set_air_conditioner",
                        control_arguments,
                        ok=True,
                        data={
                            "enabled": target_enabled,
                            "indicator_led_on": target_enabled,
                        },
                    )
                ]
            )

            records.append(add_metadata(
                make_record(
                    f"{record_prefix}-completed",
                    query,
                    completed_history,
                    final_answer(
                        f"当前车内温度为{temperature:.1f}摄氏度，"
                        + (
                            "满足设定条件，空调已经开启。"
                            if target_enabled
                            else "满足设定条件，空调已经关闭。"
                        )
                    ),
                ),
                scenario="condition_completed",
                group_id=group_id,
            ))

    return records


def build_records() -> list[dict[str, Any]]:
    records = (
        build_environment_records()
        + build_state_records()
        + build_control_records(enabled=True)
        + build_control_records(enabled=False)
        + build_conditional_records()
    )

    ids = [record["id"] for record in records]

    if len(ids) != len(set(ids)):
        raise ValueError("duplicate record ids generated")

    return records


def write_jsonl(
    path: Path,
    records: list[dict[str, Any]],
) -> None:
    path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with path.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(
                json.dumps(
                    record,
                    ensure_ascii=False,
                )
                + "\n"
            )


def print_distribution(
    name: str,
    records: list[dict[str, Any]],
) -> None:
    counts = Counter(
        record["scenario"]
        for record in records
    )

    print(f"[{name}] total={len(records)}")

    for scenario in sorted(counts):
        print(
            f"  {scenario}: {counts[scenario]}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Generate balanced v2 vehicle Agent SFT data"
        )
    )

    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("data/agent_planner"),
    )

    args = parser.parse_args()

    records = build_records()

    train_records = []
    validation_records = []

    for record in records:
        if belongs_to_validation(
            record["group_id"]
        ):
            validation_records.append(record)
        else:
            train_records.append(record)

    train_groups = {
        record["group_id"]
        for record in train_records
    }

    validation_groups = {
        record["group_id"]
        for record in validation_records
    }

    overlap = train_groups & validation_groups

    if overlap:
        raise ValueError(
            f"train/validation group leakage: {overlap}"
        )

    train_path = (
        args.output_dir / "train_v2.jsonl"
    )

    validation_path = (
        args.output_dir / "validation_v2.jsonl"
    )

    write_jsonl(
        train_path,
        train_records,
    )

    write_jsonl(
        validation_path,
        validation_records,
    )

    print_distribution(
        "TRAIN",
        train_records,
    )

    print_distribution(
        "VALIDATION",
        validation_records,
    )

    print("[OUTPUT]", train_path)
    print("[OUTPUT]", validation_path)


if __name__ == "__main__":
    main()
