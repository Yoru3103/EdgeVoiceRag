import argparse
from pathlib import Path
from typing import List

import jieba

USER_DICT_PATH = Path("python/rag/user_dict.txt")

def tokenize(text: str) -> List[str]:
    if USER_DICT_PATH.exists():
        jieba.load_userdict(str(USER_DICT_PATH))
        
    return [
        token.strip()
        for token in jieba.cut(text)
        if token.strip()
    ]
    
def main() -> None:
    parser = argparse.ArgumentParser(description="Debug jieba tokenization.")
    parser.add_argument("--text", required=True, help="Text to tokenize.")
    args = parser.parse_args()
    
    tokens = tokenize(args.text)
    
    print("Input:")
    print(args.text)
    print("\nTokens:")
    print(tokens)
    
if __name__ == "__main__":
    main()