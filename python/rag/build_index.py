# 读取docs/vehicle_manual.txt，生成 chunks.json

import argparse
import json
from pathlib import Path
from typing import List, Dict

def load_manual_lines(manual_path: Path) -> List[str]:
    if not manual_path.exists():
        raise FileNotFoundError(f"manual file not found: {manual_path}")    # raise 主动抛出异常
    
    lines: List[str] = []
    
    with manual_path.open("r", encoding="utf-8") as file:
        for line in file:
            line = line.strip() # 去除首尾空格
            if not line:
                continue
            lines.append(line)
            
    return lines

def build_chunks(lines: List[str]) -> List[Dict]:
    chunks: List[Dict] = []
    
    for idx, line in enumerate(lines):
        title = line
        content = line
        
        if ":" in line:
            title, content = line.split(":", 1)    # 按字符串: 分割，最多分割1次
            title = title.strip()
            content = content.strip()
            
        chunk = {
            "chunk_id": idx,
            "title": title,
            "content": content,
            "text": line
        }
        
        chunks.append(chunk)
        
    return chunks

def save_chunks(chunks: List[Dict], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)   # parents参数表示如果父目录不存在也一并创建，exist_ok表示如果目录已经存在，不要报错
    
    with output_path.open("w", encoding="utf-8") as file:
        json.dump(chunks, file, ensure_ascii=False, indent=2)   # 把python对象写成json文件，ensure_ascii=False表示不要把中文转义为\uXXXX；indent=2 表示格式化缩进2个空格

def main() -> None:
    # argparse 是 Python 标准库，专门用来处理命令行参数。 description 是脚本说明
    parser = argparse.ArgumentParser(description="Build chunk index from vehicle manual.")
    # 表示脚本支持一个命令行参数--manual
    parser.add_argument(
        "--manual",
        default="docs/vehicle_manual.txt",
        help="Path to vehicle manual text file.",
    )
    parser.add_argument(
        "--output",
        default="vector_db/chunks.json",
        help="Output chunk JSON path.",
    )
    
    # 读取并解析命令行参数
    args = parser.parse_args()
    
    manual_path = Path(args.manual)
    output_path = Path(args.output)
    
    lines = load_manual_lines(manual_path)
    chunks = build_chunks(lines)
    save_chunks(chunks, output_path)
    
    print(f"Loaded manual lines: {len(lines)}")
    print(f"Built chunks: {len(chunks)}")
    print(f"Saved to: {output_path}")
    
if __name__ == "__main__":
    main()