#pragma once

#include <string>

#include "continuous_voice_session.h"

class VoicePerformanceReport {
public:
    static constexpr int KVersion = 1;

    /*
     * 将一个正常完成的 AnswerCompleted 事件
     * 编码成单行 JSON。
     */
    static std::string encode(const VoiceSessionEvent& event);
};
