# CMake 构建结构与维护指南

当前 CMake 按“项目装配 → 模块源码 → 应用/工具/测试”分层。根 [CMakeLists.txt](../CMakeLists.txt) 只保留版本、语言、公共输出位置、构建模块入口和测试开关，约20行。

## 1. 文件职责

```text
CMakeLists.txt                    # 根装配，include + add_subdirectory
cmake/
  EdgeOptions.cmake               # EDGE_ENABLE_*、SDK和测试模型缓存路径
  EdgeDependencies.cmake          # 通用包、ALSA/ORT/sherpa/RKLLM依赖查找
  toolchains/
    rk3588-buildroot.cmake         # 交叉编译工具链，使用-DCMAKE_TOOLCHAIN_FILE选择
src/
  CMakeLists.txt                  # 创建edge_voice_rag_core、公共include/link、遍历模块
  common/CMakeLists.txt           # 日志、计时、指纹
  config/CMakeLists.txt           # 配置与命令行
  audio/CMakeLists.txt            # WAV、可选ALSA源码及其依赖
  speech/CMakeLists.txt           # 可选ASR/TTS/VAD源码、宏和sherpa依赖
  retrieval/CMakeLists.txt        # 检索源码、可选BGE/ORT/tokenizers
  inference/CMakeLists.txt        # Mock/工厂、可选RKLLM
  response/CMakeLists.txt         # 分类、策略、回答后端
  transport/CMakeLists.txt        # ZeroMQ、协议、服务
  devices/CMakeLists.txt          # Mock/Linux IIO设备
  agent/CMakeLists.txt            # Planner、Executor、工具和意图路由
  voice/CMakeLists.txt            # 会话、分句和语音流水线
  observability/CMakeLists.txt    # 性能报告/聚合
  apps/CMakeLists.txt             # edge_voice_rag、服务、demo、board_voice_assistant
tools/CMakeLists.txt              # retrieval_benchmark、voice_performance_summary
tests/CMakeLists.txt              # 默认/可选测试定义、注册、参数与工作目录
```

上面工具链参数的完整写法为 `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3588-buildroot.cmake`。SDK 查找和工具链承担不同职责，不把机器相关 SDK 路径写进每个模块。

