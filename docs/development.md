# 构建、运行与验证

所有命令在 Linux/WSL 项目根目录执行。Windows 上通过 WSL 进入仓库，不在 Windows 原生工具链中直接链接 Linux .so。

各测试功能和新增并发场景见[C++测试手册](testing-guide.md)。

当前板端主线的诊断步骤见[ASan/UBSan学习指南](diagnostics.md)：先建立普通Debug对照，再运行带 `board_mainline` 标签的Host测试。

## 1. 基础依赖与文本构建

Debian/Ubuntu 可先准备开发依赖：

```bash
sudo apt-get install build-essential cmake pkg-config libzmq3-dev cppzmq-dev nlohmann-json3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

项目默认 C++17。CMake 的所有硬件/模型开关默认 OFF，但即使默认构建也需要 ZeroMQ 和 JSON 开发包。部分检索测试需要本地 chunks，模型/向量资产未提交 Git；迁移到另一台电脑时先生成索引。

```bash
./build/edge_voice_rag --help
./build/edge_voice_rag --config config/app.conf --once "空调怎么打开"
./build/agent_demo
./build/llm_server --backend mock
```

`config/app.conf/dev.conf/zmq.conf/python_zmq.conf` 属于文本客户端配置；`board_*.conf` 属于语音应用。不要混用解析器。

## 2. Python 环境与测试

选择独立虚拟环境，先装所需依赖，GPU 训练环境与 CPU 服务环境可分开维护。requirements 当前未实现完整锁版本，复现实验应另存实际 pip freeze、Python、CUDA 与 SDK 版本。

```bash
python -m pip install -r python/requirements.txt
PYTHONPATH=python python -m pytest python/tests -q
python python/rag/build_index.py --manual docs/vehicle_manual.txt --output vector_db/chunks.json
python python/rag/python_rag_server.py --llm-backend mock
```

项目已有脚本中的一部分强制检查 conda 环境名 edge-rag；直接运行 Python 模块不受这个脚本检查约束。pytest 需在当前环境安装；默认系统 python 不一定是项目环境。

Python RAG 可选择 `--llm-backend mock|ollama|zmq`。zmq 模式连接 C++ llm_server。默认 RAG 端口为 5556/5557/5558，LLM 为 8899/8900/8901，见[协议文档](protocols.md)。

## 3. BGE 导出与向量索引

```bash
python -m pip install -r python/requirements_embedding.txt
python python/embedding/export_bge_onnx.py
python python/embedding/build_dense_index.py
python python/embedding/verify_dense_index.py
```

导出需要模型和下载环境；离线时提前准备 Hugging Face 模型。首次导出可能下载较大文件，本次文档整理未重新下载或训练。export 内部做 PyTorch/ORT 样例对齐，这不是完整量化精度评测。

开启原生 BGE 需要 ONNX Runtime C++ SDK 和 tokenizers-cpp 子模块；后者的工具链依赖（如 Rust/Cargo）以子模块构建要求为准：

```bash
git submodule update --init --recursive
export ORT_SDK=/absolute/path/to/onnxruntime-linux-x64
cmake -S . -B build-bge \
  -DEDGE_ENABLE_BGE_EMBEDDER=ON \
  -DONNXRUNTIME_INCLUDE_DIR="$ORT_SDK/include" \
  -DONNXRUNTIME_LIBRARY="$ORT_SDK/lib/libonnxruntime.so"
