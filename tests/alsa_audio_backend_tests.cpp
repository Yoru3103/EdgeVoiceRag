#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <alsa/asoundlib.h>
#include <iostream>
#include <stdexcept>
#include <string>

#include "alsa_audio_player.h"
#include "alsa_audio_recorder.h"
#include "alsa_pcm_stream_source.h"

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
    // 同一handle重复提交不同格式，覆盖分句播放时的重新配置。
    audio.sample_rate = 44100;
    audio.samples.assign(441, 0);
    expectTrue(player.play(audio).ok, "reuse null player at TTS-like 44100 Hz mono");
    audio.sample_rate = 48000;
    audio.channels = 2;
    audio.samples.assign(960, 0);
    expectTrue(player.play(audio).ok, "reuse null player at 48000 Hz stereo");
    player.stop();
    audio.sample_rate = 16000;
    audio.channels = 1;
    audio.samples.assign(160, 0);
    expectTrue(player.play(audio).ok, "new playback succeeds after explicit stop");
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
    expectTrue(
        result.error.find("device=null") != std::string::npos
            && result.error.find("sample_rate=16000") != std::string::npos
            && result.error.find("period_frames_requested=1024") != std::string::npos,
        "playback failure reports the requested device and audio parameters"
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

// 使用真实项目profile，仅将物理端替换成file/null；不访问真实声卡。
struct ScopedFixedRateConfig {
    std::filesystem::path root;
    bool had_previous = false;
    std::string previous;

    ScopedFixedRateConfig() {
        char pattern[] = "/tmp/edge-alsa-rate-XXXXXX";
        const char* directory = ::mkdtemp(pattern);
        if (!directory) throw std::runtime_error("cannot create ALSA test directory");
        root = directory;
        if (const char* value = std::getenv("ALSA_CONFIG_PATH")) {
            had_previous = true;
            previous = value;
        }
        const auto project = std::filesystem::path(__FILE__).parent_path().parent_path();
        std::ofstream config(root / "alsa.conf");
        config << "<" << (project / "config/alsa-rk3588.conf").string() << ">\n"
               << "pcm.!edge_voice_hw { type asym "
               << "playback.pcm { type file slave.pcm \"null\" file \""
               << (root / "converted.raw").string() << "\" format \"raw\" } "
               << "capture.pcm \"null\" }\n";
        config.close();
        if (::setenv("ALSA_CONFIG_PATH", (root / "alsa.conf").c_str(), 1) != 0)
            throw std::runtime_error("cannot set test ALSA_CONFIG_PATH");
        snd_config_update_free_global();
    }

    ~ScopedFixedRateConfig() {
        snd_config_update_free_global();
        if (had_previous) ::setenv("ALSA_CONFIG_PATH", previous.c_str(), 1);
        else ::unsetenv("ALSA_CONFIG_PATH");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

void testFixedHardwareRateWithCaptureConfigured() {
    for (const int source_rate : {8000, 44100}) {
        ScopedFixedRateConfig fixture;
        bool played = false;
        AudioBuffer audio;
        audio.sample_rate = source_rate;
        audio.channels = 1;
        audio.samples.resize(static_cast<std::size_t>(source_rate));
        for (std::size_t i = 0; i < audio.samples.size(); ++i)
            audio.samples[i] = static_cast<std::int16_t>(
                std::lround(3000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * i / source_rate))
            );
        {
            AlsaPcmStreamSourceConfig capture;
            capture.device = "edge_voice_16k";
            capture.sample_rate = 16000;
            AlsaPcmStreamSource source(capture); // 保持采集handle及16k参数存活
            AlsaAudioPlayerConfig playback;
            playback.device = "edge_voice_16k";
            AlsaAudioPlayer player(playback);
            const auto result = player.play(audio);
            played = result.ok;
            if (!played) std::cout << "Fixed-rate error: " << result.error << '\n';
        }
        expectTrue(played, std::to_string(source_rate) + " Hz source plays with 16000 Hz capture configured");
        expectTrue(
            audio.sample_rate == source_rate && audio.samples.size() == static_cast<std::size_t>(source_rate),
            "ALSA conversion leaves the source PCM unchanged"
        );
        const auto path = fixture.root / "converted.raw";
        const auto frames = std::filesystem::exists(path)
            ? std::filesystem::file_size(path) / sizeof(std::int16_t) : 0;
        // ALSA线性重采样有少量尾部滤波延迟；断言硬件端约1秒16k，不能仍为44100帧。
        expectTrue(frames >= 15900 && frames <= 16100,
                   "file sink receives 16000 Hz PCM after resampling the source PCM");
        std::ifstream input(path, std::ios::binary);
        std::vector<char> samples((std::istreambuf_iterator<char>(input)), {});
        bool nonzero = false;
        for (char value : samples) nonzero = nonzero || value != 0;
        expectTrue(nonzero, "converted output contains the tone, not empty silence");
    }
}
}  // namespace

int main() {
    testInvalidPlayerConfig();
    testInvalidRecorderConfig();
    testNullPlayback();
    testEmptyPlaybackRejected();
    testNullCapture();
    testFixedHardwareRateWithCaptureConfigured();

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