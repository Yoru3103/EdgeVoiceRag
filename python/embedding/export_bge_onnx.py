from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict

import numpy as np
import onnx
import onnxruntime as ort
import torch
import torch.nn.functional as functional
from torch import Tensor, nn
from transformers import AutoModel, AutoTokenizer
# 基于BERT，配置为4层transformer，会把一句中文转换成一个512维浮点向量
DEFAULT_MODEL_ID = "BAAI/bge-small-zh-v1.5"
DEFAULT_OUTPUT_DIR = Path("models/embedding/bge-small-zh-v1.5")
DEFAULT_MAX_LENGTH = 512

class BgeEmbeddingModel(nn.Module):
    """
    将Hugging Face BGE模型包装成板端真正需要的形式。

    输入：
        input_ids:      [batch, sequence]
        attention_mask: [batch, sequence]
        token_type_ids: [batch, sequence]

    输出：
        embeddings: [batch, 512]

    ONNX内部直接完成：
        Transformer -> CLS pooling -> L2 normalize
    """

    def __init__(self, transformer: nn.Module) -> None:
        super().__init__()  # 初始化父类nn.Module，保存原始Hugging Face Transformer模型
        self.transformer = transformer      # 将传入的BGE/BERT模型注册为当前包装模型的子模块

    # 模型推理流程
    def forward(
        self,
        input_ids: Tensor,          # 每个Token的对应ID
        attention_mask: Tensor,     # 表示哪些位置是真是token，哪些位置是补全Padding
        token_type_ids: Tensor,     # 用于标记token属于哪个句子
    ) -> Tensor:
        # 调用transformer
        outputs = self.transformer(
            input_ids=input_ids,
            attention_mask=attention_mask,
            token_type_ids=token_type_ids,
            return_dict=False,              # 让模型返回元组，而不是带字段名的对象
        )

        # 最后一层所有token的隐藏状态，形状[batch, sequence, 512]
        last_hidden_state = outputs[0]

        # BGE官方Transformers示例采用CLS pooling。
        # 提取CLS向量，得到[batch, 512]
        cls_embedding = last_hidden_state[:, 0, :]

        # 归一化后，向量点积就等于余弦相似度。
        normalized_embedding = functional.normalize(
            cls_embedding,
            p=2,        # 使用L2范数
            dim=1,      # 对每一行的512个数进行归一化
        )

        return normalized_embedding

# 构造一组用于导出ONNX的示例输入
def build_example_inputs(
    tokenizer,
    max_length: int,
) -> Dict[str, Tensor]:
    """
    使用两个不同内容的句子作为导出样例。

    batch=2有利于导出真正的动态batch维度。
    """

    encoded = tokenizer(
        [
            "空调怎么打开",
            "手机如何与车辆进行配对",
        ],
        padding=True,               # 句子长度不同，需要添加padding
        truncation=True,            # 超过最大长度时自动截断
        max_length=max_length,
        return_tensors="pt",        # 让tokenizer返回PyTorch Tensor
    )

    # 取出三个输入
    input_ids = encoded["input_ids"]
    attention_mask = encoded["attention_mask"]

    token_type_ids = encoded.get(
        "token_type_ids",
        torch.zeros_like(input_ids),    # 如果tokenizer没有返回token_type_ids，就创建一个input_ids形状的全零Tensor
    )

    return {
        "input_ids": input_ids,
        "attention_mask": attention_mask,
        "token_type_ids": token_type_ids,
    }

def validate_onnx_model(
    wrapper: BgeEmbeddingModel,
    tokenizer,
    onnx_path: Path,
    max_length: int,
) -> None:
    """
    比较PyTorch和ONNX Runtime输出。

    导出成功不代表数值一定正确，因此必须做一次交叉验证。
    """

    validation_texts = [
        "空调怎么打开",
        "车里太热了怎么办",
        "尾门如何开启",
    ]

    encoded = tokenizer(
        validation_texts,
        padding=True,
        truncation=True,
        max_length=max_length,
        return_tensors="pt",
    )

    input_ids = encoded["input_ids"]
    attention_mask = encoded["attention_mask"]
    token_type_ids = encoded.get(
        "token_type_ids",
        torch.zeros_like(input_ids),
    )

    # 只推理，不训练
    # 关闭梯度记录，减少内存，提高推理速度，避免不必要的反向传播信息
    with torch.inference_mode():
        pytorch_embeddings = wrapper(
            input_ids,
            attention_mask,
            token_type_ids,
        ).cpu().numpy()     # 移到cpu，转换为NumPy数组

    # 创建onnx runtime推理会话
    session = ort.InferenceSession(
        str(onnx_path),
        providers=["CPUExecutionProvider"],     # 使用cpu
    )

    onnx_embeddings = session.run(
        ["embeddings"],     # 指定获取名为 embeddings 的模型输出。
        {
            "input_ids": input_ids.cpu().numpy().astype(np.int64),
            "attention_mask": attention_mask.cpu().numpy().astype(np.int64),
            "token_type_ids": token_type_ids.cpu().numpy().astype(np.int64),
        },
    )[0]

    if onnx_embeddings.shape != pytorch_embeddings.shape:
        raise RuntimeError(
            "ONNX output shape mismatch: "
            f"onnx={onnx_embeddings.shape}, "
            f"pytorch={pytorch_embeddings.shape}"
        )

    # 计算每个元素的绝对误差，然后找最大值
    maximum_error = float(
        np.max(np.abs(onnx_embeddings - pytorch_embeddings))
    )

    # 归一化
    norms = np.linalg.norm(onnx_embeddings, axis=1)

    if maximum_error > 1.0e-4:
        raise RuntimeError(
            "ONNX output differs from PyTorch: "
            f"maximum_error={maximum_error}"
        )

    # 检查归一化
    if not np.allclose(
        norms,
        np.ones_like(norms),
        atol=1.0e-4,
    ):
        raise RuntimeError(
            f"ONNX embeddings are not normalized: {norms}"
        )

    print(
        "ONNX validation passed: "
        f"shape={onnx_embeddings.shape}, "
        f"maximum_error={maximum_error:.8f}"
    )

