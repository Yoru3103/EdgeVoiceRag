/*
 * PcmStreamSource::stop()是永久停止
 * 该模块实现只取消不关闭handle的功能
 */

#pragma once

#include <functional>

#include "audio_recorder.h"

using SpeechStartedHandler = std::function<void()>;

class InterruptibleAudioRecorder : public AudioRecorder {
public:
    ~InterruptibleAudioRecorder() override = default;

    /*
     * 检测到用户开始说话时调用 handler，
     * 但仍继续录音，直到获得完整 utterance。
     */
    virtual AudioCaptureResult recordUtterance(
        const SpeechStartedHandler& handler
    ) = 0;

    /*
     * 只取消当前 recordUtterance()。
     * 后续仍可再次录音。
     */
    virtual void cancelCurrentRecording() = 0;
};
