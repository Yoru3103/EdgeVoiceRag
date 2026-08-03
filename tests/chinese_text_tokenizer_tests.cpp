#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "chinese_text_tokenizer.h"

namespace {

int g_failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& test_name
) {
    if (condition) {
        std::cout
            << "[PASS] "
            << test_name
            << '\n';
    } else {
        std::cerr
            << "[FAIL] "
            << test_name
            << '\n';

        ++g_failed_count;
    }
}

void expectTokensEqual(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    const std::string& test_name
) {
    if (actual == expected) {
        std::cout
            << "[PASS] "
            << test_name
            << '\n';

        return;
    }

    std::cerr
        << "[FAIL] "
        << test_name
        << "\nexpected:";

    for (const std::string& token : expected) {
        std::cerr << " [" << token << ']';
    }

    std::cerr << "\nactual:";

    for (const std::string& token : actual) {
        std::cerr << " [" << token << ']';
    }

    std::cerr << '\n';
    ++g_failed_count;
}

void testChineseUnigramAndBigram() {
    const ChineseTextTokenizer tokenizer;

    const std::vector<std::string> tokens =
        tokenizer.tokenize("空调怎么打开");

    const std::vector<std::string> expected{
        "空",
        "调",
        "怎",
        "么",
        "打",
        "开",
        "空调",
        "调怎",
        "怎么",
        "么打",
        "打开"
    };

    expectTokensEqual(
        tokens,
        expected,
        "Chinese tokenizer: unigram and bigram"
    );
}

void testPunctuationCreatesBoundary() {
    const ChineseTextTokenizer tokenizer;

    const std::vector<std::string> tokens =
        tokenizer.tokenize("空调，打开。");

    const std::vector<std::string> expected{
        "空",
        "调",
        "空调",
        "打",
        "开",
        "打开"
    };

    expectTokensEqual(
        tokens,
        expected,
        "Chinese tokenizer: punctuation should create boundary"
    );

    expectTrue(
        std::find(
            tokens.begin(),
            tokens.end(),
            "调打"
        ) == tokens.end(),
        "Chinese tokenizer: should not create bigram across punctuation"
    );
}

void testAsciiNormalization() {
    const ChineseTextTokenizer tokenizer;

    const std::vector<std::string> tokens =
        tokenizer.tokenize("RK3576 ESP esp");

    const std::vector<std::string> expected{
        "rk3576",
        "esp",
        "esp"
    };

    expectTokensEqual(
        tokens,
        expected,
        "Chinese tokenizer: ASCII should be lowercase"
    );
}

void testMixedChineseAndAscii() {
    const ChineseTextTokenizer tokenizer;

    const std::vector<std::string> tokens =
        tokenizer.tokenize("开启ESP功能");

    const std::vector<std::string> expected{
        "开",
        "启",
        "开启",
        "esp",
        "功",
        "能",
        "功能"
    };

    expectTokensEqual(
        tokens,
        expected,
        "Chinese tokenizer: mixed Chinese and ASCII"
    );
}

void testRepeatedTokensArePreserved() {
    const ChineseTextTokenizer tokenizer;

    const std::vector<std::string> tokens =
        tokenizer.tokenize("空调空调");

    const auto air_count =
        std::count(
            tokens.begin(),
            tokens.end(),
            "空"
        );

    const auto conditioner_count =
        std::count(
            tokens.begin(),
            tokens.end(),
            "调"
        );

    expectTrue(
        air_count == 2,
        "Chinese tokenizer: repeated first character should be preserved"
    );

    expectTrue(
        conditioner_count == 2,
        "Chinese tokenizer: repeated second character should be preserved"
    );
}

void testEmptyInput() {
    const ChineseTextTokenizer tokenizer;

    expectTrue(
        tokenizer.tokenize("").empty(),
        "Chinese tokenizer: empty text"
    );

    expectTrue(
        tokenizer.tokenize(" ，。！？ ").empty(),
        "Chinese tokenizer: punctuation-only text"
    );
}

void testInvalidUtf8DoesNotCrash() {
    const ChineseTextTokenizer tokenizer;

    /*
     * 0xE4 0xB8 是一个不完整的三字节 UTF-8 序列。
     */
    const std::string invalid_text{
        static_cast<char>(0xE4),
        static_cast<char>(0xB8)
    };

    const std::vector<std::string> tokens =
        tokenizer.tokenize(invalid_text);

    expectTrue(
        tokens.empty(),
        "Chinese tokenizer: invalid UTF-8 should not crash"
    );
}

} // namespace

int main() {
    testChineseUnigramAndBigram();
    testPunctuationCreatesBoundary();
    testAsciiNormalization();
    testMixedChineseAndAscii();
    testRepeatedTokensArePreserved();
    testEmptyInput();
    testInvalidUtf8DoesNotCrash();

    if (g_failed_count == 0) {
        std::cout
            << "All Chinese tokenizer tests passed."
            << '\n';

        return 0;
    }

    std::cerr
        << g_failed_count
        << " Chinese tokenizer test(s) failed."
        << '\n';

    return 1;
}