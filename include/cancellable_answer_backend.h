#pragma once

#include <string>

struct CancellationResult {
    bool ok = false;
    bool cancelled = false;

    std::string request_id;
    std::string active_request_id;
    std::string error;

    static CancellationResult success(
        const std::string& request_id,
        bool cancelled,
        const std::string& active_request_id
    );

    static CancellationResult failure(
        const std::string& request_id,
        const std::string& error
    );
};

class CancellableAnswerBackend {
public:
    virtual ~CancellableAnswerBackend() = default;

    virtual std::string name() const = 0;

    virtual CancellationResult cancel(
        const std::string& request_id
    ) const = 0;
};
