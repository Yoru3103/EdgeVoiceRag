#include "file_fingerprint.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

class TemporaryFile final {
public:
    TemporaryFile() {
        const auto unique_value =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();

        path_ =
            fs::temp_directory_path()
            / (
                "edge_voice_rag_fingerprint_"
                + std::to_string(unique_value)
                + ".bin"
            );
    }

    ~TemporaryFile() {
        std::error_code error;
        fs::remove(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(
    const fs::path& path,
    const std::string& content
) {
    std::ofstream output(
        path,
        std::ios::binary
    );

    if (!output.is_open()) {
        throw std::runtime_error(
            "failed to create temporary file"
        );
    }

    output.write(
        content.data(),
        static_cast<std::streamsize>(
            content.size()
        )
    );
}

void testKnownFingerprint() {
    TemporaryFile file;

    writeFile(
        file.path(),
        "abc"
    );

    const FileFingerprintResult result =
        calculateFileFnv1a64(
            file.path().string()
        );

    expect(
        result.ok,
        "fingerprint calculation failed"
    );

    /*
     * FNV-1a 64-bit("abc") 标准测试值。
     */
    expect(
        result.fingerprint ==
            "e71fa2190541574b",
        "unexpected FNV-1a fingerprint"
    );
}

void testContentChange() {
    TemporaryFile file;

    writeFile(
        file.path(),
        "original"
    );

    const FileFingerprintResult first =
        calculateFileFnv1a64(
            file.path().string()
        );

    writeFile(
        file.path(),
        "changed"
    );

    const FileFingerprintResult second =
        calculateFileFnv1a64(
            file.path().string()
        );

    expect(
        first.ok && second.ok,
        "fingerprint calculation failed"
    );

    expect(
        first.fingerprint
            != second.fingerprint,
        "changed content should change "
        "fingerprint"
    );
}

void testMissingFile() {
    const FileFingerprintResult result =
        calculateFileFnv1a64(
            "/tmp/edge_voice_rag_missing_file"
        );

    expect(
        !result.ok,
        "missing file should fail"
    );

    expect(
        !result.error.empty(),
        "missing file error should not be empty"
    );
}

}  // namespace

int main() {
    try {
        testKnownFingerprint();
        testContentChange();
        testMissingFile();

        std::cout
            << "file_fingerprint_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "file_fingerprint_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
