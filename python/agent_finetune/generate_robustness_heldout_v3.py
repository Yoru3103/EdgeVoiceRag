from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from agent_finetune.generate_dataset_v3 import build_records as build_v3_records
from agent_finetune.planner_format import (
    final_answer,
    make_record,
    observation,
    tool_call,
    validate_action,
)


ATOMIC_TOOL = "set_air_conditioner_if_temperature"


@dataclass(frozen=True)
class AtomicCase:
    case_id: str
    query: str
    operator: str
    threshold_c: float
    enabled: bool
    category: str
    difficulty: str


def normalize_query(text: str) -> str:
    return re.sub(r"[^0-9a-z\u4e00-\u9fff]+", "", text.lower())


def prompt_query(record: dict[str, Any]) -> str:
    prompt = record["messages"][0]["content"]
    begin_marker = "\n\n用户原始任务：\n"
    end_marker = "\n\n已经执行的步骤和观察结果：\n"
    begin = prompt.index(begin_marker) + len(begin_marker)
    end = prompt.index(end_marker, begin)
    return prompt[begin:end]


def atomic_arguments(case: AtomicCase) -> dict[str, Any]:
    return {
        "enabled": case.enabled,
        "operator": case.operator,
        "threshold_c": case.threshold_c,
    }


def atomic_result(
    case: AtomicCase,
    *,
    temperature_c: float,
    matched: bool,
) -> dict[str, Any]:
    before = not case.enabled
    after = case.enabled if matched else before
    return {
        "temperature_c": temperature_c,
        "operator": case.operator,
        "threshold_c": case.threshold_c,
        "matched": matched,
        "target_enabled": case.enabled,
        "control_executed": matched,
        "state_before": before,
        "state_after": after,
        "state_changed": matched,
        "enabled": after,
        "indicator_led_on": after,
    }


def add_record(
    records: list[dict[str, Any]],
    *,
    record_id: str,
    query: str,
    history: list[dict[str, Any]],
    expected: dict[str, Any],
    scenario: str,
    category: str,
    difficulty: str,
    required_terms: list[str] | None = None,
    required_term_groups: list[list[str]] | None = None,
) -> None:
    record = make_record(record_id, query, history, expected)
    record.update({
        "query": query,
        "scenario": scenario,
        "category": category,
        "difficulty": difficulty,
    })
    if required_terms:
        record["required_answer_terms"] = required_terms
    if required_term_groups:
        record["required_answer_term_groups"] = required_term_groups
    records.append(record)


def build_atomic_cases() -> list[AtomicCase]:
    return [
        AtomicCase("gt-colloquial-open", "车里要是比二十六点五度还热，就把冷气整上。", "gt", 26.5, True, "colloquial", "medium"),
        AtomicCase("gt-colloquial-close", "温度冒过三十一度就别再制冷了。", "gt", 31.0, False, "colloquial", "hard"),
        AtomicCase("gt-asr-open", "车里超二十七点五度就开空条。", "gt", 27.5, True, "asr_noise", "hard"),
        AtomicCase("gt-negation-close", "不是低于二十二度才关，是高过三十点五度才关冷气。", "gt", 30.5, False, "negation", "hard"),
        AtomicCase("gt-redundant-open", "没超过二十四度就别动，真超过二十四度再开空调。", "gt", 24.0, True, "distractor", "hard"),
        AtomicCase("ge-colloquial-open", "二十五点五度往上就给空调通上。", "ge", 25.5, True, "colloquial", "medium"),
        AtomicCase("ge-boundary-open", "一到二十八点二五度，冷气就得开。", "ge", 28.25, True, "boundary", "hard"),
        AtomicCase("ge-asr-open", "温度不底于二十六点五度就启动制冷。", "ge", 26.5, True, "asr_noise", "hard"),
        AtomicCase("ge-negation-close", "别在低温时关，达到三十三度再把空调停掉。", "ge", 33.0, False, "negation", "hard"),
        AtomicCase("ge-colloquial-close", "三十四度起就让冷气歇着。", "ge", 34.0, False, "colloquial", "hard"),
        AtomicCase("lt-colloquial-close", "车里还没到二十一点五度的话，制冷先停。", "lt", 21.5, False, "colloquial", "medium"),
        AtomicCase("lt-colloquial-open", "温度没够十七度也把空调打开。", "lt", 17.0, True, "colloquial", "hard"),
        AtomicCase("lt-asr-close", "座舱底于十九点五度就关掉空调。", "lt", 19.5, False, "asr_noise", "hard"),
        AtomicCase("lt-negation-open", "不是二十度以上，得不到二十度才开启冷气。", "lt", 20.0, True, "negation", "hard"),
        AtomicCase("lt-boundary-close", "只要温度还差一点才到二十三度，就停止制冷。", "lt", 23.0, False, "boundary", "hard"),
        AtomicCase("le-colloquial-close", "温度二十二点五度封顶时把制冷停掉。", "le", 22.5, False, "colloquial", "hard"),
        AtomicCase("le-colloquial-open", "二十度往下都把空调开着。", "le", 20.0, True, "colloquial", "medium"),
        AtomicCase("le-asr-close", "温度不高与二十三点二五度就关冷气。", "le", 23.25, False, "asr_noise", "hard"),
        AtomicCase("le-boundary-open", "最多十八点五度的话就启动空调。", "le", 18.5, True, "boundary", "medium"),
        AtomicCase("le-negation-close", "超过二十四度别关，二十四度以内才关空调。", "le", 24.0, False, "negation", "hard"),
    ]