cmake --build build-bge -j4
ctest --test-dir build-bge --output-on-failure
```

启用 BGE 且 BUILD_TESTING=ON 时，配置阶段会检查默认 model.onnx、tokenizer.json、index_meta.json 和 embeddings.f32 是否存在。用 `BGE_MODEL_DIR/BGE_INDEX_DIR` 覆盖测试资源目录。

## 4. 语音可选构建

| CMake 开关 | 依赖 | 添加的代码/目标 |
| --- | --- | --- |
| EDGE_ENABLE_ALSA | ALSA 开发库 | ALSA 录放音、PCM 源和对应测试 |
| EDGE_ENABLE_SHERPA_ASR | SHERPA_ONNX_ROOT | ASR、sherpa_asr_demo、ASR 测试 |
| EDGE_ENABLE_SHERPA_TTS | SHERPA_ONNX_ROOT | TTS 和模型集成测试 |
| EDGE_ENABLE_SHERPA_VAD | SHERPA_ONNX_ROOT | VAD 录音器和测试 |
| EDGE_ENABLE_BGE_EMBEDDER | ORT SDK、tokenizers-cpp、模型和索引 | BGE/Dense 源码与四组集成测试 |
| EDGE_ENABLE_RKLLM | RKLLM_SDK_ROOT | RKLLM 后端 |

四个音频/语音开关全部 ON 时才生成 `board_voice_assistant`。PC 使用 Mock LLM 仍需真实语音模型，不代表完全无依赖：

```bash
export SHERPA_SDK=/absolute/path/to/sherpa-onnx
cmake -S . -B build-voice \
  -DEDGE_ENABLE_ALSA=ON \
  -DEDGE_ENABLE_SHERPA_ASR=ON \
  -DEDGE_ENABLE_SHERPA_TTS=ON \
  -DEDGE_ENABLE_SHERPA_VAD=ON \
  -DSHERPA_ONNX_ROOT="$SHERPA_SDK"
cmake --build build-voice --target board_voice_assistant -j4
```

若不启用 BGE，复制 board_wsl.conf 到本地实验配置并把 retrieval_backend 改为 bm25；启用 BGE 时叠加上一节的三个选项。检查模型文件、声卡和运行库路径后再运行：

```bash
arecord -l
aplay -l
./build-voice/board_voice_assistant config/board_wsl.conf
```

上面的原始 board_wsl.conf 默认 hybrid，不能直接用于 BGE=OFF 的构建。PC/WSL 声卡可见性依赖系统音频配置。`board_pc_mock.conf` 使用 NULL 音频设备，但依旧装配 ASR/TTS 模型，且 ASR 路径与 board_wsl.conf 不同。

开启 BGE 和 sherpa 时 CMake 会让 sherpa 复用显式指定的 ONNX Runtime 动态库。需要核对 sherpa、ORT 头文件/库的版本和 ABI，不能随意混搭两个 .so。程序运行找不到库时用 ldd 检查，按 SDK 要求设置库搜索路径。

## 5. RK3588 交叉编译与运行

[工具链文件](../cmake/toolchains/rk3588-buildroot.cmake)依赖板厂 Buildroot SDK。设定 host 目录后引用 SDK 的 toolchainfile。以下是参数模板，路径要换为实际 **aarch64** SDK：

```bash
export RK3588_BUILDROOT_HOST=/absolute/path/to/buildroot/host
export SHERPA_ARM64=/absolute/path/to/sherpa-onnx-rk3588
export ORT_ARM64=/absolute/path/to/onnxruntime-aarch64
export RKLLM_ARM64=/absolute/path/to/librkllm_api
cmake -S . -B build-rk3588 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3588-buildroot.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DEDGE_ENABLE_ALSA=ON \
  -DEDGE_ENABLE_SHERPA_ASR=ON \
  -DEDGE_ENABLE_SHERPA_TTS=ON \
  -DEDGE_ENABLE_SHERPA_VAD=ON \
  -DEDGE_ENABLE_BGE_EMBEDDER=ON \
  -DEDGE_ENABLE_RKLLM=ON \
  -DSHERPA_ONNX_ROOT="$SHERPA_ARM64" \
  -DONNXRUNTIME_INCLUDE_DIR="$ORT_ARM64/include" \
  -DONNXRUNTIME_LIBRARY="$ORT_ARM64/lib/libonnxruntime.so" \
  -DRKLLM_SDK_ROOT="$RKLLM_ARM64"
