from __future__ import annotations

import argparse
import json
import random
from pathlib import Path
from typing import Any

import torch
from peft import (
    LoraConfig,
    get_peft_model,
    prepare_model_for_kbit_training,
)
from torch.utils.data import Dataset
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    BitsAndBytesConfig,
    Trainer,
    TrainingArguments,
)


class PlannerSftDataset(Dataset):
    def __init__(
        self,
        path: Path,
        tokenizer,
        max_length: int,
    ) -> None:
        self.path = path
        self.tokenizer = tokenizer
        self.max_length = max_length
        self.samples: list[dict[str, list[int]]] = []

        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue

                record = json.loads(line)

                try:
                    sample = self._encode_record(record)
                except Exception as error:
                    raise ValueError(
                        f"{path}:{line_number}: {error}"
                    ) from error

                self.samples.append(sample)

        if not self.samples:
            raise ValueError(f"dataset is empty: {path}")

        lengths = [
            len(sample["input_ids"])
            for sample in self.samples
        ]

        print(
            f"[DATASET] {path}: "
            f"records={len(self.samples)} "
            f"min_tokens={min(lengths)} "
            f"max_tokens={max(lengths)}"
        )

    def _encode_record(
        self,
        record: dict[str, Any],
    ) -> dict[str, list[int]]:
        messages = record["messages"]

        if len(messages) < 2:
            raise ValueError(
                "messages must contain prompt and assistant"
            )

        if messages[-1].get("role") != "assistant":
            raise ValueError(
                "last message must have assistant role"
            )

        prompt_messages = messages[:-1]

        prompt_text = self.tokenizer.apply_chat_template(
            prompt_messages,
            tokenize=False,
            add_generation_prompt=True,
        )

        full_text = self.tokenizer.apply_chat_template(
            messages,
            tokenize=False,
            add_generation_prompt=False,
        )

        prompt_ids = self.tokenizer(
            prompt_text,
            add_special_tokens=False,
        )["input_ids"]

        full_ids = self.tokenizer(
            full_text,
            add_special_tokens=False,
        )["input_ids"]

        if len(full_ids) > self.max_length:
            raise ValueError(
                f"sample {record.get('id')} has "
                f"{len(full_ids)} tokens, exceeding "
                f"max_length={self.max_length}; "
                "do not silently truncate planner data"
            )

        if full_ids[: len(prompt_ids)] != prompt_ids:
            raise ValueError(
                "prompt tokens are not a prefix of full tokens"
            )

        if len(prompt_ids) >= len(full_ids):
            raise ValueError(
                "assistant response contains no trainable tokens"
            )

        # 只对assistant输出的JSON计算loss。
        # 提示词、工具定义、用户问题和Observation全部屏蔽。
        labels = (
            [-100] * len(prompt_ids)
            + full_ids[len(prompt_ids):]
        )

        attention_mask = [1] * len(full_ids)

        trainable_tokens = sum(
            label != -100
            for label in labels
        )

        if trainable_tokens == 0:
            raise ValueError(
                "sample contains no trainable assistant tokens"
            )

        return {
            "input_ids": full_ids,
            "attention_mask": attention_mask,
            "labels": labels,
        }

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(
        self,
        index: int,
    ) -> dict[str, list[int]]:
        return self.samples[index]


