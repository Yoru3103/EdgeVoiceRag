#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "streaming_sentence_buffer.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

void testSingleCompleteSentence() {
    StreamingSentenceBuffer buffer;

    const auto sentences = buffer.append("请检查车辆胎压。");

    expectTrue(
        sentences.size() == 1,
        "emit one complete sentence"
    );
    expectTrue(
        sentences[0] == "请检查车辆胎压。",
        "preserve complete sentence"
    );
    expectTrue(
        buffer.empty(),
        "buffer empty after complete sentence"
    );
}

void testSentenceAcrossChunks() {
    StreamingSentenceBuffer buffer;

    const auto first = buffer.append("请检查车");

    expectTrue(
        first.empty(),
        "incomplete chunk emits nothing"
    );

    const auto second = buffer.append("辆胎压。");

    expectTrue(
        second.size() == 1,
        "fragmented sentence is emitted"
    );
    expectTrue(
        second[0] == "请检查车辆胎压。",
        "fragmented sentence is assembled"
    );
}

void testMultipleSentencesInOneChunk() {
    StreamingSentenceBuffer buffer;

    const auto sentences = buffer.append("请检查胎压。然后重新启动车辆！");

    expectTrue(
        sentences.size() == 2,
        "emit multiple sentences"
    );
    expectTrue(
        sentences[0] == "请检查胎压。",
        "first sentence is correct"
    );
    expectTrue(
        sentences[1] ==
            "然后重新启动车辆！",
        "second sentence is correct"
    );
}

void testPreserveIncompleteRemainder() {
    StreamingSentenceBuffer buffer;

    const auto sentences =
        buffer.append(
            "请检查胎压。然后重新启动"
        );

    expectTrue(
        sentences.size() == 1,
        "emit first complete sentence"
    );

    expectTrue(
        buffer.pendingText() ==
            "然后重新启动",
        "preserve incomplete remainder"
    );
}

void testFlushRemainder() {
    StreamingSentenceBuffer buffer;

    buffer.append("请联系服务中心");

    const std::string remaining =
        buffer.flush();

    expectTrue(
        remaining == "请联系服务中心",
        "flush returns remaining text"
    );

    expectTrue(
        buffer.empty(),
        "flush clears buffer"
    );
}

void testClearAfterBargeIn() {
    StreamingSentenceBuffer buffer;

    buffer.append("旧回答还没有完成");

    buffer.clear();

    expectTrue(
        buffer.empty(),
        "clear removes interrupted answer"
    );

    const auto sentences =
        buffer.append("这是新回答。");

    expectTrue(
        sentences.size() == 1,
        "new answer works after clear"
    );

    expectTrue(
        sentences[0] == "这是新回答。",
        "old and new answers are not mixed"
    );
}

void testAsciiDelimiters() {
    StreamingSentenceBuffer buffer;

    const auto sentences =
        buffer.append(
            "Check the tire pressure!Is it normal?"
        );

    expectTrue(
        sentences.size() == 2,
        "support ASCII delimiters"
    );

    expectTrue(
        sentences[0] ==
            "Check the tire pressure!",
        "preserve ASCII exclamation"
    );

    expectTrue(
        sentences[1] ==
            "Is it normal?",
        "preserve ASCII question mark"
    );
}

void testNewlineDelimiter() {
    StreamingSentenceBuffer buffer;

    const auto sentences =
        buffer.append(
            "第一项：检查胎压\n第二项：检查机油\n"
        );

    expectTrue(
        sentences.size() == 2,
        "newline separates sentences"
    );

    expectTrue(
        sentences[0] ==
            "第一项：检查胎压",
        "trim first newline"
    );

    expectTrue(
        sentences[1] ==
            "第二项：检查机油",
        "trim second newline"
    );
}

void testSplitUtf8Delimiter() {
    StreamingSentenceBuffer buffer;

    const std::string delimiter = "。";

    const std::string first_part =
        std::string("检查胎压") +
        delimiter.substr(0, 1);

    const std::string second_part =
        delimiter.substr(1);

    const auto first =
        buffer.append(first_part);

    expectTrue(
        first.empty(),
        "partial UTF-8 delimiter emits nothing"
    );

    const auto second =
        buffer.append(second_part);

    expectTrue(
        second.size() == 1,
        "split UTF-8 delimiter is reconstructed"
    );

    expectTrue(
        second[0] == "检查胎压。",
        "reconstructed delimiter is preserved"
    );
}

void testWhitespaceIsTrimmed() {
    StreamingSentenceBuffer buffer;

    const auto sentences =
        buffer.append(
            "  请检查胎压。  下一步检查机油。  "
        );

    expectTrue(
        sentences.size() == 2,
        "whitespace input emits two sentences"
    );

    expectTrue(
        sentences[0] == "请检查胎压。",
        "trim leading whitespace"
    );

    expectTrue(
        sentences[1] == "下一步检查机油。",
        "trim whitespace between sentences"
    );

    expectTrue(
        buffer.pendingText() == "  ",
        "trailing incomplete whitespace remains"
    );

    expectTrue(
        buffer.flush().empty(),
        "flush discards whitespace-only remainder"
    );
}

void testEmptyDelta() {
    StreamingSentenceBuffer buffer;

    const auto sentences =
        buffer.append("");

    expectTrue(
        sentences.empty(),
        "empty delta emits nothing"
    );

    expectTrue(
        buffer.empty(),
        "empty delta does not change buffer"
    );
}

void testCustomDelimiters() {
    StreamingSentenceBuffer buffer(
        std::vector<std::string>{"<END>"}
    );

    const auto sentences =
        buffer.append(
            "第一段<END>第二段"
        );

    expectTrue(
        sentences.size() == 1,
        "custom delimiter emits sentence"
    );

    expectTrue(
        sentences[0] == "第一段<END>",
        "custom delimiter is preserved"
    );

    expectTrue(
        buffer.pendingText() == "第二段",
        "custom delimiter preserves remainder"
    );
}

void testRejectEmptyDelimiterList() {
    bool rejected = false;

    try {
        StreamingSentenceBuffer buffer(
            std::vector<std::string>{}
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject empty delimiter list"
    );
}

void testRejectEmptyDelimiter() {
    bool rejected = false;

    try {
        StreamingSentenceBuffer buffer(
            std::vector<std::string>{
                "。",
                ""
            }
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject empty delimiter"
    );
}

}   // namespace

int main() {
    testSingleCompleteSentence();
    testSentenceAcrossChunks();
    testMultipleSentencesInOneChunk();
    testPreserveIncompleteRemainder();
    testFlushRemainder();
    testClearAfterBargeIn();
    testAsciiDelimiters();
    testNewlineDelimiter();
    testSplitUtf8Delimiter();
    testWhitespaceIsTrimmed();
    testEmptyDelta();
    testCustomDelimiters();
    testRejectEmptyDelimiterList();
    testRejectEmptyDelimiter();

    if (failed_count == 0) {
        std::cout
            << "\nAll streaming sentence buffer "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
