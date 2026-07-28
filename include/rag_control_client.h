#pragma once

#include <string>

#include "cancellable_answer_backend.h"
#include "zmq_text_client.h"

class RagControlClient final
    : public CancellableAnswerBackend {
public:
    RagControlClient(
        TextRequester& requester,
        std::string endpoint,
        int timeout_ms = 2000
    );

    std::string name() const override;

    CancellationResult cancel(const std::string& request_id) const override;

    const std::string& endpoint() const;

    int timeoutMilliseconds() const;
    
private:
    TextRequester& requester_;

    std::string endpoint_;
    int timeout_ms_;
};