class PlannerDataCollator:
    def __init__(
        self,
        pad_token_id: int,
    ) -> None:
        self.pad_token_id = pad_token_id

    def __call__(
        self,
        features: list[dict[str, list[int]]],
    ) -> dict[str, torch.Tensor]:
        max_length = max(
            len(feature["input_ids"])
            for feature in features
        )

        input_ids = []
        attention_masks = []
        labels = []

        for feature in features:
            padding_length = (
                max_length - len(feature["input_ids"])
            )

            input_ids.append(
                feature["input_ids"]
                + [self.pad_token_id] * padding_length
            )

            attention_masks.append(
                feature["attention_mask"]
                + [0] * padding_length
            )

            labels.append(
                feature["labels"]
                + [-100] * padding_length
            )

        return {
            "input_ids": torch.tensor(
                input_ids,
                dtype=torch.long,
            ),
            "attention_mask": torch.tensor(
                attention_masks,
                dtype=torch.long,
            ),
            "labels": torch.tensor(
                labels,
                dtype=torch.long,
            ),
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="QLoRA fine-tuning for vehicle Agent planner"
    )

    parser.add_argument(
        "--model",
        default="Qwen/Qwen2.5-3B-Instruct",
    )

    parser.add_argument(
        "--train-data",
        type=Path,
        default=Path(
            "data/agent_planner/train.jsonl"
        ),
    )

    parser.add_argument(
        "--validation-data",
        type=Path,
        default=Path(
            "data/agent_planner/validation.jsonl"
        ),
    )

    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(
            "models/agent/qwen2.5-3b-planner-lora"
        ),
    )

    parser.add_argument(
        "--max-length",
        type=int,
        default=1280,
    )

    parser.add_argument(
        "--epochs",
        type=float,
        default=6.0,
    )

    parser.add_argument(
        "--learning-rate",
        type=float,
        default=2.0e-4,
    )

    parser.add_argument(
        "--batch-size",
        type=int,
        default=1,
    )

    parser.add_argument(
        "--gradient-accumulation-steps",
        type=int,
        default=8,
    )

    parser.add_argument(
        "--lora-rank",
        type=int,
        default=8,
    )

    parser.add_argument(
        "--lora-alpha",
        type=int,
        default=16,
    )

    parser.add_argument(
        "--lora-dropout",
        type=float,
        default=0.05,
    )

    parser.add_argument(
        "--max-steps",
        type=int,
        default=-1,
        help=(
            "Positive value overrides epochs; "
            "use 2 for a smoke test"
        ),
    )

    parser.add_argument(
        "--seed",
        type=int,
        default=3588,
    )

    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate tokenization without loading the model",
    )

    parser.add_argument(
        "--resume-from-checkpoint",
        default=None,
    )

    return parser.parse_args()


