from dataclasses import dataclass
from typing import List

import requests

@dataclass
class GenerationResult:
    answer: str
    prompt: str
    backend: str
    
class MockLLMGenerator:
    def __init__(self) -> None:
        self.backend = "mock_llm"
        
    def generate(self, query: str, contexts: List[str]) -> GenerationResult:
        prompt = build_rag_prompt(query, contexts)
        answer = self._mock_generate_answer(query, contexts)
        
        return GenerationResult(
            answer=answer,
            prompt=prompt,
            backend=self.backend,
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
    
class OllamaGenerator:
    def __init__(
        self,
        model: str = "qwen2.5:3b",
        base_url: str = "http://localhost:11434",
        timeout_seconds: int= 60) -> None:
        self.model = model
        self.base_url = base_url
        self.timeout_seconds = timeout_seconds
        self.backend = f"ollama{self.model}"
        
    def generate(self, query: str, contexts: List[str]) -> GenerationResult:
        prompt = build_rag_prompt(query, contexts)
        
        if not contexts:
            return GenerationResult(
                answer="车辆手册中没有找到相关内容。",
                prompt=prompt,
                backend=self.backend,
            )
            
        url = f"{self.base_url}/api/generate"
        
        payload = {
            "model": self.model,
            "prompt": prompt,
            "stream": False,
            "options": {
                "temperature": 0.2
            }
        }
        
        try:
            response = requests.post(
                url,
                json=payload,
                timeout=self.timeout_seconds,
            )
            response.raise_for_status()
            
            data = response.json()
            answer = str(data.get("response", "")).strip()
            
            if not answer:
                answer = "模型没有生成有效回答"
                
            return GenerationResult(
                answer=answer,
                prompt=prompt,
                backend=self.backend,
            )
            
        except requests.RequestException as exc:
            return GenerationResult(
                answer=f"Ollama 调用失败: {exc}",
                prompt=prompt,
                backend=self.backend,
            )
    
def build_rag_prompt(query: str, contexts: List[str]) -> str:
    context_text = "\n".join(
        f"{idx + 1}. {context}"
        for idx, context in enumerate(contexts)
    )
    
    return (
        "你是一个车载语音助手。请只根据给定车辆手册内容回答用户问题。\n"
        "要求：\n"
        "1. 不要编造车辆手册之外的信息。\n"
        "2. 如果车辆手册中没有相关信息，请回答“车辆手册中没有找到相关内容”。\n"
        "3. 回答要简洁，适合语音播报。\n\n"
        f"用户问题：{query}\n\n"
        f"车辆手册内容：\n{context_text}\n\n"
        "请给出最终回答："
    )

def create_llm_generator(
    backend: str,
    model: str = "qwen2.5:3b",
    base_url: str = "http://localhost:11434",
    ):
    if backend == "mock":
        return MockLLMGenerator()
    
    if backend == "ollama":
        return OllamaGenerator(
            model=model,
            base_url=base_url,
        )
    
    raise ValueError(f"Unsupported LLM backend: {backend}")