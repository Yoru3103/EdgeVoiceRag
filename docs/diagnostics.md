# 板端主线诊断：从普通 Debug 到 ASan/UBSan

本阶段用PC/WSL上的Mock测试检查板端C++主线的内存和生命周期。入口仍围绕 `ContinuousVoiceSession → VoiceAssistant → TTS/播放工作线程`、Agent和本地问答；无需启动Python服务或ZeroMQ服务。

先对照[线程泳道](project-flow.md#threads)、[正常结束](project-flow.md#normal)与[起声打断](project-flow.md#interrupt)阅读。诊断构建不生成模型、不访问声卡、不控制真实设备。

## 1. 先理解两个工具检查什么

| 工具 | 可以检查的典型问题 | 在本项目中关注的位置 |
| --- | --- | --- |
| ASan（AddressSanitizer） | 越界访问、释放后使用、重复释放；Linux运行库还可检查泄漏 | PCM缓冲、跨线程任务、RequestState和后端资源的生命周期 |
| UBSan（UndefinedBehaviorSanitizer） | 有符号整数溢出、非法移位、不满足对齐要求的访问等已插桩未定义行为 | 长度/索引计算、数值转换附近的运算、指针和对象使用 |

编译器把检查插入项目代码，运行到相关路径时才有机会发现错误。两者可以一起使用；本项目让UBSan发现错误时退出，避免“只打印错误、CTest仍通过”。参数语义见[GCC官方Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)。

它们不检查全部C++错误，也不负责证明没有数据竞争或死锁。数据竞争后续使用独立TSan构建；等待卡住通过测试进程超时发现，再结合线程栈定位。ASan运行会改变时序和资源占用，因此诊断结果不作为性能基准。

## 2. 建立普通 Debug 对照

以下命令在Linux/WSL的项目根目录执行。Windows终端可以先进入：

```powershell
wsl.exe -d Ubuntu --cd /home/xx/EdgeVoiceRAG
```

使用新目录，不沿用语音或交叉编译的旧缓存：

```bash
cmake -S . -B build-host-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DEDGE_ENABLE_ASAN=OFF \
  -DEDGE_ENABLE_UBSAN=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-host-debug -j4
ctest --test-dir build-host-debug --output-on-failure --timeout 30
```

新目录中的六个模型/硬件后端默认OFF。默认构建仍需要ZeroMQ/JSON开发依赖；部分完整CTest依赖本地 `vector_db/chunks.json`，缺失时按[开发文档](development.md)准备，不用空文件替代。

**这一步回答：不加诊断时，当前代码是否已经失败？** 先保留对照，才能区分原有问题与诊断运行中暴露的问题。

## 3. 创建独立 ASan + UBSan 构建

```bash
cmake -S . -B build-host-asan-ubsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DEDGE_ENABLE_ASAN=ON \
  -DEDGE_ENABLE_UBSAN=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-host-asan-ubsan -j4
```

配置时应看到两个 `EDGE_*SAN_SUPPORTED` 编译/链接探测成功，以及 `Edge diagnostics: -fsanitize=address;-fsanitize=undefined`。探测不执行目标程序；SDK或交叉编译运行库是否能在板端工作仍需另外验证。

检查 `build-host-asan-ubsan/compile_commands.json` 中 `voice_assistant.cpp` 和 `voice_assistant_tests.cpp` 的编译命令，都应带有两个 `-fsanitize` 以及 `-fno-omit-frame-pointer`；UBSan还带 `-fno-sanitize-recover=undefined`。GCC动态链接时，可辅助检查：

```bash
ldd build-host-asan-ubsan/voice_assistant_tests
```

其中应包含libasan/libubsan。Clang可能使用静态Runtime，不能单凭ldd缺少这两个名字断言没有插桩。

**这一步回答：诊断是否真正进入了项目源码和测试程序？** 普通目录和诊断目录保存各自缓存、目标文件与二进制，避免混用。

## 4. 先运行板端主线的18项 Host 测试

```bash
# 先列出标签下有哪些测试，不执行
ctest --test-dir build-host-asan-ubsan -L board_mainline -N

# 设置诊断运行行为；遇到报告时失败并保留栈
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-host-asan-ubsan \
  -L board_mainline --output-on-failure --timeout 30
```

| 测试组 | 重点观察 |
| --- | --- |
| bounded_blocking_queue / streaming_sentence_buffer | 满/空等待、close唤醒、流文本分句 |
| voice_assistant | 合成/播放线程、后端失败、取消生成、停止与重复停止 |
| voice_input_pipeline / continuous_voice_session | 录音→ASR→回答装配、连续会话完成事件与退出 |
| local_rag_llm_backend / policy_routing_answer_backend | 本地检索与生成、策略选择、取消传播 |
| agent_executor / llm_agent_planner / agent_workflow | 工具解析、受约束执行、确认和工作流 |
| agent_answer_backend / agent_routing_answer_backend | 会话确认状态、设备意图、安全优先路由 |
| linux_vehicle_device / vehicle_tools / board_app_config | 临时sysfs读数、工具校验与配置契约 |
| voice_lifecycle / continuous_voice_session_lifecycle | 18类助手和12类连续会话受控场景 |
| voice_lifecycle_stress | 1000个混合场景 + 同一助手1000轮唯一ID请求 |

表中名称省略 `_tests` 后缀。Linux设备测试用临时文件模拟sysfs；它不证明真实DHT11或设备写入有效。

普通主线条目为30秒进程超时，压力条目为120秒；这是测试进程上限，不是业务请求超时策略。新增的受控场景、检查点和通过条件详见[C++测试手册](testing-guide.md)。

## 5. 重复检查与固定种子压力

先单次通过，再将两组受控生命周期测试各执行20次：

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-host-asan-ubsan -L lifecycle \
  --repeat until-fail:20 --output-on-failure
```

对应40次测试进程执行，每个进程包含18类助手或12类连续会话场景。重复执行增加不同调度出现的机会，检查点才负责固定具体危险顺序。

固定种子混合压力单独查看：

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-host-asan-ubsan -L stress -V
```

该条目先执行1000个混合生命周期场景，再在同一助手中执行1000个唯一ID请求（673正常、327取消）；后者逐轮核对残留文本、音频数量和取消目标。口径与详细断言见[测试手册](testing-guide.md#5-固定种子压力测试的两个阶段)，不能将场景数、进程数和请求数混用。

完整回归：

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-host-asan-ubsan --output-on-failure --timeout 30
```

## 6. 出错时怎么读

1. 先看第一条 `ERROR: AddressSanitizer` 或 `runtime error:`，不要从后续连锁错误开始。
2. 找调用栈中最靠近错误的项目源码位置；同时对照分配栈、释放栈与线程来源。
3. 单独复现失败测试，例如 `ctest --test-dir build-host-asan-ubsan -R '^voice_assistant_tests$' -V`。
4. 若只超时，没有Sanitizer报告，优先调查锁、队列、后端取消和future等待；超时不是内存错误诊断。
5. 若程序在进入测试前崩溃且没有有效栈，先用独立小程序检查Runtime启动/环境，再定位项目。不要关闭检查或忽略退出码来获得“通过”。

完整CTest日志位于 `build-host-asan-ubsan/Testing/Temporary/LastTest.log`，每次执行可能覆盖；复现前保存需要的日志。初始基线见[2026-11-07诊断记录](experiments/2026-11-07-host-diagnostics.md)，新增场景与录音衔接修复见[2026-11-08记录](experiments/2026-11-08-voice-lifecycle.md)。

## 7. 构建参数如何传播

```text
EdgeOptions.cmake
  └─ 声明 EDGE_ENABLE_ASAN / EDGE_ENABLE_UBSAN（默认OFF）
EdgeDiagnostics.cmake
  └─ 创建 edge_diagnostics INTERFACE，附加编译与链接参数
src/CMakeLists.txt
  └─ edge_voice_rag_core PUBLIC链接 edge_diagnostics
      ├─ 核心库中的项目C++实现插桩
      └─ 链接核心库的应用、工具和测试继承参数
```

INTERFACE目标只承载使用要求，不产出一个新业务库；目标链接选项会传播给最终可执行文件，避免只编译插桩却漏链接Runtime。[CMake官方target_link_options](https://cmake.org/cmake/help/latest/command/target_link_options.html)

直接只链接zmq的最小示例不继承该目标。预编译SDK不会因此被重新插桩；第三方源码目标也没有被全局强加编译参数。当前仅支持Linux/WSL的GCC/Clang工具链。

关闭诊断可回到 `build-host-debug`，或在另一个独立部署目录显式设置两个开关为OFF。此阶段不改变现有RK3588部署配置。

## 8. 完成范围与下一步

本阶段完成的是诊断入口、主线测试筛选与可复现运行方法。它不等于完成真实声卡、RKLLM SDK、NPU/驱动或板端性能验证。

受控Mock、混合压力与已复现的录音衔接修复已完成Host验证，功能与结果见[C++测试手册](testing-guide.md)。后续独立接入TSan，并在RK3588验证真实SDK/声卡行为；性能基准继续使用Release/RelWithDebInfo目录。
