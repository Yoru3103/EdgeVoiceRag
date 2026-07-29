#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>
#include <utility>

#include "wav_audio_reader.h"
#include "wav_audio_recorder.h"

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

template <typename Exception, typename Function>
void expectThrows(
    Function function,
    const std::string& name
) {
    try {
        function();
    } catch (const Exception&) {
        std::cout << "[PASS] " << name << '\n';
        return;
    } catch (...) {
        std::cout
            << "[FAIL] "
            << name
            << " threw wrong exception\n";

        ++failed_count;
        return;
    }

    std::cout
        << "[FAIL] "
        << name
        << " did not throw\n";

    ++failed_count;
}

class TemporaryFile {
public:
    explicit TemporaryFile(
        std::string path
    )
        : path_(std::move(path)) {
    }

    ~TemporaryFile() {
        std::remove(path_.c_str());
    }

    const std::string& path() const {
        return path_;
    }

private:
    std::string path_;
};

void writeUint16Le(
    std::ostream& output,
    std::uint16_t value
) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>(
            (value >> 8U) & 0xFFU
        )
    };

    output.write(bytes, 2);
}

void writeUint32Le(
    std::ostream& output,
    std::uint32_t value
) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>(
            (value >> 8U) & 0xFFU
        ),
        static_cast<char>(
            (value >> 16U) & 0xFFU
        ),
        static_cast<char>(
            (value >> 24U) & 0xFFU
        )
    };

    output.write(bytes, 4);
}

std::vector<std::uint8_t> encodeSamples(
    const std::vector<std::int16_t>& samples
) {
    std::vector<std::uint8_t> bytes;

    bytes.reserve(samples.size() * 2U);

    for (const std::int16_t sample : samples) {
        const int signed_value =
            static_cast<int>(sample);

        const std::uint16_t value =
            signed_value < 0
                ? static_cast<std::uint16_t>(
                      signed_value + 0x10000
                  )
                : static_cast<std::uint16_t>(
                      signed_value
                  );

        bytes.push_back(
            static_cast<std::uint8_t>(
                value & 0xFFU
            )
        );

        bytes.push_back(
            static_cast<std::uint8_t>(
                (value >> 8U) & 0xFFU
            )
        );
    }

    return bytes;
}

void writePcmWav(
    const std::string& path,
    std::uint16_t channels,
    std::uint32_t sample_rate,
    std::uint16_t bits_per_sample,
    const std::vector<std::uint8_t>& data
) {
    std::ofstream output(
        path,
        std::ios::binary
    );

    if (!output.is_open()) {
        throw std::runtime_error(
            "failed to create test WAV"
        );
    }

    const std::uint16_t block_align =
        static_cast<std::uint16_t>(
            channels *
            (bits_per_sample / 8U)
        );

    const std::uint32_t byte_rate =
        sample_rate * block_align;

    const std::uint32_t padding =
        data.size() % 2U == 0U ? 0U : 1U;

    const std::uint32_t riff_size =
        36U +
        static_cast<std::uint32_t>(
            data.size()
        ) +
        padding;

    output.write("RIFF", 4);
    writeUint32Le(output, riff_size);
    output.write("WAVE", 4);

    output.write("fmt ", 4);
    writeUint32Le(output, 16U);
    writeUint16Le(output, 1U);
    writeUint16Le(output, channels);
    writeUint32Le(output, sample_rate);
    writeUint32Le(output, byte_rate);
    writeUint16Le(output, block_align);
    writeUint16Le(
        output,
        bits_per_sample
    );

    output.write("data", 4);

    writeUint32Le(
        output,
        static_cast<std::uint32_t>(
            data.size()
        )
    );

    if (!data.empty()) {
        output.write(
            reinterpret_cast<const char*>(
                data.data()
            ),
            static_cast<std::streamsize>(
                data.size()
            )
        );
    }

    if (padding != 0U) {
        output.put('\0');
    }
}

std::string makeTemporaryPath(
    const std::string& name
) {
    return (
        "/tmp/edge_voice_rag_" +
        name +
        "_" +
        std::to_string(getpid()) +
        ".wav"
    );
}

