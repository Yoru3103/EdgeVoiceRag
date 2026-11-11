#pragma once

#include "board_app_config.h"
#include "sherpa_onnx_tts_backend.h"

// 主程序、音频检查和基准使用相同的模型/词表/规则，避免各入口配置漂移。
SherpaOnnxTtsConfig makeBoardTtsConfig(const BoardAppConfig& config);
