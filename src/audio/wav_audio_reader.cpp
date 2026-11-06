#include "wav_audio_reader.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t KPcmFormat = 1;
constexpr std::uint16_t KSupportedBits = 16;

/*
 * 防止损坏文件声明几 GB 的 data chunk，
 * 导致开发板尝试分配大量内存。
 */

constexpr std::uint32_t KMaximumDataBytes = 256U * 1024U * 1024U;

// RIFF, WAVE, fmt, data, LIST, JUNK 均为四字节块标识
std::string readFourCc(
    std::istream& input,
    const std::string& description
) {
    std::array<char, 4> value{};

    input.read(
        value.data(),
        static_cast<std::streamsize>(
            value.size()
        )
    );

    if (!input) {
        throw std::runtime_error(
            "truncated WAV while reading " +
            description
        );
    }

    return std::string(
        value.data(),
        value.size()
    );
}

// 小端
// 读取音频格式、声道数、每帧字节数、采样位数
std::uint16_t readUint16Le(
    std::istream& input,
    const std::string& description
) {
    std::array<unsigned char, 2> bytes{};

    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!input) {
        throw std::runtime_error(
            "truncated WAV while reading " +
            description
        );
    }

    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8U)
    );
}

// RIFF大小、chunk大小、采样率、每秒字节数
std::uint32_t readUint32Le(
    std::istream& input,
    const std::string& description
) {
    std::array<unsigned char, 4> bytes{};

    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!input) {
        throw std::runtime_error(
            "truncated WAV while reading " +
            description
        );
    }

    return (
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U)
    );
}

void skipBytes(
    std::istream& input,
    std::uint32_t count,
    const std::string& description
) {
    if (count == 0) {
        return;
    }

    input.ignore(
        static_cast<std::streamsize>(count)
    );

    if (input.gcount() != static_cast<std::streamsize>(count)) {
        throw std::runtime_error(
            "truncated WAV while skipping " +
            description
        );
    }
}

std::vector<std::uint8_t> readBytes(
    std::istream& input,
    std::uint32_t count,
    const std::string& description
) {
    if (count > KMaximumDataBytes) {
        throw std::runtime_error(
            "WAV data chunk is too large"
        );
    }

    std::vector<std::uint8_t> bytes(count);

    if (count == 0) {
        return bytes;
    }

    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(count)
    );

    if (!input) {
        throw std::runtime_error(
            "truncated WAV while reading " +
            description
        );
    }

    return bytes;
}

// 把两个小端字节转换成一个有符号PCM16采样值
std::int16_t decodeSample(
    std::uint8_t low,
    std::uint8_t high
) {
    const std::uint16_t unsigned_value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(low) |
        (static_cast<std::uint16_t>(high) << 8U)
    );

    /*
     * 显式完成二进制补码转换，
     * 避免依赖 unsigned → signed 溢出行为。
     */
    const int signed_value = 
        unsigned_value >= 0x8000U
            ? static_cast<int>(unsigned_value) - 0x10000
            : static_cast<int>(unsigned_value);

    return static_cast<std::int16_t>(signed_value);
}

struct WavFormat {
    bool found = false;

    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint32_t byte_rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits_per_sample = 0;
};

void validateFormat(const WavFormat& format) {
    if (!format.found) {
        throw std::runtime_error("WAV fmt chunk was not found");
    }

    if (format.audio_format != KPcmFormat) {
        throw std::runtime_error(
            "only PCM WAV format is supported"
        );
    }

    if (format.channels == 0) {
        throw std::runtime_error(
            "WAV channel count must be "
            "greater than zero"
        );
    }

    if (format.sample_rate == 0) {
        throw std::runtime_error(
            "WAV sample rate must be "
            "greater than zero"
        );
    }

    if (
        format.bits_per_sample !=
        KSupportedBits
    ) {
        throw std::runtime_error(
            "only 16-bit PCM WAV is supported"
        );
    }

    const std::uint16_t expected_align =
        static_cast<std::uint16_t>(
            format.channels *
            (
                format.bits_per_sample /
                8U
            )
        );

    if (
        format.block_align !=
        expected_align
    ) {
        throw std::runtime_error(
            "invalid WAV block alignment"
        );
    }

    const std::uint32_t expected_rate =
        format.sample_rate *
        format.block_align;

    if (
        format.byte_rate !=
        expected_rate
    ) {
        throw std::runtime_error(
            "invalid WAV byte rate"
        );
    }
}

}   // namespace

