#pragma once

#include <string>

class RagClilentZmq {
public:
    explicit RagClilentZmq(const std::string& endpoint, int timeout_ms = 3000);

    std::string query(const std::string& question);

private:
    std::string endpoint_;
    int timeout_ms_;
};