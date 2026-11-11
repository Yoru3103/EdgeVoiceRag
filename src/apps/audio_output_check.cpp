#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include "alsa_audio_player.h"
#include "board_app_config.h"
#include "board_tts_config.h"
#ifdef EDGE_ENABLE_SHERPA_TTS
#include "sherpa_onnx_tts_backend.h"
#endif

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
    std::string config = "config/board_rk3588.conf";
    std::string mode = "tone";
    std::string device;
    std::string output_wav;
    std::string text = "语音输出测试，温湿度设备已经接入。";
    int sample_rate = 16000;
    unsigned long period_frames = 1024;
    unsigned int repeat = 2;
    bool help = false;
};

void usage(std::ostream& out) {
    out << "Usage: audio_output_check [--config PATH] [--device PCM]\n"
        << "       [--mode tone|tts] [--sample-rate HZ] [--text TEXT]\n"
        << "       [--period-frames N] [--repeat N] [--output-wav NEW_PATH]\n"
        << "tone: 1 second, 440 Hz, mono PCM16, amplitude 0.1.\n"
        << "tts: same model/settings as board_voice_assistant; no resampling.\n"
        << "sample-rate only applies to tone; text only applies to tts.\n"
        << "WAV is saved before opening playback; existing files are preserved.\n";
}

unsigned long number(const std::string& value, unsigned long low,
                     unsigned long high, const std::string& option) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("invalid number for " + option);
    unsigned long result;
    try {
        result = std::stoul(value);
    } catch (const std::exception&) {
        throw std::invalid_argument("number out of range for " + option);
    }
    if (result < low || result > high)
        throw std::invalid_argument("number out of range for " + option);
    return result;
}

Options parse(int argc, char** argv) {
    Options options;
    bool rate_given = false;
    bool text_given = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help" || key == "-h") {
            options.help = true;
            continue;
        }
        if (key != "--config" && key != "--device" && key != "--mode"
            && key != "--sample-rate" && key != "--text" && key != "--period-frames"
            && key != "--repeat" && key != "--output-wav")
            throw std::invalid_argument("unknown option: " + key);
        if (i + 1 >= argc)
            throw std::invalid_argument("missing value for " + key);
        const std::string value = argv[++i];
        if (value.empty() || value.rfind("--", 0) == 0)
            throw std::invalid_argument("missing value for " + key);
        if (key == "--config") options.config = value;
        else if (key == "--device") options.device = value;
        else if (key == "--mode") options.mode = value;
        else if (key == "--text") { options.text = value; text_given = true; }
        else if (key == "--output-wav") options.output_wav = value;
        else if (key == "--sample-rate") {
            options.sample_rate = static_cast<int>(number(value, 8000, 192000, key));
            rate_given = true;
        } else if (key == "--period-frames") options.period_frames = number(value, 1, 65536, key);
        else options.repeat = static_cast<unsigned int>(number(value, 1, 10, key));
    }
    if (options.mode != "tone" && options.mode != "tts")
        throw std::invalid_argument("--mode must be tone or tts");
    if (options.mode == "tts" && rate_given)
        throw std::invalid_argument("TTS uses its own sample rate; omit --sample-rate");
    if (options.mode == "tone" && text_given)
        throw std::invalid_argument("--text requires --mode tts");
    if (options.text.size() > 4096)
        throw std::invalid_argument("text exceeds 4096 UTF-8 bytes");
    return options;
}

void emit(nlohmann::json value) {
    value["version"] = 1;
    std::cout << value.dump() << std::endl;
}

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

AudioBuffer tone(int rate) {
    AudioBuffer audio;
    audio.sample_rate = rate;
    audio.channels = 1;
    audio.samples.resize(static_cast<std::size_t>(rate));
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < audio.samples.size(); ++i) {
        const double value = std::sin(2.0 * pi * 440.0 * static_cast<double>(i) / rate);
        audio.samples[i] = static_cast<std::int16_t>(std::lround(value * 3276.7));
    }
    return audio;
}

void bytes(std::FILE* file, const void* data, std::size_t size) {
    if (std::fwrite(data, 1, size, file) != size)
        throw std::runtime_error("failed to write WAV");
}

void le16(std::FILE* file, std::uint16_t value) {
    const unsigned char data[] = {static_cast<unsigned char>(value),
                                 static_cast<unsigned char>(value >> 8)};
    bytes(file, data, sizeof data);
}

void le32(std::FILE* file, std::uint32_t value) {
    const unsigned char data[] = {static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24)};
    bytes(file, data, sizeof data);
}

