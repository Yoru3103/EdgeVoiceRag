#include "rag_control_client.h"

#include <cctype>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "rag_control_protocol.h"
#include "response_backend.h"

namespace {

std::string trimAsciiWhitespace(const std::string& value) {
    std::size_t first = 0;

    while (
        first < value.size() &&
        std::isspace(
            static_cast<unsigned char>(
                value[first]
            )
        )
    ) {
        ++first;
    }

    std::size_t last = value.size();

    // 左闭右开
    while (
        last > first &&
        std::isspace(
            static_cast<unsigned char>(
                value[last - 1]
            )
        )
    ) {
        --last;
    }

    return value.substr(first, last - first);
}

void validateOptions(
    const std::string& endpoint,
    int timeout_ms
) {
    if (trimAsciiWhitespace(endpoint).empty()) {
        throw std::invalid_argument(
            "RAG control endpoint must not "
            "be empty"
        );
    }

    if (timeout_ms <= 0) {
        throw std::invalid_argument(
            "RAG control timeout must be "
            "greater than zero"
        );
    }
}

}   // namespace

RagControlClient::RagControlClient(
    TextRequester& requester,
    std::string endpoint,
    int timeout_ms
)
    : requester_(requester)
    , endpoint_(endpoint)
    , timeout_ms_(timeout_ms) {
    validateOptions(endpoint_, timeout_ms_);
}

std::string RagControlClient::name() const {
    return "zmq_rag_control";
}

CancellationResult RagControlClient::cancel(const std::string& request_id) const {
    const std::string normalized_request_id = trimAsciiWhitespace(request_id);

    if (normalized_request_id.empty()) {
        return CancellationResult::failure(
            "",
            "RAG cancel request_id must not be empty"
        );
    }

    try {
        const std::string request_message = 
            RagControlProtocol::encodeRequest(
                RagCancelRequest{
                    normalized_request_id
                }
            );

            const BackendResult transport_result = requester_.request(
                endpoint_,
                request_message,
                timeout_ms_
            );

            if (!transport_result.ok) {
                return CancellationResult::failure(
                    normalized_request_id,
                    "RAG cancellation transport failed: " + transport_result.error
                );
            }

            const RagCancelResponse response = RagControlProtocol::decodeResponse(transport_result.text);

            if (response.request_id != normalized_request_id) {
                return CancellationResult::failure(
                    normalized_request_id,
                    "RAG cancel response request_id mismatch"
                );
            }

            if (!response.ok) {
                return CancellationResult::failure(
                    normalized_request_id,
                    response.error.empty() ? "RAG cancellation failed" : response.error
                );
            }

            return CancellationResult::success(
                normalized_request_id,
                response.cancelled,
                response.active_request_id
            );
    } catch (const std::exception& error) {
        return CancellationResult::failure(
            normalized_request_id,
            "invalid RAG cancellation "
            "response: " +
            std::string(error.what())
        );
    }
}

const std::string& RagControlClient::endpoint() const {
    return endpoint_;
}

int RagControlClient::timeoutMilliseconds() const {
    return timeout_ms_;
}
