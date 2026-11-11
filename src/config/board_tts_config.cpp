#include "board_tts_config.h"

#include <filesystem>

SherpaOnnxTtsConfig makeBoardTtsConfig(const BoardAppConfig& config) {
    const std::filesystem::path dir = config.tts_model_dir;
    SherpaOnnxTtsConfig result;
    result.model_path = (dir / config.tts_model_file).string();
    result.lexicon_path = (dir / "lexicon.txt").string();
    result.tokens_path = (dir / "tokens.txt").string();
    result.rule_fsts = {(dir / "date.fst").string(), (dir / "number.fst").string()};
    result.num_threads = config.tts_num_threads;
    result.speaker_id = config.tts_speaker_id;
    result.speed = 1.0F;
    result.provider = "cpu";
    return result;
}