def build_atomic_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    cases = build_atomic_cases()

    for case in cases:
        call_id = f"robust-{case.case_id}"
        add_record(
            records,
            record_id=f"{case.case_id}-initial",
            query=case.query,
            history=[],
            expected=tool_call(call_id, ATOMIC_TOOL, atomic_arguments(case)),
            scenario="atomic_initial",
            category=case.category,
            difficulty=case.difficulty,
        )

    # Eight unseen workflows cover every operator and both target states.
    result_cases = [cases[index] for index in (0, 1, 5, 9, 10, 11, 15, 16)]
    for index, case in enumerate(result_cases):
        arguments = atomic_arguments(case)
        call_id = f"robust-result-{case.case_id}"
        if case.operator in {"gt", "ge"}:
            matched_temperature = case.threshold_c + (0.0 if case.operator == "ge" else 1.25)
            unmatched_temperature = case.threshold_c - 0.75
        else:
            matched_temperature = case.threshold_c - (0.0 if case.operator == "le" else 1.25)
            unmatched_temperature = case.threshold_c + 0.75

        for matched, temperature in (
            (True, matched_temperature),
            (False, unmatched_temperature),
        ):
            history = [observation(
                call_id,
                ATOMIC_TOOL,
                arguments,
                ok=True,
                data=atomic_result(case, temperature_c=temperature, matched=matched),
            )]
            state = "开启" if case.enabled else "关闭"
            required_groups = (
                [["开启", "打开", "启动"]]
                if matched and case.enabled
                else [["关闭", "关掉", "停止"]]
                if matched
                else [["不变", "未执行", "没有执行", "无需"]]
            )
            add_record(
                records,
                record_id=f"{case.case_id}-{'matched' if matched else 'unmatched'}",
                query=case.query,
                history=history,
                expected=final_answer(
                    f"条件{'成立' if matched else '不成立'}，"
                    + (f"空调已经{state}。" if matched else "空调状态不变。")
                ),
                scenario="atomic_matched" if matched else "atomic_unmatched",
                category=case.category,
                difficulty=case.difficulty,
                required_term_groups=required_groups,
            )

    failure_case = cases[2]
    arguments = atomic_arguments(failure_case)
    for failure_type, error in (
        ("sensor", "temperature sensor read failed"),
        ("control", "air conditioner verification failed"),
    ):
        add_record(
            records,
            record_id=f"atomic-{failure_type}-failure",
            query=failure_case.query,
            history=[observation(
                f"robust-{failure_type}-failure",
                ATOMIC_TOOL,
                arguments,
                ok=False,
                error=error,
            )],
            expected=final_answer("暂时无法完成温度条件空调控制。"),
            scenario="atomic_failure",
            category="tool_failure",
            difficulty="medium",
            required_term_groups=[["无法", "不能", "失败"]],
        )
    return records


