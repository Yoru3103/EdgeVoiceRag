# 在 PC 的 NVIDIA GPU 上，以 4-bit 方式加载一个 Hugging Face 格式的因果语言模型，
# 然后用 held-out 测试集检查它能不能稳定输出符合 Agent 协议的 JSON。
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any
from peft import PeftModel

import torch
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    BitsAndBytesConfig,
)

from agent_finetune.planner_format import parse_strict_action


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue

            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: {error}"
                ) from error

            records.append(record)

    return records

def load_model(
    model_name: str,
    adapter_path: Path | None = None,
):
    quantization_config = BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_use_double_quant=True,
        bnb_4bit_compute_dtype=torch.bfloat16,
    )
    
    tokenizer = AutoTokenizer.from_pretrained(
        model_name,
        use_fast=True,
        trust_remote_code=False,
    )
    
    if tokenizer.pad_token_id is None:
        tokenizer.pad_token_id = tokenizer.eos_token
        
    model = AutoModelForCausalLM.from_pretrained(
        model_name,
        quantization_config=quantization_config,
        torch_dtype=torch.bfloat16,
        device_map={"":0},
        low_cpu_mem_usage=True,
        trust_remote_code=False,
    )
    
    if adapter_path is not None:
        if not adapter_path.exists():
            raise FileNotFoundError(
                f"LoRA adapter does not exist: {adapter_path}"
            )
            
        print("[ADAPTER]", adapter_path)
        
        model = PeftModel.from_pretrained(
            model,
            str(adapter_path),
            is_trainable=False,
        )
        
    model.eval()
    
    model.generation_config.do_sample = False
    model.generation_config.temperature = None
    model.generation_config.top_p = None
    model.generation_config.top_k = None
    
    return tokenizer, model


def generate(
    tokenizer,
    model,
    prompt: str,
    max_new_tokens: int,
) -> tuple[str, float]:
    messages = [
        {
            "role": "user",
            "content": prompt,
        }
    ]

    # 生成类似如下格式
    # <|im_start|>user
    # 这里是完整的 Agent Planner Prompt
    # <|im_end|>
    # <|im_start|>assistant
    formatted = tokenizer.apply_chat_template(
        messages,
        tokenize=False,
        add_generation_prompt=True,
    )

    inputs = tokenizer(
        formatted,
        return_tensors="pt",
        add_special_tokens=False,
    )

    inputs = {
        key: value.to(model.device)
        for key, value in inputs.items()
    }

    input_length = inputs["input_ids"].shape[1]

    torch.cuda.synchronize()
    start = time.perf_counter()

    # 禁止梯度计算，不保存反向传播中间状态，减少模型占用，提高推理速度
    with torch.inference_mode():
        output_ids = model.generate(
            **inputs,
            max_new_tokens=max_new_tokens,
            do_sample=False,
            repetition_penalty=1.05,
            eos_token_id=tokenizer.eos_token_id,
            pad_token_id=tokenizer.pad_token_id,
            use_cache=True,
        )

    torch.cuda.synchronize()
    elapsed = time.perf_counter() - start

    generated_ids = output_ids[0, input_length:]

    text = tokenizer.decode(
        generated_ids,
        skip_special_tokens=True,
    ).strip()

    return text, elapsed


