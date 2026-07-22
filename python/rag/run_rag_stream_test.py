import json
import uuid

import zmq


def main() -> None:
    endpoint = "tcp://127.0.0.1:5557"
    request_id = uuid.uuid4().hex

    context = zmq.Context()
    socket = context.socket(zmq.DEALER)

    socket.setsockopt(zmq.LINGER, 0)
    socket.setsockopt(zmq.SNDTIMEO, 5000)
    socket.setsockopt(zmq.RCVTIMEO, 5000)

    expected_sequence = 0
    accumulated_answer = ""

    try:
        socket.connect(endpoint)

        request = {
            "version": 1,
            "type": "rag_query",
            "request_id": request_id,
            "query": "空调温度怎么调节",
            "stream": True,
        }

        print("[REQUEST]")
        print(
            json.dumps(
                request,
                ensure_ascii=False,
                indent=2,
            )
        )

        socket.send_string(
            json.dumps(
                request,
                ensure_ascii=False,
            )
        )

        while True:
            raw_response = socket.recv_string()
            event = json.loads(raw_response)

            print("\n[EVENT]")
            print(
                json.dumps(
                    event,
                    ensure_ascii=False,
                    indent=2,
                )
            )

            if event.get("request_id") != request_id:
                raise RuntimeError(
                    "response request_id mismatch"
                )

            sequence = event.get("sequence")

            if sequence != expected_sequence:
                raise RuntimeError(
                    "sequence mismatch: "
                    f"expected {expected_sequence}, "
                    f"received {sequence}"
                )

            event_type = event.get("type")

            if event_type == "rag_chunk":
                delta = event.get("delta")

                if not isinstance(delta, str) or not delta:
                    raise RuntimeError(
                        "rag_chunk has empty delta"
                    )

                accumulated_answer += delta
                expected_sequence += 1

                print(
                    "[CHUNK] "
                    f"{delta}"
                )
                continue

            if event_type == "rag_error":
                raise RuntimeError(
                    event.get(
                        "error",
                        "unknown RAG stream error",
                    )
                )

            if event_type == "rag_finished":
                final_answer = event.get("answer")

                if accumulated_answer != final_answer:
                    raise RuntimeError(
                        "chunks do not match "
                        "final answer"
                    )

                if event.get("finished") is not True:
                    raise RuntimeError(
                        "finished flag is not true"
                    )

                print("\n[PASS]")
                print(
                    "RAG streaming integration "
                    "test passed"
                )
                print(
                    f"Final answer: {final_answer}"
                )
                print(
                    "LLM backend: "
                    f"{event.get('llm_backend')}"
                )
                print(
                    "Elapsed: "
                    f"{event.get('elapsed_ms')} ms"
                )
                break

            raise RuntimeError(
                f"unexpected event: {event_type}"
            )

    except zmq.Again as exc:
        raise RuntimeError(
            f"request timed out: {endpoint}"
        ) from exc
    finally:
        socket.close(linger=0)
        context.term()


if __name__ == "__main__":
    main()
