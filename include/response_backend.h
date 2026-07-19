#pragma once

#include <string>

// 统一成功和失败格式
struct BackendResult {
    bool ok = false;
    std::string text;
    std::string error;

    static BackendResult success(const std::string& text) {
        return BackendResult{
            true,
            text,
            ""
        };
    }

    static BackendResult failure(const std::string& error) {
        return BackendResult{
            false,
            "",
            error
        };
    }
};

class ResponseBackend {
public:
    virtual ~ResponseBackend() = default;
    // 查询车辆知识库
    virtual BackendResult searchRag(const std::string& query) = 0;
    // 调用大模型
    virtual BackendResult generateLlm(const std::string& prompt) = 0;
};