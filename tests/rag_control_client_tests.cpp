#include <iostream>
#include <stdexcept>
#include <string>

#include "rag_control_client.h"
#include "rag_control_protocol.h"
#include "response_backend.h"
#include "zmq_text_client.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

class MockTextRequester final
    : public TextRequester {
public:
    BackendResult result =
        BackendResult::failure(
            "mock response not configured"
        );

    int call_count = 0;

    std::string last_endpoint;
    std::string last_message;
    int last_timeout_ms = 0;

    BackendResult request(
        const std::string& endpoint,
        const std::string& text,
        int timeout_ms
    ) override {
        ++call_count;

        last_endpoint = endpoint;
        last_message = text;
        last_timeout_ms = timeout_ms;

        return result;
    }
};

void testSuccessfulCancellation() {
    MockTextRequester requester;

    requester.result =
        BackendResult::success(
            RagControlProtocol::encodeResponse(
                RagCancelResponse{
                    true,
                    true,
                    "rag-001",
                    "rag-001",
                    ""
                }
            )
        );

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558",
        1500
    );

    const CancellationResult result =
        client.cancel("rag-001");

    expectTrue(
        result.ok,
        "cancellation request succeeds"
    );

    expectTrue(
        result.cancelled,
        "active request is cancelled"
    );

    expectTrue(
        result.request_id == "rag-001",
        "preserve request_id"
    );

    expectTrue(
        requester.call_count == 1,
        "transport called once"
    );

    expectTrue(
        requester.last_endpoint ==
            "tcp://127.0.0.1:5558",
        "use configured endpoint"
    );

    expectTrue(
        requester.last_timeout_ms == 1500,
        "use configured timeout"
    );

    const RagCancelRequest sent_request =
        RagControlProtocol::decodeRequest(
            requester.last_message
        );

    expectTrue(
        sent_request.request_id == "rag-001",
        "encode expected cancel request"
    );
}

void testInactiveRequest() {
    MockTextRequester requester;

    requester.result =
        BackendResult::success(
            RagControlProtocol::encodeResponse(
                RagCancelResponse{
                    true,
                    false,
                    "rag-inactive",
                    "",
                    ""
                }
            )
        );

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558"
    );

    const auto result =
        client.cancel("rag-inactive");

    expectTrue(
        result.ok,
        "inactive cancel request is valid"
    );

    expectTrue(
        !result.cancelled,
        "inactive request returns false"
    );
}

void testTransportFailure() {
    MockTextRequester requester;

    requester.result =
        BackendResult::failure(
            "request timed out"
        );

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558"
    );

    const auto result =
        client.cancel("rag-timeout");

    expectTrue(
        !result.ok,
        "transport failure is propagated"
    );

    expectTrue(
        result.error.find(
            "transport failed"
        ) != std::string::npos,
        "transport failure contains context"
    );
}

void testRejectResponseIdMismatch() {
    MockTextRequester requester;

    requester.result =
        BackendResult::success(
            RagControlProtocol::encodeResponse(
                RagCancelResponse{
                    true,
                    true,
                    "another-request",
                    "another-request",
                    ""
                }
            )
        );

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558"
    );

    const auto result =
        client.cancel("rag-expected");

    expectTrue(
        !result.ok,
        "reject response ID mismatch"
    );

    expectTrue(
        result.error.find(
            "request_id mismatch"
        ) != std::string::npos,
        "ID mismatch contains reason"
    );
}

void testRejectMalformedResponse() {
    MockTextRequester requester;

    requester.result =
        BackendResult::success(
            "{not valid JSON"
        );

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558"
    );

    const auto result =
        client.cancel("rag-001");

    expectTrue(
        !result.ok,
        "reject malformed response"
    );

    expectTrue(
        result.error.find(
            "invalid RAG cancellation"
        ) != std::string::npos,
        "malformed response contains context"
    );
}

void testRejectEmptyRequestIdWithoutTransport() {
    MockTextRequester requester;

    RagControlClient client(
        requester,
        "tcp://127.0.0.1:5558"
    );

    const auto result =
        client.cancel("   ");

    expectTrue(
        !result.ok,
        "reject whitespace request_id"
    );

    expectTrue(
        requester.call_count == 0,
        "invalid request does not call transport"
    );
}

void testRejectInvalidConfiguration() {
    MockTextRequester requester;

    bool endpoint_rejected = false;

    try {
        RagControlClient client(
            requester,
            " ",
            1000
        );
    } catch (const std::invalid_argument&) {
        endpoint_rejected = true;
    }

    expectTrue(
        endpoint_rejected,
        "reject empty control endpoint"
    );

    bool timeout_rejected = false;

    try {
        RagControlClient client(
            requester,
            "tcp://127.0.0.1:5558",
            0
        );
    } catch (const std::invalid_argument&) {
        timeout_rejected = true;
    }

    expectTrue(
        timeout_rejected,
        "reject invalid control timeout"
    );
}

}  // namespace

int main() {
    testSuccessfulCancellation();
    testInactiveRequest();
    testTransportFailure();
    testRejectResponseIdMismatch();
    testRejectMalformedResponse();
    testRejectEmptyRequestIdWithoutTransport();
    testRejectInvalidConfiguration();

    if (failed_count == 0) {
        std::cout
            << "\nAll RAG control client "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}