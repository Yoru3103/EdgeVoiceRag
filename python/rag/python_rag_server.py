import argparse
import json
import signal
import sys
from pathlib import Path
from typing import Any, List, Dict
from time import perf_counter

import zmq

from rag.tfidf_search import SearchResult, TfidfRagSearcher
from rag.llm_generator import create_llm_generator
from rag.rag_stream_protocol import (
    RagStreamEvent,
    decode_rag_stream_request,
    encode_rag_stream_event,
)

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
    include_prompt: bool,
) -> Dict[str, Any]:
    response = {
        "ok": True,
        "query": query,
        "backend": backend,
        "llm_backend":llm_backend,
        "answer": build_answer(results),
        "generated_answer": generated_answer,
        "result_count": len(results),
        "results": [
            result_to_dict(rank, result)
            for rank, result in enumerate(results, start=1)
        ],
    }

    if include_prompt:
        response["prompt"] = prompt

    return response

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
        self,
        endpoint: str,
        index_path: Path,
        top_k: int,
        llm_backend: str,
        llm_model: str,
        ollama_url: str,
        llm_endpoint: str,
        llm_timeout: int,
        llm_health_check: bool,
        stream_endpoint: str,
        llm_stream_endpoint: str,
        include_prompt: bool,
    ) -> None:
        self.endpoint = endpoint
        self.index_path = index_path
        self.top_k = top_k
        self.backend = "python_tfidf"
        self.include_prompt = include_prompt

        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REP)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.running = True

        self.searcher = TfidfRagSearcher(index_path)

        self.generator = create_llm_generator(
            backend=llm_backend,
            model=llm_model,
            base_url=ollama_url,
            endpoint=llm_endpoint,
            stream_endpoint=llm_stream_endpoint,
            timeout_seconds=llm_timeout,
            enable_health_check=llm_health_check,
        )

        self.stream_endpoint = stream_endpoint

        self.stream_socket = self.context.socket(zmq.ROUTER)
        self.stream_socket.setsockopt(zmq.LINGER, 0)

    def _send_stream_event(
                self,
                identity: bytes,
                event: RagStreamEvent,
            ) -> None:
                message = encode_rag_stream_event(event).encode("utf-8")

                self.stream_socket.send_multipart([identity, message])

    def _receive_stream_request(self) -> tuple[bytes, str]:
        frames = self.stream_socket.recv_multipart()

        if len(frames) != 2:
            raise RuntimeError(
                "RAG ROUTER request must contain "
                "identity and payload"
            )

        identity, payload = frames

        try:
            message = payload.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RuntimeError(
                "RAG stream request is not UTF-8"
            ) from exc

        return identity, message

    def _handle_stream_request(self) -> None:
        identity, message = self._receive_stream_request()

        request = None

        start = perf_counter()
        sequence = 0
        llm_backend = ""

        try:
            request = decode_rag_stream_request(message)

            results = self.searcher.search(
                request.query,
                self.top_k,
            )

            contexts = [
                result.text
                for result in results
            ]

            if not contexts:
                answer = ("车辆手册中没有找到相关内容。")

                self._send_stream_event(
                    identity,
                    RagStreamEvent(
                        type="rag_chunk",
                        request_id=request.request_id,
                        sequence=sequence,
                        delta=answer,
                        answer="",
                        backend=self.backend,
                        llm_backend="none",
                        error="",
                        elapsed_ms=(
                            perf_counter() - start
                        ) * 1000,
                        finished=False,
                    ),
                )

                sequence += 1

                self._send_stream_event(
                    identity,
                    RagStreamEvent(
                        type="rag_finished",
                        request_id=request.request_id,
                        sequence=sequence,
                        delta="",
                        answer=answer,
                        backend=self.backend,
                        llm_backend="none",
                        error="",
                        elapsed_ms=(
                            perf_counter() - start
                        ) * 1000,
                        finished=True,
                    ),
                )

                return

            if not hasattr(
                self.generator,
                "generate_stream",
            ):
                raise RuntimeError(
                    "selected LLM backend does not "
                    "support streaming"
                )

            accumulated_answer = ""

            for event in self.generator.generate_stream(
                query=request.query,
                contexts=contexts,
            ):
                llm_backend = event.backend

                if event.type == "generation_chunk":
                    accumulated_answer += event.delta

                    self._send_stream_event(
                        identity,
                        RagStreamEvent(
                            type="rag_chunk",
                            request_id=request.request_id,
                            sequence=sequence,
                            delta=event.delta,
                            answer="",
                            backend=self.backend,
                            llm_backend=llm_backend,
                            error="",
                            elapsed_ms=(
                                perf_counter() - start
                            ) * 1000,
                            finished=False,
                        ),
                    )

                    sequence += 1
                    continue

                if (event.type == "generation_finished"):
                    if (accumulated_answer != event.answer):
                        raise RuntimeError(
                            "RAG stream chunks do not "
                            "match LLM final answer"
                        )

                    self._send_stream_event(
                        identity,
                        RagStreamEvent(
                            type="rag_finished",
                            request_id=(
                                request.request_id
                            ),
                            sequence=sequence,
                            delta="",
                            answer=event.answer,
                            backend=self.backend,
                            llm_backend=llm_backend,
                            error="",
                            elapsed_ms=(
                                perf_counter() - start
                            ) * 1000,
                            finished=True,
                        ),
                    )

                    return

            raise RuntimeError(
                "LLM stream ended without "
                "finished event"
            )
        except Exception as exc:
            if request is None:
                print(
                    "[WARNING] Invalid RAG stream "
                    f"request: {exc}"
                )

                return

            self._send_stream_event(
                identity,
                RagStreamEvent(
                    type="rag_error",
                    request_id=request.request_id,
                    sequence=sequence,
                    delta="",
                    answer="",
                    backend=self.backend,
                    llm_backend=(
                        llm_backend or
                        self.generator.backend
                    ),
                    error=str(exc),
                    elapsed_ms=(
                        perf_counter() - start
                    ) * 1000,
                    finished=True,
                ),
            )

    def start(self) -> None:
        self.socket.bind(self.endpoint)
        self.stream_socket.bind(self.stream_endpoint)

        poller = zmq.Poller()
        poller.register(self.socket, zmq.POLLIN)
        poller.register(self.stream_socket, zmq.POLLIN)

        print(f"[INFO] Python RAG server started.")
        print(f"[INFO] Endpoint: {self.endpoint}")
        print(f"[INFO] Index path: {self.index_path}")
        print(f"[INFO] Top-k: {self.top_k}")
        print(f"[INFO] LLM backend: {self.generator.backend}")
        print(f"[INFO] LLM timeout: {self.generator.timeout_seconds if hasattr(self.generator, 'timeout_seconds') else 'N/A'} seconds")
        print(f"[INFO] Include prompt: {self.include_prompt}")
        print(f"[INFO] Stream endpoint: {self.stream_endpoint}")

        while self.running:
            try:
                events = dict(poller.poll(500))

                if self.stream_socket in events:
                    self._handle_stream_request()

                if self.socket not in events:
                    continue

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
                    include_prompt=self.include_prompt,
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
        self.stream_socket.close(linger=0)
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
        choices=["mock", "ollama", "zmq"],
        help="LLM generation backen",
    )
    parser.add_argument(
        "--llm-model",
        default="qwen2.5:1.5b",
        help="LLM model name for Ollama backend."
    )
    parser.add_argument(
        "--ollama-url",
        default="http://localhost:11434",
        help="Ollama base URL.",
    )
    parser.add_argument(
        "--llm-endpoint",
        default="tcp://127.0.0.1:8899",
        help="C++ LLM ZeroMQ service endpoint.",
    )
    parser.add_argument(
        "--llm-timeout",
        type=int,
        default=60,
        help="LLM request timeout in seconds.",
    )
    parser.add_argument(
        "--disable-llm-health-check",
        action="store_true",
        help="Disable LLM backend health check on startup.",
    )
    parser.add_argument(
        "--include-prompt",
        action="store_true",
        help="Include full prompt in JSON response for debugging.",
    )
    parser.add_argument(
        "--stream-endpoint",
        default="tcp://*:5557",
        help="RAG streaming ROUTER endpoint.",
    )
    parser.add_argument(
        "--llm-stream-endpoint",
        default="tcp://127.0.0.1:8900",
        help="C++ LLM streaming endpoint.",
    )

    args = parser.parse_args()

    if args.top_k <= 0:
        print("[ERROR] --top-k must be greater than zero.")
        sys.exit(1)

    if args.llm_timeout <= 0:
        print("[ERROR] --llm-timeout must be greater than zero.")
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
        llm_model=args.llm_model,
        ollama_url=args.ollama_url,
        llm_endpoint=args.llm_endpoint,
        llm_timeout=args.llm_timeout,
        llm_health_check=not args.disable_llm_health_check,
        llm_stream_endpoint=args.llm_stream_endpoint,
        stream_endpoint=args.stream_endpoint,
        include_prompt=args.include_prompt,
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
