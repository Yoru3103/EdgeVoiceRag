from dataclasses import dataclass
from typing import List

@dataclass
class GenerationResult:
    answer: str
    prompt: str
    backend: str
    
class MockLLMGenerator:
    def __init__(self) -> None:
        self.backend = "mock_llm"
        
    def generate(self, query: str, contexts: List[str]) -> GenerationResult:
        prompt = self._build_prompt(query, contexts)
        answer = self._mock_generate_answer(query, contexts)
        
        return GenerationResult(
            answer=answer,
            prompt=prompt,
            backend=self.backend,
        )
        
    @staticmethod
    def _build_prompt(query: str, contexts: List[str]) -> str:
        context_text = "\n".join(
            f"{idx + 1}. {context}"
            for idx, context in enumerate(contexts)
        )
        
        return (
            "你是一个车载语音助手，请只根据给定车辆手册内容回答用户问题。\n"
            "如果车辆手册中没有相关信息，请回答“车辆手册中没有找到相关内容”。\n\n"
            f"用户问题：{query}\n\n"
            f"车辆手册内容：\n{context_text}\n\n"
            "请给出简洁、精确的回答："
        )
        
    @staticmethod
    def _mock_generate_answer(query: str, contexts: List[str]) -> str:
        if not contexts:
            return "车辆手册没有找到相关内容。"
        
        lines = ["根据车辆手册："]
        
        for idx, context in enumerate(contexts, start=1):
            lines.append(f"{idx}. {context}")
        # 分隔符.join(可迭代对象) lines每个成员之间用分隔符连接
        return "\n".join(lines)