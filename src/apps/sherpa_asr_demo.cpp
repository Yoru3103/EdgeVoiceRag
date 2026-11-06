#include <iostream>
#include <string>

#include "sherpa_onnx_asr_backend.h"
#include "wav_audio_reader.h"

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr
            << "Usage:\n"
            << "  "
            << argv[0]
            << " <model.onnx>"
            << " <tokens.txt>"
            << " <audio.wav>\n";

            return 1;
    }

    try {
        SherpaOnnxAsrConfig config;

        config.model_path = argv[1];
        config.tokens_path = argv[2];
        config.language = "zh";
        config.use_itn = true;
        config.num_threads = 2;
        config.provider = "cpu";
        config.debug = false;

        SherpaOnnxAsrBackend asr(config);

        const AudioBuffer audio = WavAudioReader::read(argv[3]);

        const auto result = asr.transcribe(audio);

        if (!result.ok) {
            std::cerr
                << "[ERROR] "
                << result.error
                << '\n';

            return 1;
        }

        std::cout
            << "Backend: "
            << asr.name()
            << '\n';

        std::cout
            << "Audio duration: "
            << audio.durationSeconds()
            << " s\n";

        std::cout
            << "ASR elapsed: "
            << result.elapsed_ms
            << " ms\n";

        std::cout
            << "Text: "
            << result.text
            << '\n';

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "[ERROR] "
            << error.what()
            << '\n';

        return 1;
    }
}