cmake --build build-rk3588 --target board_voice_assistant -j4
```

JSON、ZeroMQ、ALSA 也须位于目标 sysroot/工具链可查找位置。不要把 x86 的包目录当目标依赖。编译成功后在 RK3588 部署程序、模型、vector_db、conf 与匹配运行库，从项目部署根目录执行：

```bash
export ALSA_CONFIG_PATH="$PWD/config/alsa-rk3588.conf"
./board_voice_assistant config/board_rk3588.conf
```

实际配置选用 Hybrid、RKLLM、Rule Planner 和 IIO。初次离线验证可复制配置，把 agent_device_backend 改成 mock。模型和 Runtime/驱动版本应写入实验 manifest；不能仅凭 .rkllm 文件存在认定可运行。

## 6. 内核模块和设备接口

`kernel_modules/edge_dht11` 独立于 CMake，Makefile 需要匹配开发板内核构建树，先阅读源码中的资源要求。设备树 compatible 为 `alientek,edge-dht11`，GPIO 由 data-gpios 提供，需要支持 IRQ 且不可睡眠的 GPIO。

应用按 IIO name=edge_dht11 查找节点，读取 in_temp_input、in_humidityrelative_input，并除以 1000 转为摄氏度/%RH。单元测试使用临时 sysfs 目录，不能替代真实接线和驱动时序验证。空调/LED 写入目前不访问真实 GPIO/LED Class。

本次未安装/加载内核模块、改设备树或操作真实设备。

## 7. 评测与性能工具

队列等待、BGE分段与可复现采样流程见[性能观测指南](performance-guide.md)。原始样本及源码/资产指纹由离线记录器保存，性能采样要求Release/RelWithDebInfo且关闭Sanitizer。

```bash
./build/retrieval_benchmark bm25 vector_db/chunks.json benchmarks/retrieval_cases.json 3 10 --filter
./build/voice_performance_summary perf_logs/session.log
```

Dense/Hybrid benchmark 参数：

```bash
./build-bge/retrieval_benchmark hybrid vector_db/chunks.json benchmarks/retrieval_cases.json 3 10 \
  models/embedding/bge-small-zh-v1.5 vector_db/bge-small-zh-v1.5 --filter
```

检索工具输出 Top1、Recall@K、MRR、负例拒答、延迟和 METRICS_JSON；语音工具读 PERF_JSON 生成均值/P50/P95/最大值。后者无有效报告返回 2，文件/参数错误返回 1。采集基准时使用 Release/RelWithDebInfo，Debug 适合功能调试，不用于宣传性能。

训练入口及参数先查看帮助，完整训练需 GPU 和专用依赖：

```bash
python -m pip install -r python/requirements_agent_finetune.txt
PYTHONPATH=python python -m agent_finetune.train_qlora --help
PYTHONPATH=python python -m agent_finetune.evaluate_planner --help
```

不要在文档复现步骤中自动覆盖已有 train/validation/heldout；生成新版本放独立目录。QLoRA adapter 合并、SDK 转换和板端一致性评测仍是待办。

## 8. 回归检查范围

目录迁移至少验证：CMake 默认构建、全部默认 CTest、已有 Python 测试、所有显式源码路径、所有文档相对链接、迁移文件哈希。硬件/SDK 组合单独记录，不能以 PC Mock 测试代替板端验收。本次实际结果见[迁移记录](refactoring.md)。

## 9. CMake 分层与构建流程

根 [CMakeLists.txt](../CMakeLists.txt) 负责项目总装配，C++17及程序输出目录在这里统一设置。构建组织如下：

| 文件/目录 | 作用 |
| --- | --- |
| `cmake/EdgeOptions.cmake` | 声明功能开关、SDK路径和测试模型路径 |
| `cmake/EdgeDiagnostics.cmake` | 诊断编译/链接参数与工具链探测，默认关闭 |
| `cmake/EdgeDependencies.cmake` | 查找通用依赖和已开启的可选SDK |
| `src/CMakeLists.txt` 与各模块子CMake | 创建核心库，逐模块添加源码、编译宏和依赖 |
| `src/apps/CMakeLists.txt` | 定义应用、服务和演示程序 |
| `tools/CMakeLists.txt` | 定义检索/语音性能工具 |
| `tests/CMakeLists.txt` | 定义测试程序、CTest注册、参数与工作目录 |

执行顺序为“配置并生成构建系统 → 编译指定目标 → 运行程序或CTest”。新增源码修改所属模块的子CMake；新增测试修改tests目录。程序默认仍输出到构建根目录，例如 `build/agent_demo`。

部署使用独立目录并关闭测试：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release -j4
```

该开关只关闭项目测试目标；应用、工具和所选后端的SDK检查仍然保留。默认值为ON，可正常执行43项默认CTest；可选测试数随后端组合增加。

常用定位命令：

```bash
cmake -LH -N build
cmake --build build --target help
cmake --build build --target agent_demo --verbose
ctest --test-dir build -N
```

