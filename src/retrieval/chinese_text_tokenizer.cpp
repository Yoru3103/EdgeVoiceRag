#include "chinese_text_tokenizer.h"

#include <cstdint>
#include <utility>

std::vector<std::string> ChineseTextTokenizer::tokenize(std::string_view text) const {
    std::vector<std::string> tokens;

    // 保存一段连续的中文字符
    std::vector<char32_t> cjk_run;
    // 保存一段连续的ASCII字母或数字(均为小写)
    std::string ascii_run;

    // 输出当前连续中文片段中的 unigram 和 bigram。
    const auto flushCjkRun = [&tokens, &cjk_run]() {
        if (cjk_run.empty()) {
            return;
        }

        // 先产生单字token
        for (const char32_t code_point : cjk_run) {
            tokens.push_back(encodeUtf8(code_point));
        }

        // 再产生相邻双字token
        for (std::size_t index = 0; index + 1 < cjk_run.size(); index++) {
            std::string bigram = encodeUtf8(cjk_run[index]);

            bigram += encodeUtf8(cjk_run[index + 1]);

            tokens.push_back(std::move(bigram));
        }

        cjk_run.clear();
    };

    // 输出当前连续英文或数字 token。
    const auto flushAsciiRun = [&tokens, &ascii_run]() {
        if (ascii_run.empty()) {
            return;
        }

        tokens.push_back(std::move(ascii_run));
        ascii_run.clear();
    };

    std::size_t offset = 0;

    while (offset < text.size()) {
        char32_t code_point = 0;

        const bool decoded = decodeNextUtf8(text, offset, code_point);

        if (!decoded) {
            flushCjkRun();
            flushAsciiRun();
            continue;
        }

        if (isCjkCharacter(code_point)) {
            flushAsciiRun();
            cjk_run.push_back(code_point);
            continue;
        }

        if (isAsciiAlphanumeric(code_point)) {
            flushCjkRun();

            ascii_run.push_back(toLowerAscii(static_cast<char>(code_point)));
            continue;
        }

         /*
         * 空格、逗号、句号、冒号等其他字符
         * 都被视为 token 边界。
         *
         * 这样不会给：
         *   "空调，打开"
         *
         * 生成跨标点 bigram：
         *   "调打"
         */
        flushCjkRun();
        flushAsciiRun();
    }

    // 字符串结尾可能还有未输出的token
    flushCjkRun();
    flushAsciiRun();

    return tokens;
}

bool ChineseTextTokenizer::decodeNextUtf8(
    std::string_view text,      // 只查看调用方已有的字符串，不复制内容
    std::size_t& offset,
    char32_t& code_point
) {
    if (offset >= text.size()) {
        return false;
    }

    const auto first = static_cast<unsigned char>(text[offset]);

    /*
     * ASCII：
     * 0xxxxxxx
     */
    if (first <= 0x7FU) {
        code_point = static_cast<char32_t>(first);
        offset++;
        return true;
    }

    std::size_t sequence_length = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum_value = 0;

    /*
     * 两字节 UTF-8：
     * 110xxxxx 10xxxxxx
     */
    if ((first & 0xE0U) == 0xC0U) {
        sequence_length = 2;
        value = first & 0x1Fu;
        minimum_value = 0x80U;
    }
    /*
     * 三字节 UTF-8：
     * 1110xxxx 10xxxxxx 10xxxxxx
     *
     * 常用中文通常属于这一类。
     */
    else if ((first & 0xF0U) == 0xE0U) {
        sequence_length = 3;
        value = first & 0x0Fu;
        minimum_value = 0x800U;
    }
    /*
     * 四字节 UTF-8：
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
     */
    else if ((first & 0xF8U) == 0xF0U) {
        sequence_length = 4;
        value = first & 0x07U;
        minimum_value = 0x10000U;
    } else {
        offset++;
        return false;
    }

    if (offset + sequence_length > text.size()) {
        offset++;
        return false;
    }

    for (std::size_t index = 1; index < sequence_length; index++) {
        const auto continuation = static_cast<unsigned char>(text[offset + index]);

        if ((continuation & 0xC0U) != 0x80U) {
            offset++;
            return false;
        }

        value = (value << 6U) | static_cast<std::uint32_t>(continuation & 0x3FU);
    }

    /*
     * 拒绝以下非法 Unicode：
     *
     * 1. 过长编码；
     * 2. UTF-16 surrogate；
     * 3. 超过 Unicode 最大码点。
     */
    if (
        value < minimum_value ||
        (value >= 0xD800U && value <= 0xDFFFU) ||
        value > 0x10FFFFU
    ) {
        offset++;
        return false;
    }

    offset += sequence_length;
    code_point = static_cast<char32_t>(value);

    return true;
}

std::string ChineseTextTokenizer::encodeUtf8(char32_t code_point) {
    const auto value = static_cast<std::uint32_t>(code_point);

    std::string encoded;

    if (value <= 0x7FU) {
        encoded.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
        encoded.push_back(static_cast<char>(0xC0U | (value >> 6U)));

        encoded.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        encoded.push_back(static_cast<char>(0xE0U | (value >> 12U)));

        encoded.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));

        encoded.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0x10FFFFU) {
        encoded.push_back(static_cast<char>(0xF0U | (value >> 18U)));

        encoded.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));

        encoded.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));

        encoded.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }

    return encoded;
}

bool ChineseTextTokenizer::isCjkCharacter(char32_t code_point) {
    const auto value = static_cast<std::uint32_t>(code_point);

    return
        // CJK Unified Ideographs Extension A
        (value >= 0x3400U && value <= 0x4DBFU) ||
        // CJK Unified Ideographs
        (value >= 0x4E00U && value <= 0x9FFFU) ||
        // CJK Compatibility Ideographs
        (value >= 0xF900U && value <= 0xFAFFU) ||
        // CJK Extensions B 到 F 等扩展区
        (value >= 0x20000U && value <= 0x2FA1FU) ||
        // 更新的 CJK 扩展区
        (value >= 0x30000U && value <= 0x323AFU);
}

bool ChineseTextTokenizer::isAsciiAlphanumeric(char32_t code_point) {
    return
        (code_point >= U'a' && code_point <= U'z') ||
        (code_point >= U'A' && code_point <= U'Z') ||
        (code_point >= U'0' && code_point <= U'9');
}

char ChineseTextTokenizer::toLowerAscii(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }

    return value;
}
