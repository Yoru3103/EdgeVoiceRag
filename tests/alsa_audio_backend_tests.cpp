#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "alsa_audio_player.h"
#include "alsa_audio_recorder.h"

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

void testInvalidPlayerConfig() {
    AlsaAudioPlayerConfig config;
    config.device = "";

    bool threw = false;

    try {
        AlsaAudioPlayer player(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expectTrue(
        threw,
        "reject empty playback device"
    );
}

void testInvalidRecorderConfig() {
    AlsaAudioRecorderConfig config;
    config.sample_rate = 0;

    bool threw = false;

    try {
        AlsaAudioRecorder recorder(config);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expectTrue(
        threw,
        "reject invalid capture sample rate"
    );
}

void testNullPlayback() {
    AlsaAudioPlayerConfig config;
    config.device = "null";
    config.period_frames = 64;

    AlsaAudioPlayer player(config);

    AudioBuffer audio;
    audio.sample_rate = 16000;
    audio.channels = 1;

    /*
     * 10 ms 静音。
     */
    audio.samples.resize(
        160,
        static_cast<std::int16_t>(0)
    );

    const AudioPlaybackResult result =
        player.play(audio);

    expectTrue(
        result.ok,
        "play PCM through ALSA null device"
    );
}

void testEmptyPlaybackRejected() {
    AlsaAudioPlayerConfig config;
    config.device = "null";

    AlsaAudioPlayer player(config);

    AudioBuffer audio;
    audio.sample_rate = 16000;
    audio.channels = 1;

    const AudioPlaybackResult result =
        player.play(audio);

    expectTrue(
        !result.ok,
        "reject empty playback audio"
    );
}

void testNullCapture() {
    AlsaAudioRecorderConfig config;
    config.device = "null";
    config.sample_rate = 16000;
    config.channels = 1;
    config.record_duration_ms = 20;
    config.period_frames = 64;

    AlsaAudioRecorder recorder(config);

    const AudioCaptureResult result =
        recorder.recordUtterance();

    expectTrue(
        result.ok,
        "capture PCM through ALSA null device"
    );

    if (!result.ok) {
        std::cout
            << "Capture error: "
            << result.error
            << '\n';

        return;
    }

    expectTrue(
        result.audio.sample_rate == 16000,
        "preserve capture sample rate"
    );

    expectTrue(
        result.audio.channels == 1,
        "preserve capture channel count"
    );

    expectTrue(
        result.audio.frameCount() == 320,
        "capture requested number of frames"
    );
}

}  // namespace

int main() {
    testInvalidPlayerConfig();
    testInvalidRecorderConfig();
    testNullPlayback();
    testEmptyPlaybackRejected();
    testNullCapture();

    if (failed_count == 0) {
        std::cout
            << "\nAll ALSA audio backend "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}