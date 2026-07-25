import json

import pytest

from rag.rag_control_server import RagControlServer


def create_server(
    cancel_callback,
    active_request_callback=lambda: "",
) -> RagControlServer:
    return RagControlServer(
        endpoint="tcp://127.0.0.1:5558",
        cancel_callback=cancel_callback,
        active_request_callback=active_request_callback,
    )


def test_handle_cancel_request() -> None:
    cancelled_ids = []
    server = create_server(
        cancel_callback=lambda request_id: (
            cancelled_ids.append(request_id) is None
        ),
        active_request_callback=lambda: "rag-1",
    )

    response = json.loads(
        server.handle_message(
            json.dumps(
                {
                    "version": 1,
                    "type": "cancel",
                    "request_id": "rag-1",
                }
            )
        )
    )

    assert response["ok"] is True
    assert response["cancelled"] is True
    assert response["request_id"] == "rag-1"
    assert response["active_request_id"] == "rag-1"
    assert cancelled_ids == ["rag-1"]


def test_inactive_request_returns_false() -> None:
    server = create_server(
        cancel_callback=lambda request_id: False,
    )

    response = json.loads(
        server.handle_message(
            json.dumps(
                {
                    "version": 1,
                    "type": "cancel",
                    "request_id": "inactive",
                }
            )
        )
    )

    assert response["ok"] is True
    assert response["cancelled"] is False


@pytest.mark.parametrize(
    ("payload", "expected_error"),
    [
        (
            {"version": 99, "type": "cancel", "request_id": "rag-1"},
            "protocol version",
        ),
        (
            {"version": 1, "type": "unknown", "request_id": "rag-1"},
            "request type",
        ),
        (
            {"version": 1, "type": "cancel", "request_id": ""},
            "request_id",
        ),
    ],
)
def test_reject_invalid_request(
    payload,
    expected_error: str,
) -> None:
    server = create_server(
        cancel_callback=lambda request_id: True,
    )

    response = json.loads(
        server.handle_message(json.dumps(payload))
    )

    assert response["ok"] is False
    assert response["cancelled"] is False
    assert expected_error in response["error"]


def test_cancel_callback_error_becomes_response() -> None:
    def fail_cancel(request_id: str) -> bool:
        raise RuntimeError(f"cannot cancel {request_id}")

    server = create_server(cancel_callback=fail_cancel)
    response = json.loads(
        server.handle_message(
            json.dumps(
                {
                    "version": 1,
                    "type": "cancel",
                    "request_id": "rag-1",
                }
            )
        )
    )

    assert response["ok"] is False
    assert response["cancelled"] is False
    assert response["error"] == "cannot cancel rag-1"