def set_seed(seed: int) -> None:
    random.seed(seed)
    torch.manual_seed(seed)

    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def main() -> None:
    args = parse_args()
    set_seed(args.seed)

    tokenizer = AutoTokenizer.from_pretrained(
        args.model,
        use_fast=True,
        trust_remote_code=False,
    )

    if tokenizer.pad_token_id is None:
        tokenizer.pad_token = tokenizer.eos_token

    # 训练采用右侧padding。
    tokenizer.padding_side = "right"

    train_dataset = PlannerSftDataset(
        args.train_data,
        tokenizer,
        args.max_length,
    )

    validation_dataset = PlannerSftDataset(
        args.validation_data,
        tokenizer,
        args.max_length,
    )

    if args.validate_only:
        print("[VALIDATE] tokenization completed")
        return

    if not torch.cuda.is_available():
        raise SystemExit(
            "CUDA is required for QLoRA training"
        )

    # BF16相比FP16整数范围更大，训练时通常不容易发生数值溢出
    use_bf16 = torch.cuda.is_bf16_supported()
    compute_dtype = (
        torch.bfloat16
        if use_bf16
        else torch.float16
    )

    print("[SYSTEM] GPU:", torch.cuda.get_device_name(0))
    print("[SYSTEM] BF16:", use_bf16)
    print("[SYSTEM] compute dtype:", compute_dtype)
    print("[SYSTEM] max length:", args.max_length)

    # 量化配置， QloRA训练阶段节省显存的量化配置
    quantization_config = BitsAndBytesConfig(
        load_in_4bit=True,                          # 权重以4bit加载
        bnb_4bit_quant_type="nf4",                  # 让权重密集的区域获得更细的表示能力。
        bnb_4bit_use_double_quant=True,             # 启用双重量化（量化abs_max）
        bnb_4bit_compute_dtype=compute_dtype,       # 用bf16参与计算
    )

    model = AutoModelForCausalLM.from_pretrained(
        args.model,
        quantization_config=quantization_config,
        torch_dtype=compute_dtype,
        device_map={"": 0},
        low_cpu_mem_usage=True,
        trust_remote_code=False,
    )

    # KV Cache主要用于自回归推理加速，训练时需要同时处理完整序列并反向传播
    # KV_Cache通常不能提供推理增益，还会增加显存占用，还会与梯度检查点冲突
    # 因此要关闭
    model.config.use_cache = False

    # 把一个已经以 4-bit/8-bit 加载的基础模型，处理成“适合 LoRA/QLoRA 训练”的状态。
    # 判断量化模型的量化方式，冻结基础模型
    # 对某些需要稳定计算的层保留较高精度（LayerNorm，RMSNorm，其他原本属于FP16/BF16的非量化参数，部分输出层或相关稳定参数）
    # 准备输入梯度（即Embedding）
    model = prepare_model_for_kbit_training(
        model,
        use_gradient_checkpointing=True,
    )

    lora_config = LoraConfig(
        r=args.lora_rank,                   # 低秩矩阵的中间维度
        lora_alpha=args.lora_alpha,         # 比例缩放：\(W'=W+\frac{\alpha}{r}BA\)
        lora_dropout=args.lora_dropout,     # 训练时随机丢弃 LoRA 分支约 5% 的输入，有助于减少过拟合。
        bias="none",                        # 不训练原线性层的 bias，只训练 LoRA 的矩阵参数，进一步减少参数数量。
        task_type="CAUSAL_LM",              # 明确告诉 PEFT：这是自回归因果语言模型任务。
        target_modules=[
            "q_proj",               # 生成query
            "k_proj",               # 生成key
            "v_proj",               # 生成value
            "o_proj",               # Attention输出投影
            "gate_proj",            # FFN门控投影
            "up_proj",              # FFN升维
            "down_proj",            # FFN降维
        ],
    )

    model = get_peft_model(
        model,
        lora_config,
    )

    model.print_trainable_parameters()

    collator = PlannerDataCollator(
        tokenizer.pad_token_id
    )

    args.output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    # 利用gradient_accumulation_steps 可以在显存有限的情况下模拟更大的batch
    training_args = TrainingArguments(
        output_dir=str(args.output_dir),
        num_train_epochs=args.epochs,
        max_steps=args.max_steps,
        per_device_train_batch_size=args.batch_size,        # 每次GPU实际处理的样本数
        per_device_eval_batch_size=1,
        gradient_accumulation_steps=(
            args.gradient_accumulation_steps                # 连续累计多少次梯度后更新一次系数
        ),
        learning_rate=args.learning_rate,                   # 学习率， 样本小，可以适当调大一些学习率，但也要小心过拟合
        lr_scheduler_type="cosine",                         # 增加到指定值后按余弦曲线逐步下降
        warmup_ratio=0.1,                                   # 按10%增加
        weight_decay=0.01,
        max_grad_norm=0.3,                                  # 梯度裁剪
        optim="paged_adamw_8bit",                           # 优化器
        fp16=not use_bf16,
        bf16=use_bf16,
        gradient_checkpointing=True,
        gradient_checkpointing_kwargs={
            "use_reentrant": False,
        },
        logging_strategy="steps",
        logging_steps=1,
        eval_strategy="epoch",                              # 在 validation 数据上计算 eval_loss
        save_strategy="epoch",                              # 每一轮保存一个checkpoint
        save_total_limit=2,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        greater_is_better=False,
        report_to="none",
        remove_unused_columns=False,
        dataloader_num_workers=0,
        group_by_length=True,
        seed=args.seed,
        data_seed=args.seed,
        ddp_find_unused_parameters=False,
    )

    trainer = Trainer(
        model=model,
        args=training_args,
        train_dataset=train_dataset,
        eval_dataset=validation_dataset,
        data_collator=collator,
    )

    print("[TRAIN] starting")

    train_result = trainer.train(
        resume_from_checkpoint=(
            args.resume_from_checkpoint
        )
    )

    print("[TRAIN] completed")
    print("[TRAIN] metrics:", train_result.metrics)

    final_adapter_dir = (
        args.output_dir / "final_adapter"
    )

    final_adapter_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    model.save_pretrained(
        final_adapter_dir,
        safe_serialization=True,
    )

    tokenizer.save_pretrained(
        final_adapter_dir
    )

    metrics = {
        key: float(value)
        if isinstance(value, (int, float))
        else value
        for key, value in train_result.metrics.items()
    }

    metrics["peak_gpu_memory_gib"] = (
        torch.cuda.max_memory_allocated()
        / 1024**3
    )

    metrics_path = (
        args.output_dir / "train_metrics.json"
    )

    metrics_path.write_text(
        json.dumps(
            metrics,
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    print("[OUTPUT] adapter:", final_adapter_dir)
    print("[OUTPUT] metrics:", metrics_path)
    print(
        "[MEMORY] peak:",
        f"{metrics['peak_gpu_memory_gib']:.2f} GiB",
    )


if __name__ == "__main__":
    main()
