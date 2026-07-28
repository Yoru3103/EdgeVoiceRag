#include <iostream>
#include <stdexcept>
#include <string>

#include "rag_control_protocol.h"

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

template <typename Exception, typename Function>
void expectThrows(
    Function function,
    const std::string& name
) {
    try {
        function();
    } catch (const Exception&) {
        std::cout << "[PASS] " << name << '\n';
        return;
    } catch (...) {
        std::cout
            << "[FAIL] "
            << name
            << " threw wrong exception\n";

        ++failed_count;
        return;
    }

    std::cout
        << "[FAIL] "
        << name
        << " did not throw\n";

    ++failed_count;
}

void testRequestRoundTrip() {
    const RagCancelRequest original{
        "rag-001"
    };

    const std::string message =
        RagControlProtocol::encodeRequest(
            original
        );

    const RagCancelRequest decoded =
        RagControlProtocol::decodeRequest(
            message
        );

    expectTrue(
        decoded.request_id ==
            original.request_id,
        "cancel request round trip"
    );
}

void testSuccessfulResponseRoundTrip() {
    const RagCancelResponse original{
        true,
        true,
        "rag-001",
        "rag-001",
        ""
    };

    const std::string message =
        RagControlProtocol::encodeResponse(
            original
        );

    const RagCancelResponse decoded =
        RagControlProtocol::decodeResponse(
            message
        );

    expectTrue(
        decoded.ok,
        "decode successful cancellation"
    );

    expectTrue(
        decoded.cancelled,
        "decode cancelled=true"
    );

    expectTrue(
        decoded.request_id == "rag-001",
        "preserve cancel request_id"
    );

    expectTrue(
        decoded.active_request_id ==
            "rag-001",
        "preserve active request_id"
    );
}

void testInactiveResponseRoundTrip() {
    const RagCancelResponse original{
        true,
        false,
        "rag-inactive",
        "",
        ""
    };

    const auto decoded =
        RagControlProtocol::decodeResponse(
            RagControlProtocol::encodeResponse(
                original
            )
        );

    expectTrue(
        decoded.ok,
        "inactive response is valid"
    );

    expectTrue(
        !decoded.cancelled,
        "inactive response is not cancelled"
    );
}

void testErrorResponseRoundTrip() {
    const RagCancelResponse original{
        false,
        false,
        "rag-001",
        "",
        "cannot cancel request"
    };

    const auto decoded =
        RagControlProtocol::decodeResponse(
            RagControlProtocol::encodeResponse(
                original
            )
        );

    expectTrue(
        !decoded.ok,
        "decode failed cancellation"
    );

    expectTrue(
        decoded.error ==
            "cannot cancel request",
        "preserve cancellation error"
    );
}

void testRejectInvalidVersion() {
    const std::string message = R"({
        "version": 99,
        "type": "cancel_result",
        "ok": true,
        "request_id": "rag-001",
        "cancelled": true,
        "active_request_id": "rag-001",
        "error": ""
    })";

    expectThrows<std::runtime_error>(
        [&message]() {
            RagControlProtocol::
                decodeResponse(message);
        },
        "reject invalid protocol version"
    );
}

void testRejectEmptyRequestId() {
    expectThrows<std::invalid_argument>(
        []() {
            RagControlProtocol::encodeRequest(
                RagCancelRequest{""}
            );
        },
        "reject empty cancel request_id"
    );
}

void testRejectInvalidCancelledType() {
    const std::string message = R"({
        "version": 1,
        "type": "cancel_result",
        "ok": true,
        "request_id": "rag-001",
        "cancelled": "true",
        "active_request_id": "rag-001",
        "error": ""
    })";

    expectThrows<std::runtime_error>(
        [&message]() {
            RagControlProtocol::
                decodeResponse(message);
        },
        "reject non-boolean cancelled"
    );
}

void testRejectFailedResponseWithoutError() {
    const RagCancelResponse response{
        false,
        false,
        "rag-001",
        "",
        ""
    };

    expectThrows<std::runtime_error>(
        [&response]() {
            RagControlProtocol::
                encodeResponse(response);
        },
        "reject failed response without error"
    );
}

}  // namespace

int main() {
    testRequestRoundTrip();
    testSuccessfulResponseRoundTrip();
    testInactiveResponseRoundTrip();
    testErrorResponseRoundTrip();
    testRejectInvalidVersion();
    testRejectEmptyRequestId();
    testRejectInvalidCancelledType();
    testRejectFailedResponseWithoutError();

    if (failed_count == 0) {
        std::cout
            << "\nAll RAG control protocol "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
