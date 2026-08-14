from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from agent_finetune.planner_format import canonical_json, parse_strict_action


def validate_record(record: Any, source: Path, line_number: int) -> list[str]:
    prefix = f"{source}:{line_number}"
    errors: list[str] = []
    if not isinstance(record, dict):
        return [f"{prefix}: record must be an object"]
    if not isinstance(record.get("id"), str) or not record["id"]:
        errors.append(f"{prefix}: id must be non-empty")
    messages = record.get("messages")
    if not isinstance(messages, list) or len(messages) != 2:
        return errors + [f"{prefix}: messages must contain user and assistant"]
    if messages[0].get("role") != "user" or not messages[0].get("content"):
        errors.append(f"{prefix}: first message must be a non-empty user prompt")
    if messages[1].get("role") != "assistant":
        errors.append(f"{prefix}: second message must be assistant")

    parsed = parse_strict_action(messages[1].get("content", ""))
    if not parsed.ok:
        errors.append(f"{prefix}: invalid assistant action: {parsed.error}")
    elif canonical_json(parsed.value) != canonical_json(record.get("expected")):
        errors.append(f"{prefix}: assistant content differs from expected")
    return errors


def validate_file(path: Path) -> tuple[int, list[str]]:
    errors: list[str] = []
    count = 0
    ids: set[str] = set()
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            count += 1
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                errors.append(f"{path}:{line_number}: invalid record JSON: {error.msg}")
                continue
            errors.extend(validate_record(record, path, line_number))
            record_id = record.get("id") if isinstance(record, dict) else None
            if isinstance(record_id, str):
                if record_id in ids:
                    errors.append(f"{path}:{line_number}: duplicate id {record_id}")
                ids.add(record_id)
    return count, errors


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate vehicle Agent JSONL data")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    total = 0
    all_errors: list[str] = []
    for path in args.paths:
        count, errors = validate_file(path)
        total += count
        all_errors.extend(errors)
        print(f"{path}: records={count} errors={len(errors)}")
    if all_errors:
        for error in all_errors:
            print(error)
        raise SystemExit(1)
    print(f"validated {total} records")


if __name__ == "__main__":
    main()

