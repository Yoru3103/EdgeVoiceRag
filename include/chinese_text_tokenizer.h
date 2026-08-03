#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/*
 * 面向中文知识库检索的轻量 tokenizer。
 *
 * 主要功能：
 * 1. UTF-8 解码；
 * 2. 中文字符 unigram；
 * 3. 中文相邻字符 bigram；
 * 4. 英文和数字连续保留；
 * 5. ASCII 英文转小写；
 * 6. 标点和空白作为 token 边界。
 */

class ChineseTextTokenizer final {
public:
    std::vector<std::string> tokenize(std::string_view text) const;

private:
    /*
     * 从 text[offset] 开始解析一个 UTF-8 字符。
     *
     * 成功：
     * - 返回 true；
     * - code_point 保存 Unicode 码点；
     * - offset 移动到下一个字符。
     *
     * 遇到非法 UTF-8：
     * - 返回 false；
     * - offset 至少向后移动一个字节；
     * - 调用方把非法字节视为分隔符。
     */
    static bool decodeNextUtf8(
        std::string_view text,      // 只查看调用方已有的字符串，不复制内容
        std::size_t& offset,
        char32_t& code_point
    );

    // 把 Unicode 码点重新编码为 UTF-8 字符串。
    static std::string encodeUtf8(char32_t code_point);

    static bool isAsciiAlphanumeric(char32_t code_point);
    static bool isCjkCharacter(char32_t code_point);
    
    static char toLowerAscii(char value);
};