根文件通过 add_subdirectory 进入目录，模块通过 target_sources 把显式源码加入已创建的核心目标；source 采用 CMAKE_CURRENT_SOURCE_DIR 构造绝对路径，避免从父目录注册目标时路径含义混淆。[CMake add_subdirectory](https://cmake.org/cmake/help/latest/command/add_subdirectory.html)、[CMake target_sources](https://cmake.org/cmake/help/latest/command/target_sources.html)

## 2. 为什么当前仍保留一个核心库

**构建脚本分层与拆成多个库是两个独立步骤。** 当前每个模块都能维护自己的源文件和条件依赖，但最终仍编入 edge_voice_rag_core，所有原有调用者继续使用该目标。

这样可以先解决根文件过长、修改点不明确和测试重复定义的问题。下一步真正拆成 edge_retrieval、edge_inference 等库时，再处理接口类型归属、PUBLIC/PRIVATE依赖和跨层引用，避免按目录机械拆库。

现有目标名、缓存变量名、默认功能开关和 ./build/<program> 路径保持兼容；静态核心库也继续输出到构建根目录。拆分没有改业务源码。

## 3. 依赖与功能开关怎么流转

以 BGE 为例：

1. [EdgeOptions.cmake](../cmake/EdgeOptions.cmake) 声明 EDGE_ENABLE_BGE_EMBEDDER、ONNXRUNTIME_*、TOKENIZERS_CPP_ROOT、BGE_MODEL_DIR/BGE_INDEX_DIR。
2. [EdgeDependencies.cmake](../cmake/EdgeDependencies.cmake) 在开关开启时检查ORT SDK与tokenizers-cpp，并引入第三方构建目录。
3. [retrieval/CMakeLists.txt](../src/retrieval/CMakeLists.txt) 添加 bge_embedder/dense_retriever 源码、编译宏和依赖。
4. [tests/CMakeLists.txt](../tests/CMakeLists.txt) 在测试开启时检查BGE模型/索引资产，注册四项模型集成测试。
5. Runtime依旧读取实际配置文件并检查模型/索引，构建成功不等于运行资产齐全。

sherpa 与 BGE 同时开启时，依旧共用显式指定的 ONNX Runtime 库；不要因配置拆开就混用不同ABI的库。RKLLM与ALSA遵循相同的“查找在cmake、使用在所属模块”约定。

## 4. 新增代码应该改哪里

### 普通实现文件

例如新增 src/retrieval/reranker.cpp：在 retrieval/CMakeLists.txt 的 target_sources 中显式加一行：

```cmake
target_sources(edge_voice_rag_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/reranker.cpp"
)
```

不用修改根CMake，不使用 GLOB 自动搜源码。接口声明仍放 include，并同步接口文档。

### 一个新可选后端

在 EdgeOptions 中声明开关与缓存路径，在 EdgeDependencies 中做查找/错误提示；所属源码目录在 if(开关) 内声明 source、include、编译宏与链接库。最后在 tests 中用相同开关注册对应测试。运行时配置和模型契约也需单独实现。

### 一个新应用或工具

应用入口放 src/apps 并修改其 CMakeLists；性能/数据工具放 tools 并修改 tools/CMakeLists。新程序会按默认配置输出到构建根目录。

### 一个新测试

默认约定“目标名 = 源码文件名（去.cpp）”。tests/CMakeLists 提供小型注册函数：

```cmake
edge_add_test(reranker_tests)
edge_add_test(dataset_tests ARGS "${PROJECT_SOURCE_DIR}/path/to/fixture.json")
edge_add_test(special_tests NAME public_test_name)
```

如需实际使用特殊源文件名，可直接写 add_executable/add_test，不扩展函数形成复杂测试框架。默认工作目录是 PROJECT_BINARY_DIR，保持原有测试语义；读取根目录相对资产的测试显式设置 WORKING_DIRECTORY。

## 5. 开发构建与部署构建

默认流程不变：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

使用标准 BUILD_TESTING 关闭测试：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release -j4
```

BUILD_TESTING 由 include(CTest) 提供，默认 ON；关闭后不创建项目测试可执行目标，也不检查仅用于BGE测试的模型资产，但应用和工具依旧构建，SDK依赖检查依旧生效。[CMake CTest模块](https://cmake.org/cmake/help/latest/module/CTest.html)

在已使用过的构建目录切换 OFF 后，旧测试二进制或CTest生成文件可能残留，不能用“文件还在”判断开关未生效；生产构建使用独立目录可避免这种混淆。

RK3588构建继续使用[开发文档](development.md)中的工具链和SDK参数，另外追加 -DBUILD_TESTING=OFF 即可选择不构建测试。

## 6. 本次验证范围

- 默认配置：编译并执行40项CTest。
- PC可选配置：ALSA、sherpa ASR/TTS/VAD、BGE开启，board_voice_assistant编译链接；RKLLM关闭。
- 对照拆分前的默认40项/可选48项CTest注册信息，测试名、命令参数和工作目录一致。
- 使用CMake File API核对可选配置下120个项目.cpp源文件与拆分前清单一致，另一个RKLLM源文件保持原条件注册。
- 全新BUILD_TESTING=OFF配置：只生成应用、工具和核心库目标，无项目测试可执行目标。

本轮验证只针对构建组织，未在真实开发板运行，不重复执行未受影响的Python训练/评测。

## 7. 从配置到编译的实际流程

```text
cmake -S . -B <build-dir> [选项]
  → 根CMake声明语言与C++17，设置程序输出位置
  → EdgeOptions读取/创建缓存开关和路径
  → EdgeDependencies查找依赖，引入需要的第三方目录
  → CTest提供BUILD_TESTING选项
  → src创建edge_voice_rag_core，逐模块添加源码/宏/依赖
  → apps定义程序；tools定义性能工具
  → BUILD_TESTING=ON时，tests创建并注册测试
  → 生成所选构建系统及CMakeCache.txt

cmake --build <build-dir> [--target <target>] -j4
  → 按目标依赖编译对象文件、构建库并链接程序

ctest --test-dir <build-dir> --output-on-failure
  → 执行已构建的测试，不代替编译步骤
```

`include` 加载当前构建模块，`add_subdirectory` 进入带有 CMakeLists 的子目录；模块源码通过显式 `target_sources` 注册到核心目标。配置、编译和运行三个阶段出现的错误应分别定位：

| 阶段 | 常见现象 | 首先检查 |
| --- | --- | --- |
| 配置 | 缺JSON包、SDK头文件/库、BGE测试资产 | EdgeDependencies、tests、缓存中的路径和开关 |
| 编译/链接 | 缺符号、头文件找不到、库架构不匹配 | 所属模块的源码列表、target依赖与交叉工具链 |
| 运行 | 找不到.so、模型/索引不存在、后端未编入 | 动态库搜索路径、部署目录、conf与构建开关的一致性 |

根项目要求 CMake 3.16 起；当前 tokenizers-cpp 子模块声明 CMake 3.19 起，启用 BGE 时还要满足该依赖自己的构建要求。

## 8. 当前构建目标

| 目标 | 来源 | 生成条件 | 链接依赖/用途 |
| --- | --- | --- | --- |
| edge_voice_rag_core | src | 始终 | 汇总模块实现；公共依赖为zmq、JSON、Threads，附加依赖随开关启用 |
| edge_voice_rag | src/apps | 始终 | 核心库；文本分级响应入口 |
| rag_server | src/apps | 始终 | 核心库；早期BM25文本服务 |
| llm_server | src/apps | 始终 | 核心库；LLM普通/流式/取消服务 |
| agent_demo | src/apps | 始终 | 核心库；Rule Planner + Mock设备 |
| zmq_rep_server / zmq_req_client / rag_client | src/apps | 始终 | 直接链接zmq的联调程序 |
| sherpa_asr_demo | src/apps | SHERPA_ASR=ON | 核心库；ASR演示 |
| board_voice_assistant | src/apps | ALSA及三个SHERPA开关均为ON | 核心库；连续语音主入口 |
| retrieval_benchmark / voice_performance_summary | tools | 始终 | 核心库；检索评测与语音日志汇总 |
| unit_tests及各模块测试 | tests | BUILD_TESTING=ON | 默认40项；可选测试再按后端开关增加 |

表中 SHERPA_* 均指 `EDGE_ENABLE_SHERPA_*`。当前可选测试增量为 ALSA、ASR、TTS、VAD 各1项，BGE 4项；全部开启时共48项。RKLLM目前没有单独增加一项CTest，不能把48项测试理解为板端全部硬件已验证。

## 9. 功能与路径变量速查

| 变量 | 默认值 | 含义 |
| --- | --- | --- |
| BUILD_TESTING | ON | 是否创建项目测试目标 |
| EDGE_ENABLE_ALSA | OFF | 编入ALSA录放音/PCM实现 |
| EDGE_ENABLE_SHERPA_ASR | OFF | 编入sherpa ASR，添加ASR演示 |
| EDGE_ENABLE_SHERPA_TTS | OFF | 编入sherpa TTS |
| EDGE_ENABLE_SHERPA_VAD | OFF | 编入sherpa VAD录音器 |
| EDGE_ENABLE_BGE_EMBEDDER | OFF | 编入BGE/Dense及tokenizers-cpp/ORT依赖 |
| EDGE_ENABLE_RKLLM | OFF | 编入RKLLM后端 |
| RKLLM_SDK_ROOT | 空 | RKLLM SDK查找根目录 |
| SHERPA_ONNX_ROOT | 空 | sherpa C API头文件与运行库根目录 |
| ONNXRUNTIME_INCLUDE_DIR | 空 | 含onnxruntime_cxx_api.h的目录 |
| ONNXRUNTIME_LIBRARY | 空 | ONNX Runtime动态库完整路径 |
| TOKENIZERS_CPP_ROOT | third_party/tokenizers-cpp | tokenizer子模块目录 |
| BGE_MODEL_DIR | models/embedding/bge-small-zh-v1.5 | C++模型集成测试使用的BGE目录 |
| BGE_INDEX_DIR | vector_db/bge-small-zh-v1.5 | C++模型集成测试使用的向量索引目录 |
| SHERPA_ONNX_TTS_MODEL_DIR | models/tts/vits-melo-tts-zh_en | TTS集成测试模型目录 |

表中默认相对路径在CMake缓存中按项目根目录展开。BGE_MODEL_DIR/BGE_INDEX_DIR等测试路径变量不会自动改写运行配置中的 `bge_model_path/dense_*_path`。

选项保存在各自构建目录的 CMakeCache.txt 中；重新配置同一目录会沿用未显式覆盖的旧值。例如把语音配置切回纯PC配置时，需要显式传 `-DEDGE_ENABLE_ALSA=OFF` 等，或使用独立构建目录。

```bash
# 查看当前缓存中的普通变量及其说明
cmake -LH -N build

# 查看当前配置生成的目标
cmake --build build --target help

# 只列出测试，不执行
ctest --test-dir build -N

# 查看实际编译/链接命令
cmake --build build --target agent_demo --verbose
```

切换 x86/ARM 编译器或工具链时使用不同构建目录，分别保存SDK路径和编译产物。完整板端参数继续见[开发文档](development.md)。
