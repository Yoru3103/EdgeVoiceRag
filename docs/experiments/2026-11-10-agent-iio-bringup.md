# IIO Agent入口：本地验证与用户板端确认

日期：2026-11-10。本轮继续板端C++主线，准备真实温湿度→工具→Rule Planner→确认/取消的独立验收入口。板端由使用者执行，本轮没有远程运行Agent或采集传感器。

## 改动

- agent_demo增加--device mock/iio、--iio-root、--iio-device-name及help，默认仍为原Mock温湿度。
- 复用LinuxVehicleDevice、ToolRegistry、RuleAgentPlanner、AgentExecutor；打印所选设备后端与工具Observation。
- 明确空调/LED仍为进程内状态，写操作沿用确认机制。
- linux_vehicle_device_tests新增IIO到Rule响应、确认/取消/查询状态、错误传播及恢复3组场景，沿用既有CTest目标。

## 当前执行的检查

| 检查 | 实际结果 |
| --- | --- |
| 默认Host完整构建 | 通过 |
| 默认CTest | 43/43通过 |
| ASan+UBSan聚焦linux_vehicle_device_tests | 1/1通过 |
| Host CLI实际进程 | 8/8检查通过 |
| 全后端既有ARM目录agent_demo构建 | 通过，但依赖会传递语音/模型SDK |
| 独立ARM目录，六个可选后端OFF / BUILD_TESTING=OFF | 配置和agent_demo构建通过 |
| 部署包中二进制与动态库 | 校验为AArch64；递归依赖已收集 |
| 新版板端Agent运行 | 使用者确认“agent调用正常，已确认”；无新增逐轮原始输出或样本数量 |

Host CLI检查为4类参数错误、help、默认Mock、IIO确认工作流、IIO错误报告。记录位于.cache/agent-iio-cli-check-20261110-dw_jf1j_，使用临时sysfs样式目录，不代表真实DHT11通过。

## 部署资产

- tag：agent-iio-20261110-ff6tnvnc
- archive：.cache/agent-iio-20261110-ff6tnvnc.tar.gz
- archive SHA256：7d84001de34943dccb02b31c1c43b80f43407228af46c6edadb138fdd0017d11
- bytes：1614995
- agent_demo SHA256：480464a82872812b8fb3da8630c0c5da51f3037eab18b6fce09ccf72e5dcf60a
- libs：libzmq.so.5, libstdc++.so.6, libgcc_s.so.1, libbsd.so.0, libmd.so.0
- build：/home/xx/EdgeVoiceRAG/.cache/build-rk3588-agent-20261110-1z8_q0zp

包内build-manifest.json保存构建类型、编译器、缓存/源码/二进制/库指纹和Git状态；bundle.sha256用于板端校验。系统glibc及动态加载器仍由板端提供。包没有模型或向量资产，不改内核/设备树，必须解压到独立新目录。

## 验收与后续

部署和逐句验收见[主线联调手册](../board-agent-bringup.md)。先确认backend=iio、get_cabin_environment返回真实IIO换算值，再验证写动作的等待确认、取消不执行、确认后状态可查询。

该入口不初始化ASR/TTS/LLM，不能用文字测试通过代替完整语音通过。使用者已确认文字Agent正常；后续先按[独立输出手册](../board-audio-output.md)核对ALSA与TTS输出格式，再进入ASR→Agent→TTS/播放及打断链路。
