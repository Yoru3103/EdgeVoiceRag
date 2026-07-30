#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sherpa_onnx_tts_backend.h"

namespace {

namespace fs = std::filesystem;

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

SherpaOnnxTtsConfig makeConfig(
    const fs::path& model_dir
) {
    SherpaOnnxTtsConfig config;

    /*
     * 板端优先使用 int8 模型，降低内存和 CPU 开销。
     */
    config.model_path =
        (model_dir / "model.int8.onnx").string();

    config.lexicon_path =
        (model_dir / "lexicon.txt").string();

    config.tokens_path =
        (model_dir / "tokens.txt").string();

    config.rule_fsts = {
        (model_dir / "date.fst").string(),
        (model_dir / "number.fst").string()
    };

    config.num_threads = 2;
    config.speaker_id = 0;
    config.speed = 1.0F;
    config.provider = "cpu";

    return config;
}

void testInvalidThreadCount() {
    SherpaOnnxTtsConfig config;
    config.num_threads = 0;

    bool threw = false;

    try {
        SherpaOnnxTtsBackend backend(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expectTrue(
        threw,
        "reject non-positive thread count"
    );
}

void testMissingModelFiles() {
    SherpaOnnxTtsConfig config;

    config.model_path =
        "missing/model.onnx";
    config.lexicon_path =
        "missing/lexicon.txt";
    config.tokens_path =
        "missing/tokens.txt";
    config.num_threads = 1;

    bool threw = false;

    try {
        SherpaOnnxTtsBackend backend(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expectTrue(
        threw,
        "reject missing model files"
    );
}

void testRealSynthesis(
    const fs::path& model_dir
) {
    SherpaOnnxTtsBackend backend(
        makeConfig(model_dir)
    );

    expectTrue(
        backend.name() ==
            "sherpa_onnx_vits_tts",
        "report TTS backend name"
    );

    const TtsSynthesisResult result =
        backend.synthesize(
            "胎压正常，请安全驾驶。"
        );

    expectTrue(
        result.ok,
        "synthesize Chinese speech"
    );

    if (!result.ok) {
        std::cout
            << "TTS error: "
            << result.error
            << '\n';

        return;
    }

    expectTrue(
        !result.audio.empty(),
        "generated audio is not empty"
    );

    expectTrue(
        result.audio.sample_rate > 0,
        "generated sample rate is valid"
    );

    expectTrue(
        result.audio.channels == 1,
        "generated audio is mono"
    );

    expectTrue(
        result.audio.durationSeconds() > 0.0,
        "generated audio duration is positive"
    );

    std::cout
        << "sample_rate="
        << result.audio.sample_rate
        << ", samples="
        << result.audio.samples.size()
        << ", duration="
        << result.audio.durationSeconds()
        << " seconds\n";
}

void testEmptyText(
    const fs::path& model_dir
) {
    SherpaOnnxTtsBackend backend(
        makeConfig(model_dir)
    );

    const TtsSynthesisResult result =
        backend.synthesize("   ");

    expectTrue(
        !result.ok,
        "reject empty TTS text"
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <tts-model-dir>\n";

        return 2;
    }

    testInvalidThreadCount();
    testMissingModelFiles();
    testRealSynthesis(argv[1]);
    testEmptyText(argv[1]);

    if (failed_count == 0) {
        std::cout
            << "\nAll sherpa-onnx TTS backend "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}