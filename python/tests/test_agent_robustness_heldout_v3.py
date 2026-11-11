from collections import Counter

from agent_finetune.generate_robustness_heldout_v3 import (
    build_records,
    normalize_query,
    prompt_query,
)
from agent_finetune.generate_dataset_v3 import build_records as build_v3_records


def test_robustness_heldout_has_expected_distribution() -> None:
    records = build_records()
    scenarios = Counter(record["scenario"] for record in records)

    assert len(records) == 61
    assert scenarios == {
        "atomic_initial": 20,
        "atomic_matched": 8,
        "atomic_unmatched": 8,
        "atomic_failure": 2,
        "environment_initial": 4,
        "environment_result": 2,
        "state_initial": 4,
        "state_result": 2,
        "control_initial": 6,
        "unsupported": 5,
    }


def test_robustness_heldout_has_no_exact_query_leakage() -> None:
    training_queries = {
        normalize_query(prompt_query(record))
        for record in build_v3_records()
    }
    heldout_queries = {
        normalize_query(record["query"])
        for record in build_records()
    }

    assert not training_queries & heldout_queries


def test_robustness_atomic_cases_cover_operators_and_targets() -> None:
    initial = [
        record
        for record in build_records()
        if record["scenario"] == "atomic_initial"
    ]
    operators = Counter(
        record["expected"]["tool_call"]["arguments"]["operator"]
        for record in initial
    )
    targets = {
        record["expected"]["tool_call"]["arguments"]["enabled"]
        for record in initial
    }

    assert operators == {"gt": 5, "ge": 5, "lt": 5, "le": 5}
    assert targets == {True, False}


def test_robustness_records_use_compact_runtime_prompt() -> None:
    for record in build_records():
        prompt = record["messages"][0]["content"]
        assert '"name":"set_air_conditioner_if_temperature"' in prompt
        assert '\n  {\n    "description"' not in prompt
