import json
import threading
from typing import Callable, Optional

import zmq


PROTOCOL_VERSION = 1


class RagControlServer:
    def __init__(
        self,
        endpoint: str,
        cancel_callback: Callable[[str], bool],
        active_request_callback: Callable[[], str],
    ) -> None:
        if not endpoint.strip():
            raise ValueError("RAG control endpoint must not be empty")

        self.endpoint = endpoint
        self.cancel_callback = cancel_callback
        self.active_request_callback = active_request_callback
        self._running = threading.Event()
        self._ready = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._startup_error: Optional[Exception] = None

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            raise RuntimeError("RAG control server is already running")

        self._startup_error = None
        self._ready.clear()
        self._running.set()
        self._thread = threading.Thread(
            target=self._run,
            name="rag-control-server",
            daemon=True,
        )
        self._thread.start()

        if not self._ready.wait(timeout=2.0):
            self.stop()
            raise RuntimeError("RAG control server startup timed out")

        if self._startup_error is not None:
            error = self._startup_error
            self.stop()
            raise RuntimeError(
                f"RAG control server failed to start: {error}"
            ) from error

    def stop(self) -> None:
        self._running.clear()

        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    def handle_message(self, message: str) -> str:
        request_id = ""

        try:
            request = json.loads(message)

            if not isinstance(request, dict):
                raise ValueError("RAG control request must be an object")

            if request.get("version") != PROTOCOL_VERSION:
                raise ValueError("unsupported RAG control protocol version")

            if request.get("type") != "cancel":
                raise ValueError("unsupported RAG control request type")

            request_id = request.get("request_id", "")

            if not isinstance(request_id, str) or not request_id.strip():
                raise ValueError("missing RAG cancel request_id")

            request_id = request_id.strip()
            cancelled = self.cancel_callback(request_id)

            return json.dumps(
                {
                    "version": PROTOCOL_VERSION,
                    "type": "cancel_result",
                    "ok": True,
                    "request_id": request_id,
                    "cancelled": cancelled,
                    "active_request_id": self.active_request_callback(),
                    "error": "",
                },
                ensure_ascii=False,
            )
        except Exception as exc:
            return json.dumps(
                {
                    "version": PROTOCOL_VERSION,
                    "type": "cancel_result",
                    "ok": False,
                    "request_id": request_id,
                    "cancelled": False,
                    "active_request_id": self.active_request_callback(),
                    "error": str(exc),
                },
                ensure_ascii=False,
            )

    def _run(self) -> None:
        context = zmq.Context()
        socket = context.socket(zmq.REP)
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.RCVTIMEO, 200)

        try:
            socket.bind(self.endpoint)
            self._ready.set()

            while self._running.is_set():
                try:
                    message = socket.recv_string()
                except zmq.Again:
                    continue

                socket.send_string(self.handle_message(message))
        except Exception as exc:
            self._startup_error = exc
            self._running.clear()
            self._ready.set()
        finally:
            socket.close(linger=0)
            context.term()

