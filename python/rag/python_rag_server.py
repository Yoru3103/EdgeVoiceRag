import argparse
import json
import signal
import sys
from pathlib import Path
from typing import Any, List, Dict

import zmq

from tfidf_search import SearchResult, TfidfRagSearcher
from llm_generator import create_llm_generator

def result_to_dict(rank: int, result: SearchResult) -> Dict[str, Any]:
    return {
        "rank": rank,
        "chunk_id": result.chunk_id,
        "title": result.title,
        "content": result.content,
        "text": result.text,
        "score": result.score,
    }
    
def build_answer(results: List[SearchResult]) -> str:
    if not results:
        return "根据车辆手册：没有找到相关车辆手册内容。"
    
    lines = ["根据车辆手册："]
    
    for rank, result in enumerate(results, start=1):
        lines.append(f"{rank}. {result.content}")
        
    return "\n".join(lines)
    
def build_success_response(
    query: str,
    backend: str,
    results: List[SearchResult],
    generated_answer: str,
    prompt: str,
    llm_backend: str,
) -> Dict[str, Any]:
    return {
        "ok": True,
        "query": query,
        "backend": backend,
        "llm_backend":llm_backend,
        "answer": build_answer(results),
        "generated_answer": generated_answer,
        "prompt": prompt,
        "result_count": len(results),
        "results": [
            result_to_dict(rank, result)
            for rank, result in enumerate(results, start=1)
        ],
    }
    
def build_error_response(query: str, backend: str, error: str) -> Dict[str, Any]:
    return {
        "ok": False,
        "query": query,
        "backend": backend,
        "answer": "Python RAG 检索失败。",
        "error": error,
        "result_count": 0,
        "results": [],
    }
    
def response_to_json(response: Dict[str, Any]) -> str:
    return json.dumps(response, ensure_ascii=False)

class PythonRagServer:
    def __init__(
        self, endpoint: str, 
        index_path: Path, 
        top_k: int,
        llm_backend: str) -> None:
        self.endpoint = endpoint
        self.index_path = index_path
        self.top_k = top_k
        self.backend = "python_tdidf"
        
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REP)
        self.running = True
        
        self.searcher = TfidfRagSearcher(index_path)
        self.generator = create_llm_generator(llm_backend)
        
    def start(self) -> None:
        self.socket.bind(self.endpoint)
        
        print(f"[INFO] Python RAG server started.")
        print(f"[INFO] Endpoint: {self.endpoint}")
        print(f"[INFO] Index path: {self.index_path}")
        print(f"[INFO] Top-k: {self.top_k}")
        print(f"[INfO] LLM backend: {self.generator.backend}")
        
        while self.running:
            try:
                query = self.socket.recv_string()
                print(f"[REQUEST] {query}")
                
                if query == "exit":
                    response = {
                        "ok": True,
                        "query": query,
                        "backend": self.backend,
                        "answer": "python_rag_server exiting",
                        "message": "python_rag_server exiting",
                        "result_count": 0,
                        "results": [],
                    }
                    self.socket.send_string(response_to_json(response))
                    break
                
                results = self.searcher.search(query, self.top_k)
                
                contexts = [
                    result.text
                    for result in results
                ]
                
                generation = self.generator.generate(
                    query=query,
                    contexts=contexts,
                )
                response = build_success_response(
                    query=query,
                    backend=self.backend,
                    results=results,
                    generated_answer=generation.answer,
                    prompt=generation.prompt,
                    llm_backend=generation.backend,
                )
                
                self.socket.send_string(response_to_json(response))
                
            except KeyboardInterrupt:
                print("\n[INFO] KeyboardInterrupt received.")
                break
            
            except Exception as exc:
                error_query = ""
                try:
                    error_query = query
                except UnboundLocalError:
                    error_query = ""
                    
                response = build_error_response(
                    query=error_query,
                    backend=self.backend,
                    error=str(exc),
                )
                
                try:
                    self.socket.send_string(response_to_json(response))
                except Exception:
                    pass
                
        self.stop()
        
    def stop(self) -> None:
        self.running = False
        self.socket.close(linger=0) #linger控制socket关闭时，还没发出去的消息要不要等待发送完成。-1表示一直等到发送完毕，0表示立即关闭，大于零表示最大等待时间
        self.context.term() # 终止context
        print("[INFO] Python RAG server stopped.")
        
def main() -> None:
    parser = argparse.ArgumentParser(description="Python TF-IDF RAG ZeroMQ server.")
    parser.add_argument(
        "--endpoint",
        default="tcp://*:5556",
        help="ZeroMQ REP endpoint.",
    )
    parser.add_argument(
        "--index",
        default="vector_db/chunks.json",
        help="Path to chunks JSON index.",
    )
    parser.add_argument(
        "--top-k",
        type=int,
        default=3,
        help="Number of results to return.",
    )
    parser.add_argument(
        "--llm-backend",
        default="mock",
        choices=["mock"],
        help="LLM generation backen",
    )
    
    args = parser.parse_args()
    
    if args.top_k < 0:
        print("[ERROR] --top-k must be positive.")
        sys.exit(1)
        
    index_path = Path(args.index)
    
    if not index_path.exists():
        print(f"[ERROR] Index file not found: {index_path}")
        print("Please build it first:")
        print("  python python/rag/build_index.py --manual docs/vehicle_manual.txt --output vector_db/chunks.json")
        sys.exit(1)
        
    server = PythonRagServer(
        endpoint=args.endpoint,
        index_path=index_path,
        top_k=args.top_k,
        llm_backend=args.llm_backend,
    )
    
    def handle_signal(signum, frame) -> None:
        print(f"\n[INFO] signal received: {signum}")
        server.running = False
    
    # signal表示操作系统信号，SIGINT中断信号，通常表示ctrl+c；SIGTERM通常表示kill <pid>发出的终止请求
    # frame：收到信号那一刻，程序正在执行的代码位置
    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    
    server.start()
    
if __name__ == "__main__":
    main()