AudioBuffer WavAudioReader::read(const std::string& path) {
    if (path.empty()) {
        throw std::invalid_argument("WAV path must not be empty");
    }

    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        throw std::runtime_error(
            "failed to open WAV file: " +
            path
        );
    }

    const std::string riff = readFourCc(input, "RIFF header");

    if (riff != "RIFF") {
        throw std::runtime_error(
            "invalid WAV RIFF header"
        );
    }

    /*
     * 当前不依赖 RIFF size 定位 chunk，
     * 但仍需读取该字段。
     */
    const std::uint32_t riff_size = readUint32Le(input, "RIFF size");

    if (riff_size < 4U) {
        throw std::runtime_error(
            "invalid WAV RIFF size"
        );
    }

    const std::string wave = readFourCc(input, "WAVE header");
    if (wave != "WAVE") {
        throw std::runtime_error(
            "invalid WAV WAVE header"
        );
    }

    WavFormat format;

    bool data_found = false;
    std::vector<std::uint8_t> pcm_bytes;

    while (input) {
        std::array<char, 4> chunk_id_bytes{};

        input.read(
            chunk_id_bytes.data(),
            static_cast<std::streamsize>(chunk_id_bytes.size())
        );

        if (input.eof() && input.gcount() == 0) {
            break;
        }

        if (
            input.gcount() != static_cast<std::streamsize>(chunk_id_bytes.size())
        ) {
            throw std::runtime_error(
                "truncated WAV chunk header"
            );
        }

        const std::string chunk_id(
            chunk_id_bytes.data(),
            chunk_id_bytes.size()
        );

        const std::uint32_t chunk_size =
            readUint32Le(
                input,
                "chunk size"
            );

        if (chunk_id == "fmt ") {
            if (chunk_size < 16U) {
                throw std::runtime_error(
                    "WAV fmt chunk is too small"
                );
            }

            format.audio_format =
                readUint16Le(
                    input,
                    "audio format"
                );

            format.channels =
                readUint16Le(
                    input,
                    "channel count"
                );

            format.sample_rate =
                readUint32Le(
                    input,
                    "sample rate"
                );

            format.byte_rate =
                readUint32Le(
                    input,
                    "byte rate"
                );

            format.block_align =
                readUint16Le(
                    input,
                    "block alignment"
                );

            format.bits_per_sample =
                readUint16Le(
                    input,
                    "bits per sample"
                );

            format.found = true;

            skipBytes(
                input,
                chunk_size - 16U,
                "extra fmt data"
            );
        } else if (chunk_id == "data") {
            pcm_bytes = readBytes(
                input,
                chunk_size,
                "PCM data"
            );

            data_found = true;
        } else {
            /*
             * 忽略 LIST、JUNK、fact 等未知 chunk。
             */
            skipBytes(
                input,
                chunk_size,
                "unknown chunk " +
                chunk_id
            );
        }

        /*
         * RIFF chunk 使用偶数字节对齐。
         * 奇数大小 chunk 后面有一个 padding byte。
         */
        if ((chunk_size & 1U) != 0U) {
            skipBytes(
                input,
                1U,
                "chunk padding"
            );
        }

        if (format.found && data_found) {
            break;
        }
    }

    validateFormat(format);

    if (!data_found) {
        throw std::runtime_error(
            "WAV data chunk was not found"
        );
    }

    if (pcm_bytes.empty()) {
        throw std::runtime_error(
            "WAV PCM data is empty"
        );
    }

    if (
        pcm_bytes.size() %
            format.block_align != 0
    ) {
        throw std::runtime_error(
            "WAV data size is not aligned "
            "to complete audio frames"
        );
    }

    AudioBuffer audio;

    audio.sample_rate =
        static_cast<int>(
            format.sample_rate
        );

    audio.channels =
        static_cast<int>(
            format.channels
        );

    audio.samples.reserve(
        pcm_bytes.size() / 2U
    );

    for (
        std::size_t index = 0;
        index < pcm_bytes.size();
        index += 2U
    ) {
        audio.samples.push_back(
            decodeSample(
                pcm_bytes[index],
                pcm_bytes[index + 1U]
            )
        );
    }

    return audio;
}