def compare_action(
    actual: dict[str, Any],
    expected: dict[str, Any],
    required_answer_terms: list[str],
) -> tuple[bool, str]:
    if actual.get("type") != expected.get("type"):
        return (
            False,
            f"expected type={expected.get('type')}, "
            f"received type={actual.get('type')}",
        )

    if expected["type"] == "tool_call":
        actual_call = actual["tool_call"]
        expected_call = expected["tool_call"]

        if actual_call["name"] != expected_call["name"]:
            return (
                False,
                f"expected tool={expected_call['name']}, "
                f"received tool={actual_call['name']}",
            )

        if actual_call["arguments"] != expected_call["arguments"]:
            return (
                False,
                f"expected arguments={expected_call['arguments']}, "
                f"received arguments={actual_call['arguments']}",
            )

        # call id允许模型自由生成，不作为准确率指标。
        return True, ""

    answer = actual["answer"]

    missing = [
        term
        for term in required_answer_terms
        if term not in answer
    ]

    if missing:
        return (
            False,
            f"final answer is missing terms: {missing}",
        )

    return True, ""


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate Agent planner structured output"
    )

    parser.add_argument(
        "--model",
        default="Qwen/Qwen2.5-3B-Instruct",
    )

    parser.add_argument(
        "--dataset",
        type=Path,
        default=Path("data/agent_planner/heldout.jsonl"),
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "perf_logs/agent_planner_heldout_result.json"
        ),
    )

    parser.add_argument(
        "--max-new-tokens",
        type=int,
        default=192,
    )

    parser.add_argument(
        "--limit",
        type=int,
        default=0,
    )
    
    parser.add_argument(
        "--adapter",
        type=Path,
        default=None,
        help="Optional PEFT LoRA adapter directory",
    )

    args = parser.parse_args()

    if not torch.cuda.is_available():
        raise SystemExit("CUDA is not available")

    records = load_jsonl(args.dataset)

    if args.limit > 0:
        records = records[: args.limit]

    tokenizer, model = load_model(args.model, args.adapter)

    total = len(records)
    json_valid_count = 0
    passed_count = 0
    elapsed_total = 0.0
    results: list[dict[str, Any]] = []

    for index, record in enumerate(records, 1):
        prompt = record["messages"][0]["content"]
        expected = record["expected"]
        required_terms = record.get(
            "required_answer_terms",
            [],
        )

        raw_output, elapsed = generate(
            tokenizer,
            model,
            prompt,
            args.max_new_tokens,
        )

        elapsed_total += elapsed
        parsed = parse_strict_action(raw_output)

        passed = False
        error = ""

        if parsed.ok and parsed.value is not None:
            json_valid_count += 1
            passed, error = compare_action(
                parsed.value,
                expected,
                required_terms,
            )
        else:
            error = parsed.error

        if passed:
            passed_count += 1

        print(
            f"[{index:03d}/{total:03d}] "
            f"id={record['id']} "
            f"valid={parsed.ok} "
            f"passed={passed} "
            f"elapsed={elapsed:.2f}s"
        )

        if not passed:
            print("  expected:", json.dumps(
                expected,
                ensure_ascii=False,
            ))
            print("  output:", raw_output)
            print("  error:", error)

        results.append(
            {
                "id": record["id"],
                "passed": passed,
                "json_valid": parsed.ok,
                "elapsed_seconds": elapsed,
                "expected": expected,
                "raw_output": raw_output,
                "error": error,
            }
        )

    summary = {
        "model": args.model,
        "dataset": str(args.dataset),
        "total": total,
        "json_valid_count": json_valid_count,
        "json_valid_rate": (
            json_valid_count / total if total else 0.0
        ),
        "passed_count": passed_count,
        "accuracy": passed_count / total if total else 0.0,
        "average_elapsed_seconds": (
            elapsed_total / total if total else 0.0
        ),
        "peak_gpu_memory_gib": (
            torch.cuda.max_memory_allocated() / 1024**3
        ),
        "results": results,
        "adapter": (
            str(args.adapter)
            if args.adapter is not None
            else None
        ),
    }

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    args.output.write_text(
        json.dumps(
            summary,
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    print()
    print("=" * 72)
    print(
        "[SUMMARY]",
        f"json_valid={json_valid_count}/{total}",
        f"json_rate={summary['json_valid_rate']:.2%}",
        f"passed={passed_count}/{total}",
        f"accuracy={summary['accuracy']:.2%}",
        f"average={summary['average_elapsed_seconds']:.2f}s",
        f"peak_vram={summary['peak_gpu_memory_gib']:.2f}GiB",
    )
    print("[REPORT]", args.output)


if __name__ == "__main__":
    main()
