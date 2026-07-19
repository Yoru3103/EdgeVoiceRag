#pragma once

#include <string>

#include "response_backend.h"

class TextRequester {
public:
    virtual ~TextRequester() = default;

    virtual BackendResult request(
        const std::string& endpoint,
        const std::string& text,
        int timeout_ms
    ) = 0;
};

class ZmqTextClient : public TextRequester {
public:
    BackendResult request(
        const std::string& eendpoint,
        const std::string& text,
        int timeout_ms
    ) override;
};