def build_basic_tool_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    environment_queries = [
        ("车厢里头现在啥温度，湿气重不重？", "colloquial", "medium"),
        ("报一下驾驶位这会儿的温湿度数。", "colloquial", "medium"),
        ("看看座仓环境传感器读多少。", "asr_noise", "hard"),
        ("我想知道车里此刻的温度还有相对湿度。", "paraphrase", "easy"),
    ]
    for index, (query, category, difficulty) in enumerate(environment_queries):
        add_record(
            records,
            record_id=f"robust-environment-{index}",
            query=query,
            history=[],
            expected=tool_call(f"robust-env-{index}", "get_cabin_environment", {}),
            scenario="environment_initial",
            category=category,
            difficulty=difficulty,
        )

    for index, (temperature, humidity) in enumerate(((16.8, 41.5), (28.6, 67.2))):
        query = "刚才读到的座舱环境怎么样？"
        add_record(
            records,
            record_id=f"robust-environment-result-{index}",
            query=query,
            history=[observation(
                "robust-env-result",
                "get_cabin_environment",
                {},
                ok=True,
                data={"temperature_c": temperature, "humidity_percent": humidity},
            )],
            expected=final_answer(f"温度{temperature:.1f}摄氏度，湿度{humidity:.1f}%。"),
            scenario="environment_result",
            category="observation",
            difficulty="medium",
            required_terms=[f"{temperature:.1f}", f"{humidity:.1f}"],
        )

    state_queries = [
        "冷气这阵儿到底转没转？",
        "帮忙瞅眼空调灯是不是亮的。",
        "制冷系统当前啥状态？",
        "空条现在开着没有？",
    ]
    for index, query in enumerate(state_queries):
        add_record(
            records,
            record_id=f"robust-state-{index}",
            query=query,
            history=[],
            expected=tool_call(f"robust-state-{index}", "get_air_conditioner_state", {}),
            scenario="state_initial",
            category="asr_noise" if index == 3 else "colloquial",
            difficulty="hard" if index in {0, 3} else "medium",
        )

    for enabled in (True, False):
        add_record(
            records,
            record_id=f"robust-state-result-{int(enabled)}",
            query="刚查的空调是什么状态？",
            history=[observation(
                "robust-state-result",
                "get_air_conditioner_state",
                {},
                ok=True,
                data={"enabled": enabled, "indicator_led_on": enabled},
            )],
            expected=final_answer("空调当前开启。" if enabled else "空调当前关闭。"),
            scenario="state_result",
            category="observation",
            difficulty="medium",
            required_term_groups=(
                [["开启", "打开", "运行"]]
                if enabled
                else [["关闭", "关掉", "停止"]]
            ),
        )

    controls = [
        ("热得慌，冷气安排一下。", True, "colloquial", "hard"),
        ("麻烦让制冷开始工作。", True, "paraphrase", "medium"),
        ("把空条给我开开。", True, "asr_noise", "hard"),
        ("不用制冷了，让它歇会儿。", False, "colloquial", "hard"),
        ("把驾驶室冷气停了吧。", False, "paraphrase", "medium"),
        ("空条关一下。", False, "asr_noise", "hard"),
    ]
    for index, (query, enabled, category, difficulty) in enumerate(controls):
        add_record(
            records,
            record_id=f"robust-control-{index}",
            query=query,
            history=[],
            expected=tool_call(
                f"robust-control-{index}",
                "set_air_conditioner",
                {"enabled": enabled},
            ),
            scenario="control_initial",
            category=category,
            difficulty=difficulty,
        )
    return records


def build_unsupported_records() -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    cases = [
        ("温度超过二十六度并且湿度高于百分之七十才开空调。", "compound_condition"),
        ("把空调设成二十四度。", "unsupported_parameter"),
        ("每隔十分钟查一次温度，热了就开冷气。", "scheduled_action"),
        ("把驾驶位车窗降下来。", "unknown_device"),
        ("放一首轻音乐。", "unknown_device"),
    ]
    for index, (query, category) in enumerate(cases):
        add_record(
            records,
            record_id=f"robust-unsupported-{index}",
            query=query,
            history=[],
            expected=final_answer("当前工具无法完成这项请求。"),
            scenario="unsupported",
            category=category,
            difficulty="hard",
            required_term_groups=[["无法", "不能", "不支持", "没有"]],
        )
    return records


def validate_records(records: list[dict[str, Any]]) -> None:
    ids = [record["id"] for record in records]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate robustness record ids")

    training_queries = {
        normalize_query(prompt_query(record))
        for record in build_v3_records()
    }
    overlaps = sorted({
        record["query"]
        for record in records
        if normalize_query(record["query"]) in training_queries
    })
    if overlaps:
        raise ValueError(f"held-out query leakage: {overlaps}")

    for record in records:
        result = validate_action(record["expected"])
        if not result.ok:
            raise ValueError(f"{record['id']}: {result.error}")

    operators = {
        record["expected"]["tool_call"]["arguments"]["operator"]
        for record in records
        if record["scenario"] == "atomic_initial"
    }
    if operators != {"gt", "ge", "lt", "le"}:
        raise ValueError(f"incomplete operator coverage: {operators}")


def build_records() -> list[dict[str, Any]]:
    records = (
        build_atomic_records()
        + build_basic_tool_records()
        + build_unsupported_records()
    )
    validate_records(records)
    return records


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate independent v3 Agent robustness held-out data"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/agent_planner/robustness_heldout_v3.jsonl"),
    )
    args = parser.parse_args()
    records = build_records()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as stream:
        for record in records:
            stream.write(json.dumps(record, ensure_ascii=False) + "\n")
    print(f"[OUTPUT] {args.output}")
    print(f"[RECORDS] {len(records)}")


if __name__ == "__main__":
    main()