void testReadMonoPcm16() {
    TemporaryFile file(
        makeTemporaryPath("mono")
    );

    const std::vector<std::int16_t>
        expected_samples = {
            0,
            1000,
            -1000,
            32767,
            -32768
        };

    writePcmWav(
        file.path(),
        1,
        16000,
        16,
        encodeSamples(expected_samples)
    );

    const AudioBuffer audio =
        WavAudioReader::read(
            file.path()
        );

    expectTrue(
        audio.sample_rate == 16000,
        "read WAV sample rate"
    );

    expectTrue(
        audio.channels == 1,
        "read WAV channel count"
    );

    expectTrue(
        audio.samples == expected_samples,
        "decode signed PCM samples"
    );
}

void testReadStereoPcm16() {
    TemporaryFile file(
        makeTemporaryPath("stereo")
    );

    const std::vector<std::int16_t>
        expected_samples = {
            100,
            -100,
            200,
            -200
        };

    writePcmWav(
        file.path(),
        2,
        48000,
        16,
        encodeSamples(expected_samples)
    );

    const AudioBuffer audio =
        WavAudioReader::read(
            file.path()
        );

    expectTrue(
        audio.channels == 2,
        "read stereo WAV"
    );

    expectTrue(
        audio.frameCount() == 2,
        "calculate stereo frame count"
    );

    expectTrue(
        audio.samples == expected_samples,
        "preserve interleaved stereo samples"
    );
}

void testRejectUnsupportedBitDepth() {
    TemporaryFile file(
        makeTemporaryPath("pcm8")
    );

    writePcmWav(
        file.path(),
        1,
        16000,
        8,
        std::vector<std::uint8_t>{
            0,
            128,
            255,
            128
        }
    );

    expectThrows<std::runtime_error>(
        [&file]() {
            WavAudioReader::read(
                file.path()
            );
        },
        "reject non-16-bit WAV"
    );
}

void testRejectInvalidHeader() {
    TemporaryFile file(
        makeTemporaryPath(
            "invalid_header"
        )
    );

    {
        std::ofstream output(
            file.path(),
            std::ios::binary
        );

        output.write("NOTWAVE", 7);
    }

    expectThrows<std::runtime_error>(
        [&file]() {
            WavAudioReader::read(
                file.path()
            );
        },
        "reject invalid WAV header"
    );
}

void testRejectMissingFile() {
    expectThrows<std::runtime_error>(
        []() {
            WavAudioReader::read(
                "/tmp/"
                "edge_voice_rag_missing.wav"
            );
        },
        "reject missing WAV file"
    );
}

void testWavRecorder() {
    TemporaryFile file(
        makeTemporaryPath("recorder")
    );

    const std::vector<std::int16_t>
        samples = {
            10,
            20,
            30
        };

    writePcmWav(
        file.path(),
        1,
        16000,
        16,
        encodeSamples(samples)
    );

    WavAudioRecorder recorder(
        file.path()
    );

    const AudioCaptureResult result =
        recorder.recordUtterance();

    expectTrue(
        result.ok,
        "WAV recorder succeeds"
    );

    expectTrue(
        result.audio.samples == samples,
        "WAV recorder returns PCM"
    );

    expectTrue(
        recorder.name() == "wav_file",
        "WAV recorder exposes backend name"
    );
}

void testStoppedRecorderFails() {
    TemporaryFile file(
        makeTemporaryPath(
            "stopped_recorder"
        )
    );

    writePcmWav(
        file.path(),
        1,
        16000,
        16,
        encodeSamples({1, 2, 3})
    );

    WavAudioRecorder recorder(
        file.path()
    );

    recorder.stop();

    const auto result =
        recorder.recordUtterance();

    expectTrue(
        !result.ok,
        "stopped recorder rejects read"
    );

    expectTrue(
        result.error.find("stopped") !=
            std::string::npos,
        "stopped recorder reports reason"
    );
}

}  // namespace

int main() {
    testReadMonoPcm16();
    testReadStereoPcm16();
    testRejectUnsupportedBitDepth();
    testRejectInvalidHeader();
    testRejectMissingFile();
    testWavRecorder();
    testStoppedRecorderFails();

    if (failed_count == 0) {
        std::cout
            << "\nAll WAV audio reader "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}