# 提供统一 JSON 搜索入口。

import argparse
import json
from pathlib import Path
from typing import List, Dict, Any

from tfidf_search import SearchResult, TfidfRagSearcher

def result_to_dict(rank: int, result: SearchResult) -> Dict[str, Any]:
    return {
        "rank": rank,
        "chunk_id": result.chunk_id,
        "content": result.content,
        "score": result.score,
        "text": result.text,
        "title": result.title,
    }
    
def build_answer(results: List[SearchResult]) -> str:
    if not results:
        return "根据车辆手册：没有找到相关车辆手册内容。"
    
    lines = ["根据车辆手册："]
    
    for rank, result in enumerate(results, start=1):
        lines.append(f"{rank}, {result}")
        
    return "\n".join(lines)
    
def build_success_response(
    query: str,
    backend: str,
    results: List[SearchResult],
) -> Dict[str, Any]:
    return {
        "ok": True,
        "query": query,
        "backend": backend,
        "answer": build_answer(results),
        "result_count": len(results),
        "results": [
            result_to_dict(rank, result)
            for rank, result in enumerate(results, start=1)
        ],
    }
    
def build_error_response(
    query: str,
    backend: str,
    error: str,
) -> Dict[str, Any]:
    return {
        "ok": False,
        "query": query,
        "backend": backend,
        "answer": "Python RAG 检索失败。",
        "error": error,
        "result_count": 0,
        "results": [],
    }
    
def print_json_response(response: Dict[str, Any]) -> None:
    print(json.dumps(response, ensure_ascii=False, indent=2))
    
def main() -> None:
    parser = argparse.ArgumentParser(description="Unified Python RAG search entry.")
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
        default=3,
        type=int,
        help="Number of results to return.",
    )
    parser.add_argument(
        "--backend",
        default="tfidf",
        choices=["tfidf"],
        help="Search backend.",
    )
    
    args = parser.parse_args()
    
    try:
        if args.backend == "tfidf":
            searcher = TfidfRagSearcher(Path(args.index))
            results = searcher.search(args.query, args.top_k)
        else:
            raise ValueError(f"Unsupported backend: {args.backend}")
        
        response = build_success_response(
            query=args.query,
            backend=args.backend,
            results=results,
        )
        
    except Exception as exc:
        response = build_error_response(
            query=args.query,
            backend=args.backend,
            error=str(exc),
        )
        
    print_json_response(response)
    
if __name__ == "__main__":
    main()