CMake开关控制哪些后端编入二进制，运行conf选择实际使用的后端。修改同一构建目录的配置会保留未覆盖的缓存值；PC、语音联调和RK3588交叉编译建议使用各自独立的构建目录。

完整层级、目标表、变量默认值和新增模块示例见[CMake构建指南](cmake-structure.md)，总体模块关系见[项目文档](project-guide.md)。

## RK3588手动验收入口

后续板端测试由使用者操作，步骤见[手动性能测试](rk3588-performance.md)。DHT11 DATA改用JP6第3脚（GPIO4_A5，3.3V IO；需拔掉JP6跳帽并禁用UART3），接线与设备树核对见[DHT11硬件说明](hardware-dht11.md)。当前板端配置使用DeepSeek RKLLM和Rule Planner，本地Qwen LoRA适配器尚未接入板端。

DHT11当前准备的0.2.2版本只在实际测量期间持有CPU latency QoS请求，缓存读取不持有。手动加载及不带外部约束的验证见[窗口QoS记录](experiments/2026-11-09-dht11-qos-window.md)，仍独立于用户态CMake。

用户后续确认DHT11测试没有问题，按要求结束首条专项追查；驱动分层、硬中断与QoS时序及历史异常原因见[DHT11专题](dht11-driver-guide.md)。接下来继续真实温湿度Agent联调，语音链路单独验收。

## 真实IIO设备的C++ Agent联调

agent_demo默认仍用Mock，可通过以下参数选择Linux IIO，仍使用Rule Planner：

```bash
./build/agent_demo --device iio --iio-root /sys/bus/iio/devices --iio-device-name edge_dht11
```

该入口不初始化声卡和模型；空调/LED仍为内存状态。独立ARM包、动态库校验及确认/取消验收见[板端Agent主线联调](board-agent-bringup.md)。此步骤通过后再接回board_voice_assistant的语音路径。

## 独立语音输出验收

使用者已确认IIO文字Agent调用正常。完整语音恢复前，先用新增`audio_output_check`独立检查tone→ALSA与文本→TTS→ALSA，并将同一份PCM导出WAV交给aplay对照。该入口不开录音、VAD、ASR、Agent或LLM，复用board_voice_assistant的TTS设置及AlsaAudioPlayer。

[逐步手动操作、独立ARM包与结果判读](board-audio-output.md)。此包复用部署根目录模型，不覆盖现有主程序；Host null设备测试无法证明板端发声。

## 已删除板端部署目录时恢复

使用[完整部署包](rk3588-deployment.md)，包含当前主线模型、索引、全部用户态运行库及0.2.2-window-qos模块。包内只有一个EdgeVoiceRAG顶层目录，附启动脚本和校验表；不修改启动分区或系统自动加载配置。驱动针对6.1.141重新编译，用户仍需手动核对板端内核与当前已加载版本。

板端当前PCM配置为edge_voice_16k，通过config/alsa-rk3588.conf固定底层录放音同为16k，TTS保持源格式再转换。直接运行程序须设置ALSA_CONFIG_PATH，完整包启动脚本自动设置；详见[采样率对称冲突与口语查询修复](rk3588-duplex-fix.md)。


## TTS模型选择与测量

当前board_rk3588.conf显式选择原Melo的FP32 model.onnx、speaker0；旧conf省略新字段仍保留INT8缺省。
需要新增权重，不能只更新二进制后继续使用缺少model.onnx的旧目录。
[TTS更新包与板端对照步骤](tts-optimization.md)提供保留用户打断阈值的安装方式，以及8k中文备选配置。

tts_benchmark仅需SHERPA_TTS即可生成，默认固定4条文本；--text可聚焦用户本次读数。
它复用相同后端配置，计时不含模型加载和播放；RTF/RSS口径与主程序PERF_JSON首播应分开。
[NPU迁移](speech-npu-roadmap.md)是后续计划，当前SDK仍未启用RKNN，CPU provider未变。

## 半双工语音基线

新版主程序支持barge_in_enabled=false：回答期间不监听打断，结束后再正常录音，适用于当前外放基线。缺项旧配置保持true；启动会显示实际模式。用户TTS速度已认可，下一步操作见[主线基线与打断验收](board-mainline-acceptance.md)。
