#include <iostream>
#include <stdexcept>
#include <string>

#include "sherpa_onnx_asr_backend.h"

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

void testRejectInvalidThreadCount() {
    SherpaOnnxAsrConfig config;

    config.num_threads = 0;

    bool rejected = false;

    try {
        SherpaOnnxAsrBackend backend(
            config
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject invalid ASR thread count"
    );
}

void testRejectMissingModel() {
    SherpaOnnxAsrConfig config;

    config.model_path =
        "/tmp/missing-sense-voice.onnx";

    config.tokens_path =
        "/tmp/missing-tokens.txt";

    bool rejected = false;

    try {
        SherpaOnnxAsrBackend backend(
            config
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject missing ASR model"
    );
}

void testRejectEmptyLanguage() {
    SherpaOnnxAsrConfig config;

    config.language = "";
    config.num_threads = 1;

    bool rejected = false;

    try {
        SherpaOnnxAsrBackend backend(
            config
        );
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject empty ASR language"
    );
}

}

int main() {
    testRejectInvalidThreadCount();
    testRejectMissingModel();
    testRejectEmptyLanguage();

    if (failed_count == 0) {
        std::cout
            << "\nAll sherpa ASR backend "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
