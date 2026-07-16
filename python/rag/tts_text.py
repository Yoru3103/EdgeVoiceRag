import re
from typing import List

def normalize_tts_text(text: str) -> str:
    result = text.strip()

    # 必须在删除换行之前处理行首列表编号
    result = re.sub(
        r"(?m)^\s*\d+\s*[.、]\s*",
        "",
        result,
    )

    replacements = {
        "\r": "",
        "\n": "。",
        "：": "，",
        ":": "，",
        "“": "",
        "”": "",
        "\"": "",
        "‘": "",
        "’": "",
        "*": "",
        "#": "",
    }

    for source, target in replacements.items():
        result = result.replace(source, target)

    result = re.sub(r"\s+", "", result)
    result = re.sub(r"。+", "。", result)
    result = re.sub(r"，+", "，", result)

    # 保留句末句号
    return result.strip("， ")

def split_tts_text(
    text: str,
    max_chars: int = 60,
) -> List[str]:
    normalized_text = normalize_tts_text(text)
    
    if not normalized_text:
        return []
    
    sentences = re.split(
        r"(?<=[。！？!?；;])",
        normalized_text,
    )
    
    result = []
    
    for sentence in sentences:
        sentence = sentence.strip()
        
        if not sentence:
            continue
        
        result.extend(
            _split_long_sentence(
                sentence,
                max_chars=max_chars,
            )
        )
        
    return result

def _split_long_sentence(
    sentence: str,
    max_chars: int,
) -> List[str]:
    if len(sentence) <= max_chars:
        return [sentence]
    
    # 按照给定的标点符号分割句子并保留这些标点
    # ?<= 为正向后行断言，表示匹配一个位置，要求该位置前面是指定字符
    parts = re.split(
        r"(?<=[，,、])",
        sentence,
    )
    
    result = []
    current = ""
    
    for part in parts:
        part = part.strip()
        
        if not part:
            continue
        
        if len(current) + len(part) <= max_chars:
            current += part
            continue
        
        if current:
            result.append(current)
            current = ""
            
        while len(part) > max_chars:
            result.append(part[:max_chars])
            part = part[max_chars:]

        current = part
        
    if current:
        result.append(current)
        
    return result