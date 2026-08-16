from __future__ import annotations

import argparse
import gc
import json
import time

import torch
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    BitsAndBytesConfig,
)

from agent_finetune.planner_format import (
    build_runtime_prompt,
    parse_strict_action,
)


TEST_CASES = [
    {
        "name": "environment",
        "query": "车内的温度是多少？",
        "expected_type": "tool_call",
        "expected_tool": "get_cabin_environment",
    },
    {
        "name": "air_conditioner_state",
        "query": "空调现在开着吗？",
        "expected_type": "tool_call",
        "expected_tool": "get_air_conditioner_state",
    },
    {
        "name": "open_air_conditioner",
        "query": "请打开空调。",
        "expected_type": "tool_call",
        "expected_tool": "set_air_conditioner",
    },
    {
        "name": "close_air_conditioner",
        "query": "请关闭空调。",
        "expected_type": "tool_call",
        "expected_tool": "set_air_conditioner",
    },
]


def gibibytes(value: int) -> float:
    return value / 1024**3


def print_gpu_memory(label: str) -> None:
    allocated = torch.cuda.memory_allocated()
    reserved = torch.cuda.memory_reserved()
    maximum = torch.cuda.max_memory_allocated()

    print(
        f"[MEMORY] {label}: "
        f"allocated={gibibytes(allocated):.2f} GiB, "
        f"reserved={gibibytes(reserved):.2f} GiB, "
        f"peak={gibibytes(maximum):.2f} GiB"
    )


def load_model(model_name: str):
    quantization_config = BitsAndBytesConfig(
        load_in_4bit=True,
        bnb_4bit_quant_type="nf4",
        bnb_4bit_use_double_quant=True,
        bnb_4bit_compute_dtype=torch.float16,
    )

    tokenizer = AutoTokenizer.from_pretrained(
        model_name,
        use_fast=True,
        trust_remote_code=False,
    )

    if tokenizer.pad_token_id is None:
        tokenizer.pad_token = tokenizer.eos_token

    model = AutoModelForCausalLM.from_pretrained(
        model_name,
        quantization_config=quantization_config,
        torch_dtype=torch.float16,
        device_map={"": 0},
        low_cpu_mem_usage=True,
        trust_remote_code=False,
    )

    model.eval()
    return tokenizer, model


def generate_action(
    tokenizer,
    model,
    query: str,
    max_new_tokens: int,
) -> tuple[str, float]:
    runtime_prompt = build_runtime_prompt(
        user_input=query,
        observations=[],
    )

    messages = [
        {
            "role": "user",
            "content": runtime_prompt,
        }
    ]

    formatted_prompt = tokenizer.apply_chat_template(
        messages,
        tokenize=False,
        add_generation_prompt=True,
    )

    inputs = tokenizer(
        formatted_prompt,
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

    with torch.inference_mode():
        generated = model.generate(
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

    output_tokens = generated[0, input_length:]

    output = tokenizer.decode(
        output_tokens,
        skip_special_tokens=True,
    ).strip()

    return output, elapsed


def evaluate_output(
    output: str,
    expected_type: str,
    expected_tool: str,
) -> tuple[bool, str]:
    parsed = parse_strict_action(output)

    if not parsed.ok:
        return False, parsed.error

    action = parsed.value
    if action is None:
        return False, "parsed action is empty"

    if action.get("type") != expected_type:
        return (
            False,
            f"expected type={expected_type}, "
            f"received type={action.get('type')}",
        )

    if expected_type == "tool_call":
        actual_tool = action["tool_call"]["name"]

        if actual_tool != expected_tool:
            return (
                False,
                f"expected tool={expected_tool}, "
                f"received tool={actual_tool}",
            )

    return True, ""


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Check Qwen2.5-3B Agent planner baseline"
    )

    parser.add_argument(
        "--model",
        default="Qwen/Qwen2.5-3B-Instruct",
    )

    parser.add_argument(
        "--max-new-tokens",
        type=int,
        default=192,
    )

    args = parser.parse_args()

    if not torch.cuda.is_available():
        raise SystemExit("CUDA is not available")

    print("[SYSTEM] GPU:", torch.cuda.get_device_name(0))
    print(
        "[SYSTEM] total VRAM:",
        f"{gibibytes(torch.cuda.get_device_properties(0).total_memory):.2f} GiB",
    )
    print("[MODEL]", args.model)

    torch.cuda.empty_cache()
    torch.cuda.reset_peak_memory_stats()

    tokenizer, model = load_model(args.model)

    print_gpu_memory("after model load")

    passed = 0
    results = []

    for case in TEST_CASES:
        print()
        print("=" * 72)
        print("[CASE]", case["name"])
        print("[QUERY]", case["query"])

        output, elapsed = generate_action(
            tokenizer=tokenizer,
            model=model,
            query=case["query"],
            max_new_tokens=args.max_new_tokens,
        )

        ok, error = evaluate_output(
            output=output,
            expected_type=case["expected_type"],
            expected_tool=case["expected_tool"],
        )

        if ok:
            passed += 1

        print("[RAW_OUTPUT]")
        print(output)
        print("[VALID]", ok)

        if error:
            print("[ERROR]", error)

        print("[ELAPSED]", f"{elapsed:.3f} seconds")
        print_gpu_memory(f"after {case['name']}")

        results.append(
            {
                "name": case["name"],
                "query": case["query"],
                "ok": ok,
                "error": error,
                "elapsed_seconds": elapsed,
                "raw_output": output,
            }
        )

    print()
    print("=" * 72)
    print(
        "[SUMMARY]",
        f"passed={passed}/{len(TEST_CASES)}",
        f"success_rate={passed / len(TEST_CASES):.2%}",
    )

    print("[RESULT_JSON]")
    print(
        json.dumps(
            {
                "model": args.model,
                "passed": passed,
                "total": len(TEST_CASES),
                "success_rate": passed / len(TEST_CASES),
                "results": results,
            },
            ensure_ascii=False,
            indent=2,
        )
    )

    del model
    del tokenizer

    gc.collect()
    torch.cuda.empty_cache()


if __name__ == "__main__":
    main()