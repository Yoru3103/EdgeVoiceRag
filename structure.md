# 当前板载主路线

src/board_voice_main.cpp
    │
    ├── ALSA PCM + Silero VAD
    ├── SenseVoice ASR
    ├── RetrieverRuntime
    │       BM25 / Dense / Hybrid + relevance filter
    ├── PolicyRoutingAnswerBackend
    │   ├── QueryClassifier
    │   ├── ResponsePolicy
    │   ├── Safety
    │   ├── DirectRag
    │   ├── RagLlm
    │   ├── LlmOnly
    │   └── Clarification
    ├── RKLLM / Mock LLM
    ├── sherpa-onnx VITS TTS
    ├── ALSA playback
    └── ContinuousVoiceSession
            播放期间 VAD 打断并取消 RKLLM

# 板载决策流程

ASR 文本
  → QueryClassifier
  → Creative                    → LlmOnly（跳过检索）
  → 其他类型                    → 检索一次
      ├── Emergency             → Safety
      ├── Factual + 高置信结果  → DirectRag
      ├── 有相关结果            → RagLlm
      ├── Complex + 无结果      → LlmOnly
      └── Unknown + 无结果      → Clarification

# 兼容命令行路线

src/main.cpp 仍使用旧的 MultiLevelResponseSystem 和 ResponseBackend，
用于命令行、ZeroMQ 与早期架构兼容，不是当前板载语音入口。
