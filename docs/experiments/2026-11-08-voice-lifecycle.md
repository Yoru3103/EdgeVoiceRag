# 板端主线Host生命周期测试记录：2026-11-08

## 本次完成内容

补齐18类VoiceAssistant受控生命周期场景、12类ContinuousVoiceSession场景和两阶段固定种子压力检查；补上队列clear唤醒生产者断言，并启用原先已定义但未调用的无活动请求打断测试。

逐项功能与通过条件见[C++测试手册](../testing-guide.md)。所有测试均为C++，Python/ZeroMQ联调路线未扩展，也未重新生成训练集、模型或索引。

## 实际发现与修复

原会话在answer_future先就绪时，无条件取消打断录音，并把barge_future结果放入ignored后丢弃。即使新问题已经录完，下一轮仍会重新调用普通录音；若已起声但还在录制，也可能提前中断该语句。

新增测试用“取消唤醒后才发布成功录音”强制选择answer先完成分支，不依赖sleep。修复前有两项失败：

- `completed_capture_survives_answer_first`
- `speech_callback_preserves_next_capture`

共同错误为 `normal recording failed: unexpected new normal recording`。修复位于[continuous_voice_session.cpp](../../src/voice/continuous_voice_session.cpp)：已经观察到起声时不取消该录音；无论先处理answer还是barge，只要取得成功capture就保存pending_capture，并按一次中断计数。未修改公共接口签名，也未改变max_turns计入中断轮的语义。

修复后，下一轮ASR输入为录音202，普通录音只调用一次，录音耗时34ms保留，两轮request_id不同，中断计数一次；会话结束时工作线程已停止。

阶段停止检查还复现了两个失败用例：`stop_during_asr` 与 `stop_from_recognized_callback`。停止标志已设置，但原实现仍继续派发回答；分别在ASR返回和RecognizedText回调返回后增加检查。修复后这两个受控路径不再启动回答，轮数为0，线程回收完成。

## 环境与输入身份

| 项目 | 本次实际值 |
| --- | --- |
| Host | Linux x86_64，WSL Ubuntu |
| 编译器 | c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| CMake | cmake version 3.28.3 |
| Git HEAD | `b1a9b13c1f4c399302b406269fa620375e203691` |
| 工作树 | 有未提交的本轮改动与用户原有修改；HEAD本身不足以复现 |
| 源码/构建输入 | 221份文件；逐文件指纹在manifest |
| 输入清单摘要 | `cd9f8ba450d76de46967c60fbf110fe6402a9ab92134bdfb40f26f79ff1260ef` |
| Host Debug | 六个模型/硬件后端OFF |
| Host诊断 | Debug，ASan/UBSan均ON，六个模型/硬件后端OFF |
| PC SDK | Release；ALSA、sherpa ASR/TTS/VAD、BGE ON；RKLLM OFF；诊断OFF |
| Runtime环境 | `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`；`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` |

manifest为 `build-host-asan-ubsan/validation-20261108.json`，保存源码SHA256、CTest配置和名称、关键模型/索引/WAV与Runtime库指纹、日志指纹及18类场景计数。构建目录被Git忽略，复现或发布结果时需另行归档原始证据与对应源码。

## 实际检查结果

| 检查 | 结果 |
| --- | --- |
| 修复前新会话测试 | 2个场景失败、8个通过，留存失败日志 |
| 阶段停止用例修复前 | 2个失败，其余10个通过；停止后仍启动回答，已留日志 |
| 修复后普通Debug全量 | 43/43通过 |
| ASan/UBSan全量 | 43/43通过，无Sanitizer报告 |
| ASan/UBSan两组生命周期各重复20次 | 40次进程执行通过，覆盖(18+12)×20=600次受控场景执行 |
| 固定种子混合生命周期 | 1000个场景通过，18类均被执行 |
| 同一助手持续请求 | 1000轮通过：673正常、327取消，1000个唯一request_id |
| PC可选SDK组合 | 51/51通过；board_voice_assistant编译链接成功 |
| BUILD_TESTING=OFF | 配置、agent_demo构建通过，无注册测试，程序根目录输出保留 |
| 新增测试额外语法检查 | -Wall/-Wextra/-Wpedantic检查编译成功；说明见下文 |

压力检查除成功/失败状态外，逐轮核对取消ID、PCM数量和残留文本；TTS暂停、播放暂停与队列占用使用条件变量检查点控制。Mock/future等待上限5秒，普通主线测试进程30秒，压力进程120秒。这里没有测量板端性能或内存峰值。

PC SDK测试使用真实TTS/BGE模型与本地VAD WAV。ALSA使用null设备、VAD从内存PCM读取、IIO使用临时sysfs；ASR测试仅验证参数/缺失模型等错误处理，不是实际转写质量评测。SDK组合本次未运行Sanitizer。

额外警告检查中，新旧连续会话测试都会报告既有 `InterruptibleAudioRecorder` 隐藏零参recordUtterance重载的警告；已用未修改的旧测试对照确认，不归因于新增用例，本轮未调整该公共接口。

## 证据与复现

运行步骤见[测试手册](../testing-guide.md#6-如何运行)，ASan/UBSan准备见[诊断指南](../diagnostics.md)。

| 本地文件 | 内容 |
| --- | --- |
| `build-host-debug/session-before-fix.log` | 修复前两个失败场景的原始CTest输出 |
| `build-host-debug/session-stop-before-fix.log` | ASR与识别回调停止边界的修复前失败 |
| `build-host-debug/ctest-all-20261108.log` | 最终普通Debug 43项 |
| `build-host-asan-ubsan/asan-all-20261108.log` | 最终诊断43项汇总 |
| `build-host-asan-ubsan/asan-all-detail-20261108.log` | 包含每项断言输出及压力计数的完整日志 |
| `build-host-asan-ubsan/asan-lifecycle-20261108.log` | 两组生命周期各20次 |
| `build-board-pc/ctest-all-20261108.log` | PC SDK 51项 |
| `build-host-asan-ubsan/validation-20261108.json` | 本次输入、环境及证据指纹 |

## 完成边界

本轮Host行为、受控生命周期、混合压力与ASan/UBSan检查已完成。没有用这些结果宣称所有线程调度都安全；TSan、真实RKLLM回调线程、真实声卡/IIO、RK3588 NPU/驱动及板端质量/时延不在本次实测范围内。

所有结果对应当前工作树和本机已有资产，新克隆需按文档准备依赖与资产；历史2026-11-07记录保留原始40项/15组口径，不与本次43项/18项混用。
