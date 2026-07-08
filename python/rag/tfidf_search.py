# TF:Term Frequency，词频某个词在当前文本里出现越多，权重越高
# IDF：Inverse Document Frequency，逆文档频率:某个词在所有文档里越常见，权重越低；越少见，越重要。
# 某个词在当前段落常出现，但在其他段落不常出现 => 它很重要

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import List

import jieba
import numpy as np
from sklearn.feature_extraction.text import TfidfVectorizer # 用来把文本转换成数字向量
from sklearn.metrics.pairwise import cosine_similarity

USER_DICT_PATH = Path("python/rag/user_dict.txt")

# 用dataclass加工一下这个类，为其自动生成构造函数等
@dataclass
class SearchResult:
    chunk_id: int
    title: str
    content: str
    text: str
    score: float


class TfidfRagSearcher:
    def __init__(self, chunks_path: Path) -> None:
        self.chunks_path = chunks_path
        
        if USER_DICT_PATH.exists():
            jieba.load_userdict(str(USER_DICT_PATH))
                    
        self.chunks = self._load_chunks(chunks_path)

        if not self.chunks:
            raise ValueError(f"No chunks loaded from {chunks_path}")

        self.texts = [chunk["text"] for chunk in self.chunks]

        self.vectorizer = TfidfVectorizer(
            tokenizer=self._tokenize,   # 分词函数
            token_pattern=None,     # 关闭默认匹配规则
        )

        # fit：从 self.texts 里学习词表和 TF-IDF 权重规则
        # transform：把所有文本转换成向量矩阵
        self.doc_matrix = self.vectorizer.fit_transform(self.texts) 

    def search(self, query: str, top_k: int = 3) -> List[SearchResult]:
        query = query.strip()

        if not query:
            return []

        if top_k <= 0:
            return []

        # 把查询文本转成向量 transform处理列表 需要转换成列表，列表值为TF*IDF
        query_vector = self.vectorizer.transform([query])

        # 计算 query 和每个文档 chunk 的余弦相似度。.flatten() 把结果压平成一维数组，方便后面排序。
        scores = cosine_similarity(query_vector, self.doc_matrix).flatten()

        if scores.size == 0:
            return []
        
        # [::-1]表示反转，从大到小；[:top_k]取前top_k个，此处存大小索引
        top_indices = np.argsort(scores)[::-1][:top_k]

        results: List[SearchResult] = []

        for idx in top_indices:
            score = float(scores[idx])

            if score <= 0:
                continue

            chunk = self.chunks[int(idx)]

            results.append(
                SearchResult(
                    chunk_id=int(chunk["chunk_id"]),
                    title=str(chunk["title"]),
                    content=str(chunk["content"]),
                    text=str(chunk["text"]),
                    score=score,
                )
            )

        return results

    @staticmethod
    def _tokenize(text: str) -> List[str]:
        return [
            token.strip()
            for token in jieba.cut(text)
            if token.strip()
        ]

    @staticmethod
    def _load_chunks(chunks_path: Path) -> List[dict]:
        if not chunks_path.exists():
            raise FileNotFoundError(f"chunks file not found: {chunks_path}")

        with chunks_path.open("r", encoding="utf-8") as file:
            data = json.load(file)

        if not isinstance(data, list):
            raise ValueError(f"chunks file should contain a list: {chunks_path}")

        return data


def print_results(results: List[SearchResult]) -> None:
    if not results:
        print("No results found.")
        return

    for rank, result in enumerate(results, start=1):
        print(f"{rank}. [{result.score:.4f}] {result.title}")
        print(f"   {result.text}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Search vehicle manual chunks with TF-IDF.")
    parser.add_argument(
        "--index",
        default="vector_db/chunks.json",
        help="Path to chunks JSON file.",
    )
    parser.add_argument(
        "--query",
        required=True,
        help="User query.",
    )
    parser.add_argument(
        "--top-k",
        type=int,
        default=3,
        help="Number of results to return.",
    )

    args = parser.parse_args()

    searcher = TfidfRagSearcher(Path(args.index))
    results = searcher.search(args.query, args.top_k)

    print_results(results)


if __name__ == "__main__":
    main()