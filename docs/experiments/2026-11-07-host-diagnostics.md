# Host诊断基线记录：2026-11-07

## 目的与环境

为RK3588板端C++主线建立独立的普通Debug和ASan/UBSan对照，先验证现有测试的内存/未定义行为检查。此次不新增业务行为，不运行Python服务或训练，不访问真实声卡、NPU、传感器。

| 项目 | 本次实际值 |
| --- | --- |
| 环境 | Linux x86_64，WSL Ubuntu |
| 编译器 | c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| CMake | cmake version 3.28.3 |
| Git HEAD | `b1a9b13c1f4c399302b406269fa620375e203691` |
| 工作树 | 有未提交的文档、构建及用户原有修改；结果对应当前工作树，不能仅凭HEAD复现 |
| 诊断配置 | Debug；ASan/UBSan均ON；六个模型/硬件后端OFF |
| Runtime环境 | `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`；`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` |
| 测试超时 | 15组主线测试注册30秒进程超时；全量执行也指定30秒 |
| chunks SHA256 | `3da4298ce119a2334c7a88eb378a76f3a40002f36e1fa5685e4797e4e5c71a3c` |
| 68份核心/主线测试源文件清单摘要 | `1b784e459d125978b56b2a27d6f5d4d4deccd70bca77ac7337db70a7e72b48a9` |

完整源码逐文件SHA256、选中测试、构建文件SHA256和环境保存在本地 `build-host-asan-ubsan/validation-manifest.json`。构建目录被Git忽略，日志不会随新克隆自动出现；发布实验结果前应归档该目录中的证据或保存已提交的源码版本。

## 实际检查与结果

| 检查 | 本次结果 |
| --- | --- |
| 普通Debug默认配置/全量构建/CTest | 40/40通过 |
| ASan+UBSan配置与全量构建 | 通过；编译及Runtime链接探测成功 |
| 核心实现及测试编译命令 | 均含两种插桩、保留栈帧和UBSan失败即退出参数 |
| ASan+UBSan主线标签测试 | 15/15通过，无Sanitizer报告 |
| 主线各组重复20次 | 300/300次测试进程执行通过，无Sanitizer报告或超时 |
| ASan+UBSan全量CTest | 40/40通过，无Sanitizer报告 |
| 隔离ASan正向校验 | 故意堆越界被报告，进程退出码1，符合预期 |
| 隔离UBSan正向校验 | 故意有符号整数溢出被报告，进程退出码1，符合预期 |
| ASan-only / UBSan-only组合 | 配置、编译/链接能力探测和生成命令检查通过；未完整构建/运行各自测试集 |
| Release、BUILD_TESTING=OFF、诊断OFF | agent_demo构建成功，输出仍位于构建根目录；CTest无注册测试，编译命令不含插桩 |

两个正向校验小程序位于独立构建目录 `diagnostic-probes/`；它们有意触发错误，不能当成项目缺陷或混入项目通过率。

## 复现与原始证据

逐步命令与解释见[板端主线诊断指南](../diagnostics.md)。本地证据位置：

| 文件 | 内容 |
| --- | --- |
| `build-host-debug/Testing/Temporary/LastTest.log` | 普通Debug全量CTest |
| `build-host-asan-ubsan/build.log`、`final-build.log` | 初次与最终诊断构建 |
| `build-host-asan-ubsan/ctest-mainline.log` | 主线单次执行 |
| `build-host-asan-ubsan/ctest-mainline-repeat20.log` | 300次重复执行 |
| `build-host-asan-ubsan/ctest-all.log` | 全量插桩测试 |
| `build-host-asan-ubsan/diagnostic-probes/address.log`、`undefined.log` | 故障正向校验 |
| `build-host-deploy/build.log` | 不含测试/诊断的Release Agent构建 |

CTest的Temporary日志可能被下一次执行覆盖，命名日志与manifest应一起保存。

## 结论与后续边界

当前执行路径未暴露ASan/UBSan错误，独立诊断入口可用。测试既有正常完成，也有队列关闭与取消生成等路径；连续会话测试仍主要覆盖正常完成。

300次进程执行不是300轮语音，也没有达到演进报告建议的“固定种子1000轮正常/取消混合请求”。本次未用TSan检查数据竞争，未验证真实SDK回调线程身份、声卡、RKLLM/NPU/驱动或板端时延。

下一步补受控Mock：合成未返回时取消并启动新请求、满队列时取消/退出、回答完成与新录音同时就绪。先用行为断言稳定复现，再决定是否修改生命周期。