void writeWav(const std::string& path, const AudioBuffer& audio) {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    if (audio.samples.size() > (maximum - 36) / 2 || audio.channels > 32767
        || static_cast<std::uint64_t>(audio.sample_rate) * audio.channels * 2 > maximum)
        throw std::runtime_error("audio exceeds PCM16 RIFF limits");

    // O_EXCL避免覆盖已有录音、模型产物或其他本地资产。
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        throw std::runtime_error("cannot create new WAV " + path + ": " + std::strerror(errno));
    std::FILE* raw = ::fdopen(fd, "wb");
    if (!raw) {
        const int error = errno;
        ::close(fd);
        throw std::runtime_error("cannot open WAV stream: " + std::string(std::strerror(error)));
    }
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(raw, &std::fclose);
    const auto data_size = static_cast<std::uint32_t>(audio.samples.size() * 2);
    bytes(raw, "RIFF", 4); le32(raw, 36 + data_size); bytes(raw, "WAVEfmt ", 8);
    le32(raw, 16); le16(raw, 1); le16(raw, static_cast<std::uint16_t>(audio.channels));
    le32(raw, static_cast<std::uint32_t>(audio.sample_rate));
    le32(raw, static_cast<std::uint32_t>(audio.sample_rate) * audio.channels * 2);
    le16(raw, static_cast<std::uint16_t>(audio.channels * 2)); le16(raw, 16);
    bytes(raw, "data", 4); le32(raw, data_size);
    for (auto sample : audio.samples)
        le16(raw, static_cast<std::uint16_t>(sample));
    if (std::fclose(file.release()) != 0)
        throw std::runtime_error("failed to close WAV " + path);
}
}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        usage(std::cerr);
        return 2;
    }
    if (options.help) { usage(std::cout); return 0; }

    std::string phase = "config";
    try {
        const auto config = BoardAppConfig::load(options.config);
        if (options.device.empty()) options.device = config.playback_device;
#ifdef EDGE_ENABLE_SHERPA_TTS
        constexpr bool tts_compiled = true;
#else
        constexpr bool tts_compiled = false;
#endif
        emit({{"type", "audio_check_config"}, {"mode", options.mode},
              {"device", options.device}, {"period_frames_requested", options.period_frames},
              {"repeat", options.repeat}, {"tts_compiled", tts_compiled},
              {"config", options.config}, {"tts_model_file", config.tts_model_file},
              {"tts_model_dir", config.tts_model_dir}, {"tts_num_threads", config.tts_num_threads},
              {"tts_speaker_id", config.tts_speaker_id}});
        phase = "audio_prepare";
        AudioBuffer audio;
        nlohmann::json audio_info = {{"type", "audio_check_audio"},
            {"tts_load_ms", nullptr}, {"tts_synthesis_ms", nullptr}};
        const auto prepare_start = Clock::now();
        if (options.mode == "tone") {
            audio = tone(options.sample_rate);
        } else {
#ifdef EDGE_ENABLE_SHERPA_TTS
            const auto tts_config = makeBoardTtsConfig(config);
            auto start = Clock::now();
            SherpaOnnxTtsBackend tts(tts_config);
            audio_info["tts_load_ms"] = elapsed(start);
            start = Clock::now();
            auto result = tts.synthesize(options.text);
            audio_info["tts_synthesis_ms"] = elapsed(start);
            if (!result.ok) throw std::runtime_error("TTS synthesis failed: " + result.error);
            audio = std::move(result.audio);
#else
            throw std::runtime_error("TTS is not compiled; enable EDGE_ENABLE_SHERPA_TTS");
#endif
        }
        if (audio.empty() || audio.sample_rate <= 0 || audio.channels <= 0
            || audio.samples.size() % static_cast<std::size_t>(audio.channels) != 0)
            throw std::runtime_error("generated audio is empty or malformed");
        audio_info.update({{"sample_rate", audio.sample_rate}, {"channels", audio.channels},
            {"frames", audio.frameCount()}, {"format", "S16_LE"},
            {"duration_seconds", audio.durationSeconds()}, {"audio_prepare_ms", elapsed(prepare_start)}});
        emit(audio_info);

        if (!options.output_wav.empty()) {
            phase = "wav";
            writeWav(options.output_wav, audio);
            emit({{"type", "audio_check_wav"}, {"path", options.output_wav}});
        }
        phase = "playback_open";
        AlsaAudioPlayerConfig player_config;
        player_config.device = options.device;
        player_config.period_frames = options.period_frames;
        AlsaAudioPlayer player(player_config);
        for (unsigned int i = 1; i <= options.repeat; ++i) {
            phase = "playback";
            const auto start = Clock::now();
            const auto result = player.play(audio);
            emit({{"type", "audio_check_playback"}, {"iteration", i},
                  {"ok", result.ok}, {"elapsed_ms", elapsed(start)}, {"error", result.error}});
            if (!result.ok) return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        emit({{"type", "audio_check_error"}, {"phase", phase}, {"error", error.what()}});
        return 1;
    }
}
