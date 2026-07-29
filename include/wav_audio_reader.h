#pragma once

#include <string>

#include "audio_buffer.h"

/*
 * 标准WAV结构如下：
 * RIFF
 * ├── RIFF size
 * ├── WAVE
 * ├── fmt
 * │   ├── 编码格式
 * │   ├── 声道数
 * │   ├── 采样率
 * │   ├── 每秒字节数
 * │   ├── 每帧字节数
 * │   └── 采样位数
 * ├── LIST/JUNK等可选块
 * └── data
 *     └── PCM采样数据
 */

class WavAudioReader final {
public:
    static AudioBuffer read(const std::string& path);
};