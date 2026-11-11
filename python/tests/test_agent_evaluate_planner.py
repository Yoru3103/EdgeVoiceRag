from agent_finetune.evaluate_planner import compare_action, summarize_groups


def test_summarize_groups_reports_validity_and_accuracy() -> None:
    results = [
        {
            "scenario": "atomic_initial",
            "json_valid": True,
            "passed": True,
        },
        {
            "scenario": "atomic_initial",
            "json_valid": True,
            "passed": False,
        },
        {
            "scenario": "unsupported",
            "json_valid": False,
            "passed": False,
        },
    ]

    groups = summarize_groups(results, "scenario")

    assert groups["atomic_initial"] == {
        "total": 2,
        "json_valid_count": 2,
        "json_valid_rate": 1.0,
        "passed_count": 1,
        "accuracy": 0.5,
    }
    assert groups["unsupported"]["json_valid_rate"] == 0.0
    assert groups["unsupported"]["accuracy"] == 0.0


def test_compare_action_accepts_synonym_term_group() -> None:
    actual = {"type": "final_answer", "answer": "当前工具不支持这个操作。"}
    expected = {"type": "final_answer", "answer": "无法完成。"}

    passed, error = compare_action(
        actual,
        expected,
        [],
        [["无法", "不能", "不支持"]],
    )

    assert passed
    assert not error


def test_compare_action_rejects_missing_synonym_group() -> None:
    actual = {"type": "final_answer", "answer": "操作已经完成。"}
    expected = {"type": "final_answer", "answer": "无法完成。"}

    passed, error = compare_action(
        actual,
        expected,
        [],
        [["无法", "不能", "不支持"]],
    )

    assert not passed
    assert "groups" in error
