#pragma once

#include <cstddef>
#include <string>
#include <vector>

class StreamingSentenceBuffer {
public:
    StreamingSentenceBuffer();

    explicit StreamingSentenceBuffer(std::vector<std::string> delimiters);

    // 追加一段LLM输出，并返回其中新出现的完整句子
    std::vector<std::string> append(const std::string& delta);

    // 生成结束后，将没有标点的剩余文本取出，同时清空缓冲区
    std::string flush();
    
    void clear();

    bool empty() const;

    std::size_t pendingBytes() const;

    const std::string& pendingText() const;

private:
//  缓冲器为单线程设计，每个线程单独使用，没有上锁
    std::string buffer_;
    std::vector<std::string> delimiters_;

    std::vector<std::string> extractSentences();
};
