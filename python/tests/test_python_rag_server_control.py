import threading

from rag.python_rag_server import PythonRagServer


class FakeCancellableGenerator:
    def __init__(self, result: bool = True) -> None:
        self.result = result
        self.cancelled_request_ids = []

    def cancel(self, request_id: str) -> bool:
        self.cancelled_request_ids.append(request_id)
        return self.result


def create_server_for_control_test(
    active_request_id: str,
    generator,
) -> PythonRagServer:
    server = PythonRagServer.__new__(PythonRagServer)
    server._active_request_id = active_request_id
    server._active_request_lock = threading.Lock()
    server.generator = generator
    return server


def test_cancel_active_request_is_forwarded_to_llm() -> None:
    generator = FakeCancellableGenerator()
    server = create_server_for_control_test(
        active_request_id="rag-1",
        generator=generator,
    )

    assert server._cancel_active_request("rag-1") is True
    assert generator.cancelled_request_ids == ["rag-1"]


def test_cancel_inactive_request_is_not_forwarded() -> None:
    generator = FakeCancellableGenerator()
    server = create_server_for_control_test(
        active_request_id="rag-1",
        generator=generator,
    )

    assert server._cancel_active_request("another-request") is False
    assert generator.cancelled_request_ids == []


def test_generator_without_cancel_returns_false() -> None:
    server = create_server_for_control_test(
        active_request_id="rag-1",
        generator=object(),
    )

    assert server._cancel_active_request("rag-1") is False
