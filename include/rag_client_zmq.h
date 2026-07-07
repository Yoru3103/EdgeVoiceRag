#pragma once

#include <string>

class RagClilentZmq {
public:
    explicit RagClilentZmq(const std::string& endpoint);

    std::string query(const std::string& question);

private:
    std::string endpoint_;
};