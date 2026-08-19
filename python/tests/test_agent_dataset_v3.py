from collections import Counter

from agent_finetune.generate_dataset_v3 import (
    ATOMIC_TOOL,
    build_atomic_condition_records,
    build_atomic_specs,
    build_records,
    split_records,
)


def test_atomic_specs_balance_operators_and_targets() -> None:
    specs = build_atomic_specs()
    operators = Counter(spec.operator for spec in specs)

    assert operators == {
        "gt": 10,
        "ge": 10,
        "lt": 10,
        "le": 10,
    }

    for comparison_operator in operators:
        targets = {
            spec.target_enabled
            for spec in specs
            if spec.operator == comparison_operator
        }
        assert targets == {True, False}


def test_atomic_records_cover_every_workflow_state() -> None:
    records = build_atomic_condition_records()
    scenarios = Counter(record["scenario"] for record in records)

    assert scenarios == {
        "atomic_initial": 40,
        "atomic_completed": 40,
        "atomic_unmatched": 40,
        "atomic_failure_sensor": 8,
        "atomic_failure_control": 8,
    }

    initial_records = [
        record
        for record in records
        if record["scenario"] == "atomic_initial"
    ]

    assert all(
        record["expected"]["tool_call"]["name"]
        == ATOMIC_TOOL
        for record in initial_records
    )


def test_v3_split_has_no_group_leakage() -> None:
    train_records, validation_records = split_records(
        build_records()
    )

    train_groups = {
        record["group_id"]
        for record in train_records
    }
    validation_groups = {
        record["group_id"]
        for record in validation_records
    }

    assert not train_groups & validation_groups
    assert len(train_records) == 381
    assert len(validation_records) == 115


def test_v3_observations_match_cpp_field_names() -> None:
    records = build_records()

    for record in records:
        prompt = record["messages"][0]["content"]
        assert '"indicator_on"' not in prompt