def export_model(
    model_id: str,
    output_dir: Path,
    max_length: int,
) -> None:
    if max_length <= 0:
        raise ValueError("max_length must be positive")

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    print(f"Loading tokenizer: {model_id}")

    # 下载并加载Tokenizer
    tokenizer = AutoTokenizer.from_pretrained(
        model_id,
        use_fast=True,
    )

    if not tokenizer.is_fast:
        raise RuntimeError(
            "A fast tokenizer is required because "
            "the C++ runtime will use tokenizer.json"
        )

    print(f"Loading model: {model_id}")

    # 下载并加载预训练BGE模型（没有训练和微调，直接使用官方参数）
    # ONNX legacy exporter relies on TorchScript tracing.  Force the
    # explicit attention implementation because the Transformers 5.x
    # SDPA mask helper uses scalar shape logic that cannot be traced
    # correctly by this exporter.
    transformer = AutoModel.from_pretrained(
        model_id,
        attn_implementation="eager",
    )

    transformer.eval()  # 关闭Dropout等训练行为
    transformer.cpu()   # 使用CPU完成导出

    # 将原始BGE模型包装为上述BGE+CLS pooling + normalize
    wrapper = BgeEmbeddingModel(transformer)

    wrapper.eval()
    wrapper.cpu()

    example_inputs = build_example_inputs(
        tokenizer,
        max_length,
    )

    onnx_path = output_dir / "model.onnx"

    print(f"Exporting ONNX model: {onnx_path}")

    # 使用 legacy TorchScript ONNX exporter。
    # PyTorch 2.6 的 Dynamo exporter 会为 BERT 生成
    # sequence != max_length 的 guard，与我们声明的
    # 2 <= sequence <= max_length 冲突。
    # dynamic_axes 仍然保留动态 batch 和序列长度。
    dynamic_axes = {
        "input_ids": {
            0: "batch",
            1: "sequence",
        },
        "attention_mask": {
            0: "batch",
            1: "sequence",
        },
        "token_type_ids": {
            0: "batch",
            1: "sequence",
        },
        "embeddings": {
            0: "batch",
        },
    }

    torch.onnx.export(
        wrapper,
        (
            example_inputs["input_ids"],
            example_inputs["attention_mask"],
            example_inputs["token_type_ids"],
        ),
        str(onnx_path),
        input_names=[
            "input_ids",
            "attention_mask",
            "token_type_ids",
        ],
        output_names=["embeddings"],
        opset_version=17,
        dynamo=False,
        dynamic_axes=dynamic_axes,
    )

    # 保存tokenizer.json、vocab.txt等分词资源。
    tokenizer.save_pretrained(
        output_dir
    )

    transformer.config.to_json_file(
        output_dir / "hf_model_config.json"
    )

    model_proto = onnx.load(
        str(onnx_path)
    )

    onnx.checker.check_model(
        model_proto
    )

    validate_onnx_model(
        wrapper,
        tokenizer,
        onnx_path,
        max_length,
    )

    print("BGE ONNX export completed.")
    print(f"Model: {onnx_path}")
    print(
        f"Tokenizer: {output_dir / 'tokenizer.json'}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Export BGE embedding model to ONNX "
            "with CLS pooling and L2 normalization."
        )
    )

    parser.add_argument(
        "--model-id",
        default=DEFAULT_MODEL_ID,
    )

    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
    )

    parser.add_argument(
        "--max-length",
        type=int,
        default=DEFAULT_MAX_LENGTH,
    )

    arguments = parser.parse_args()

    export_model(
        model_id=arguments.model_id,
        output_dir=arguments.output_dir,
        max_length=arguments.max_length,
    )


if __name__ == "__main__":
    main()
