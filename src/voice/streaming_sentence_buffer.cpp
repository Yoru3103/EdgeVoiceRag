#include "streaming_sentence_buffer.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
// 不添加， .等防止生成句子过短
std::vector<std::string> defaultDelimiters() {
    return {
        "。",
        "！",
        "？",
        "；",
        "!",
        "?",
        ";",
        "\n"
    };
}

std::string trimAsciiWhitespace(const std::string& text) {
    auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char character) {
            return std::isspace(character) != 0;
        }
    );

    if (first == text.end()) {
        return "";
    }

    auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char character) {
            return std::isspace(character) != 0;
        }
    ).base();

    return std::string(first, last);
}

struct Boundary {
    bool found = false;
    std::size_t position = 0;
    std::size_t length = 0;
};

// 中文标点UTF-8采用多字节编码，因此使用find寻找而非char遍历
Boundary findFirstBoundary(
    const std::string& text,
    const std::vector<std::string>& delimiters
) {
    Boundary result;

    std::size_t eraliest_position = std::numeric_limits<std::size_t>::max();

    for (const auto& delimiter : delimiters) {
        const std::size_t position = text.find(delimiter);

        if (position == std::string::npos) {
            continue;
        }

        const bool appears_earlier = position < eraliest_position;

        const bool samp_position_but_longer = position == eraliest_position &&
            delimiter.size() > result.length;
        
        if (appears_earlier || samp_position_but_longer) {
            result.found = true;
            result.position = position;
            result.length = delimiter.size();
            eraliest_position = position;
        }
    }

    return result;
}

void validateDelimiters(const std::vector<std::string>& delimiters) {
    if (delimiters.empty()) {
        throw std::invalid_argument(
            "sentence delimiters must not be empty"
        );
    }

    for (const auto& delimiter : delimiters) {
        if (delimiter.empty()) {
            throw std::invalid_argument(
                "sentence delimiter must not be empty"
            );
        }
    }
}

}   // namespace

StreamingSentenceBuffer::StreamingSentenceBuffer()
    : delimiters_(defaultDelimiters()) {
}

StreamingSentenceBuffer::StreamingSentenceBuffer(std::vector<std::string> delimiters)
    :delimiters_(std::move(delimiters)) {
    validateDelimiters(delimiters_);
}

std::vector<std::string> StreamingSentenceBuffer::append(const std::string& delta) {
    if (delta.empty()) {
        return {};
    }

    buffer_ += delta;

    return extractSentences();
}

std::string StreamingSentenceBuffer::flush() {
    std::string remaining = trimAsciiWhitespace(buffer_);

    buffer_.clear();

    return remaining;
}

void StreamingSentenceBuffer::clear() {
    buffer_.clear();
}

bool StreamingSentenceBuffer::empty() const {
    return buffer_.empty();
}

std::size_t StreamingSentenceBuffer::pendingBytes() const {
    return buffer_.size();
}

const std::string& StreamingSentenceBuffer::pendingText() const {
    return buffer_;
}

std::vector<std::string> StreamingSentenceBuffer::extractSentences() {
    std::vector<std::string> sentences;

    while (true) {
        const Boundary boundary = findFirstBoundary(buffer_, delimiters_);

        if (!boundary.found) {
            break;
        }

        const std::size_t sentence_end = boundary.position + boundary.length;

        std::string sentence = buffer_.substr(0, sentence_end);
        buffer_.erase(0, sentence_end);

        sentence = trimAsciiWhitespace(sentence);

        if (!sentence.empty()) {
            sentences.push_back(std::move(sentence));
        }
    }

    return sentences;
}
