# Discover dependencies only; module CMakeLists attach them to the core target.
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

if(EDGE_ENABLE_BGE_EMBEDDER)
    if(
        NOT EXISTS
        "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h"
    )
        message(FATAL_ERROR
            "onnxruntime_cxx_api.h was not found under "
            "ONNXRUNTIME_INCLUDE_DIR"
        )
    endif()

    if(
        NOT EXISTS
        "${ONNXRUNTIME_LIBRARY}"
    )
        message(FATAL_ERROR
            "ONNXRUNTIME_LIBRARY does not exist"
        )
    endif()

    if(
        NOT EXISTS
        "${TOKENIZERS_CPP_ROOT}/CMakeLists.txt"
    )
        message(FATAL_ERROR
            "tokenizers-cpp was not found under TOKENIZERS_CPP_ROOT"
        )
    endif()

    add_subdirectory(
        "${TOKENIZERS_CPP_ROOT}"
        "${PROJECT_BINARY_DIR}/tokenizers-cpp"
        EXCLUDE_FROM_ALL
    )
endif()

if(EDGE_ENABLE_RKLLM)
    # 查找rkllm.h所在目录，在设定paths中寻找，找到后赋值给RKLLM_INCLUDE_DIR
    find_path(
        RKLLM_INCLUDE_DIR
        rkllm.h
        PATHS
        "${RKLLM_SDK_ROOT}/include"
        "${RKLLM_SDK_ROOT}/librkllm_api/include"
        NO_DEFAULT_PATH
    )

    # 查找动态库带有rkllmrt相关的动态库文件
    find_library(
        RKLLM_RUNTIME_LIBRARY
        NAMES rkllmrt
        PATHS
        "${RKLLM_SDK_ROOT}/lib"
        "${RKLLM_SDK_ROOT}/lib64"
        "${RKLLM_SDK_ROOT}/${CMAKE_SYSTEM_PROCESSOR}"
        NO_DEFAULT_PATH
    )

    if(NOT RKLLM_INCLUDE_DIR)
        message(FATAL_ERROR
            "rkllm.h was not found under RKLLM_SDK_ROOT"
        )
    endif()

    if(NOT RKLLM_RUNTIME_LIBRARY)
        message(FATAL_ERROR
            "librkllmrt.so was not found under RKLLM_SDK_ROOT"
        )
    endif()
endif()

if(
    EDGE_ENABLE_SHERPA_ASR
    OR EDGE_ENABLE_SHERPA_TTS
    OR EDGE_ENABLE_SHERPA_VAD
)
    # 使用必须说明sherpa SDK的根目录位置
    if(NOT SHERPA_ONNX_ROOT)
        message(FATAL_ERROR
            "sherpa-onnx ASR/TTS requires SHERPA_ONNX_ROOT"
        )
    endif()

    find_path(
        SHERPA_ONNX_INCLUDE_DIR
        sherpa-onnx/c-api/c-api.h
        PATHS
        "${SHERPA_ONNX_ROOT}/include"
        NO_DEFAULT_PATH
    )

    if(EDGE_ENABLE_BGE_EMBEDDER)
        set(
            SHERPA_ONNX_RUNTIME_LIBRARY
            "${ONNXRUNTIME_LIBRARY}"
        )
    else()
        find_library(
            SHERPA_ONNX_RUNTIME_LIBRARY
            NAMES onnxruntime
            PATHS
            "${SHERPA_ONNX_ROOT}/lib"
            NO_DEFAULT_PATH
        )
    endif()

    find_library(
        SHERPA_ONNX_C_LIBRARY
        NAMES sherpa-onnx-c-api
        PATHS
        "${SHERPA_ONNX_ROOT}/lib"
        NO_DEFAULT_PATH
    )

    if(NOT SHERPA_ONNX_INCLUDE_DIR)
        message(FATAL_ERROR
            "sherpa-onnx C API headers were not found"
        )
    endif()

    if(NOT SHERPA_ONNX_C_LIBRARY)
        message(FATAL_ERROR
            "libsherpa-onnx-c-api was not found"
        )
    endif()

    if(NOT SHERPA_ONNX_RUNTIME_LIBRARY)
        message(FATAL_ERROR
            "libonnxruntime was not found"
        )
    endif()
endif()

if(EDGE_ENABLE_ALSA)
    find_package(ALSA REQUIRED)
endif()
