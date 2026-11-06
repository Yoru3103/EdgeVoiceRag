#include "file_fingerprint.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

FileFingerprintResult calculateFileFnv1a64(const std::string& path) {
    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        FileFingerprintResult result;
        result.error = "failed to open file for fingerprint: " + path;

        return result;
    }

    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;

    std::uint64_t fingerprint = offset_basis;

    std::array<char, 8192> buffer{};

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

        const std::streamsize count = input.gcount();

        for (
            std::streamsize index = 0;
            index < count;
            index++
        ) {
            const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);

            fingerprint ^= static_cast<std::uint64_t>(byte);

            fingerprint *= prime;
        }
    }

    if (input.bad()) {
        FileFingerprintResult result;
        result.error = "failed while reading file for "
            "fingerprint: "
            + path;

        return result;
    }

    std::ostringstream encoded;

    encoded
        << std::hex
        << std::nouppercase
        << std::setfill('0')
        << std::setw(16)
        << fingerprint;

    FileFingerprintResult result;
    result.ok = true;
    result.fingerprint = encoded.str();

    return result;
}
