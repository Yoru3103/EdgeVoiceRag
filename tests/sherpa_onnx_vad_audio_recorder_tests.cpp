#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include "pcm_stream_source.h"
#include "sherpa_onnx_vad_audio_recorder.h"
#include "wav_audio_reader.h"

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

class MemoryPcmStreamSource final
    : public PcmStreamSource {
public:
    explicit MemoryPcmStreamSource(
        AudioBuffer audio,
        std::size_t chunk_frames = 512
    )
        : audio_(std::move(audio))
        , chunk_frames_(chunk_frames) {
    }

    std::string name() const override {
        return "memory_pcm_stream";
    }

    int sampleRate() const override {
        return audio_.sample_rate;
    }

    int channels() const override {
        return audio_.channels;
    }

    PcmStreamResult capture(
        const PcmChunkHandler& handler
    ) override {
        if (!handler) {
            return PcmStreamResult::failure(
                "handler is empty"
            );
        }

        const std::size_t channels =
            static_cast<std::size_t>(
                audio_.channels
            );

        std::size_t offset_frames = 0;

        while (
            !stopped_
            && offset_frames < audio_.frameCount()
        ) {
            const std::size_t frames =
                std::min(
                    chunk_frames_,
                    audio_.frameCount()
                        - offset_frames
                );

            AudioBuffer chunk;
            chunk.sample_rate =
                audio_.sample_rate;
            chunk.channels =
                audio_.channels;

            const std::size_t begin_sample =
                offset_frames * channels;

            const std::size_t end_sample =
                begin_sample + frames * channels;

            chunk.samples.assign(
                audio_.samples.begin()
                    + static_cast<std::ptrdiff_t>(
                        begin_sample
                    ),
                audio_.samples.begin()
                    + static_cast<std::ptrdiff_t>(
                        end_sample
                    )
            );

            offset_frames += frames;

            if (!handler(chunk)) {
                break;
            }
        }

        return PcmStreamResult::success(
            offset_frames
        );
    }

    void stop() override {
        stopped_ = true;
    }

private:
    AudioBuffer audio_;
    std::size_t chunk_frames_;
    bool stopped_ = false;
};

SherpaOnnxVadConfig makeVadConfig(
    const std::string& model_path
) {
    SherpaOnnxVadConfig config;

    config.model_path = model_path;
    config.sample_rate = 16000;
    config.window_size = 512;
    config.threshold = 0.25F;
    config.min_silence_duration = 0.8F;
    config.min_speech_duration = 0.25F;
    config.max_speech_duration = 15.0F;
    config.max_wait_seconds = 10.0F;
    config.num_threads = 1;
    config.provider = "cpu";

    return config;
}

void testSpeechIsDetected(
    const std::string& model_path
) {
    AudioBuffer input =
        WavAudioReader::read(
            "voice_input/air_conditioner.wav"
        );

    MemoryPcmStreamSource source(
        std::move(input)
    );

    SherpaOnnxVadAudioRecorder recorder(
        source,
        makeVadConfig(model_path)
    );

    const AudioCaptureResult result =
        recorder.recordUtterance();

    expectTrue(
        result.ok,
        "detect speech from WAV input"
    );

    if (!result.ok) {
        std::cout
            << "VAD error: "
            << result.error
            << '\n';

        return;
    }

    expectTrue(
        !result.audio.empty(),
        "VAD returns speech samples"
    );

    expectTrue(
        result.audio.sample_rate == 16000,
        "VAD preserves sample rate"
    );

    expectTrue(
        result.audio.channels == 1,
        "VAD returns mono audio"
    );

    expectTrue(
        result.audio.durationSeconds() >= 0.25,
        "VAD speech satisfies minimum duration"
    );
}

void testSilenceTimesOut(
    const std::string& model_path
) {
    AudioBuffer silence;
    silence.sample_rate = 16000;
    silence.channels = 1;

    /*
     * 2 秒静音。
     */
    silence.samples.resize(
        16000 * 2,
        static_cast<std::int16_t>(0)
    );

    MemoryPcmStreamSource source(
        std::move(silence)
    );

    SherpaOnnxVadConfig config =
        makeVadConfig(model_path);

    config.max_wait_seconds = 1.0F;

    SherpaOnnxVadAudioRecorder recorder(
        source,
        config
    );

    const AudioCaptureResult result =
        recorder.recordUtterance();

    expectTrue(
        !result.ok,
        "silence does not produce utterance"
    );

    expectTrue(
        result.error.find("no speech detected")
            != std::string::npos,
        "silence reports wait timeout"
    );
}

void testWrongSourceFormatIsRejected(
    const std::string& model_path
) {
    AudioBuffer input;
    input.sample_rate = 8000;
    input.channels = 1;
    input.samples.resize(8000);

    MemoryPcmStreamSource source(
        std::move(input)
    );

    bool threw = false;

    try {
        SherpaOnnxVadAudioRecorder recorder(
            source,
            makeVadConfig(model_path)
        );
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expectTrue(
        threw,
        "reject non-16000 Hz PCM source"
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <silero-vad-model>\n";

        return 2;
    }

    const std::string model_path = argv[1];

    testSpeechIsDetected(model_path);
    testSilenceTimesOut(model_path);
    testWrongSourceFormatIsRejected(
        model_path
    );

    if (failed_count == 0) {
        std::cout
            << "\nAll sherpa-onnx VAD recorder "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
