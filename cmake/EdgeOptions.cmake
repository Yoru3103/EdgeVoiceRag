# Build features and SDK/model locations. Keep cache names stable.
option(EDGE_ENABLE_ALSA "Enable ALSA audio capture and playback" OFF)
option(EDGE_ENABLE_SHERPA_VAD "Enable sherpa-onnx C++ VAD recorder" OFF)

option(EDGE_ENABLE_RKLLM "Enable Rockchip RKLLM backend" OFF)
set(RKLLM_SDK_ROOT "" CACHE PATH "Path to RKLLM runtime directory")

option(EDGE_ENABLE_BGE_EMBEDDER "Enable native ONNX BGE embedding backend" OFF)
set(ONNXRUNTIME_INCLUDE_DIR "" CACHE PATH "Directory containing onnxruntime_cxx_api.h")
set(ONNXRUNTIME_LIBRARY "" CACHE FILEPATH "Path to libonnxruntime.so")
set(TOKENIZERS_CPP_ROOT "${PROJECT_SOURCE_DIR}/third_party/tokenizers-cpp" CACHE PATH "Path to tokenizers-cpp source")
set(BGE_MODEL_DIR "${PROJECT_SOURCE_DIR}/models/embedding/bge-small-zh-v1.5" CACHE PATH "BGE ONNX model and tokenizer directory")
set(BGE_INDEX_DIR "${PROJECT_SOURCE_DIR}/vector_db/bge-small-zh-v1.5" CACHE PATH "BGE document vector index directory")

option(EDGE_ENABLE_SHERPA_ASR "Enable sherpa-onnx C++ ASR backend" OFF)
option(EDGE_ENABLE_SHERPA_TTS "Enable sherpa-onnx C++ TTS backend" OFF)
set(SHERPA_ONNX_ROOT "" CACHE PATH "Path to sherpa-onnx SDK root")
set(
    SHERPA_ONNX_TTS_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/tts/vits-melo-tts-zh_en"
    CACHE PATH
    "Path to the sherpa-onnx TTS model used by integration tests"
)
