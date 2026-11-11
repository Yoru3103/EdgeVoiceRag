# 板端真实IIO温湿度接入C++ Agent

更新：2026-11-10。本步骤验证DHT11 → LinuxVehicleDevice → 工具 → Rule Planner → 确认/取消的C++链路。板端由使用者实际操作，智能体不远程启动测试。

**当前状态：2026-11-10使用者确认“agent调用正常，已确认”。真实IIO文字Agent阶段按用户反馈通过；未提供新的逐轮输出或样本计数，不补写通过率。下一步见[独立语音输出验收](board-audio-output.md)。**

## 1. 这一步验证什么

驱动已取得用户板端读取结果，后续用户反馈测试正常。现在继续验证应用是否按IIO名称找到设备、正确换算单位，并通过现有工具/执行器返回结果。

现有agent_demo新增设备选择，默认仍是Mock。IIO模式直接读取Linux温湿度，不初始化声卡、ASR/TTS、检索模型或LLM。使用RuleAgentPlanner；既有DeepSeek或QLoRA资产不会参与此入口。

空调/LED仍为进程内逻辑状态。打开/关闭命令用于验证白名单和写操作确认，不代表真实空调或LED控制。

## 2. CLI与Host使用

```bash
./build-docs-check/agent_demo
./build-docs-check/agent_demo --help

# 真实IIO模式；选项次序不影响含义
./build-docs-check/agent_demo --device iio \
  --iio-root /sys/bus/iio/devices --iio-device-name edge_dht11
```

| 参数 | 默认值 / 行为 |
| --- | --- |
| --device mock或iio | 默认mock；Mock温湿度仍为28.5℃ / 60%RH |
| --iio-root DIR | 默认/sys/bus/iio/devices；仅--device iio允许 |
| --iio-device-name NAME | 默认edge_dht11；按name属性查找，不固定device编号 |
| --help / -h | 打印用法，不初始化设备 |

未知选项、未知后端、缺失/空值或在Mock模式指定IIO参数会返回2。初始化异常返回1；exit/EOF正常结束REPL返回0。每轮工具错误会打印Agent error并允许继续提问，所以会话退出0不能当作每轮全部通过。

当前Rule Planner支持温湿度查询、空调状态和简单开关请求；本轮不涉及LLM多步条件规划。待确认动作绑定同一console-session，需在默认30秒有效期内确认或取消；查询不会自动变成确认。

## 3. 已准备的独立ARM包

打包标签：`agent-iio-20261110-ff6tnvnc`。本机归档：`.cache/agent-iio-20261110-ff6tnvnc.tar.gz`。

此包来自独立Release交叉构建，六个可选模型/语音后端均关闭，BUILD_TESTING=OFF。它包含agent_demo、所需非系统动态库、构建清单和校验表，不包含模型、向量索引、内核模块或DTB，也不修改现有板端部署。

系统glibc/动态加载器仍由板端提供；已校验包内ELF为AArch64，实际ldd与运行结果由板端验证。libzmq是当前公共core目标传递的链接依赖，不要求启动ZeroMQ服务。

### 重新构建

.cache中的包只属于当前工作区，不随Git提交；新克隆需自行准备SDK并编译。工具链SDK位置按[开发文档](development.md)设置，使用独立目录：

```bash
cmake -S . -B .cache/build-rk3588-agent \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3588-buildroot.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DEDGE_ENABLE_ALSA=OFF -DEDGE_ENABLE_BGE_EMBEDDER=OFF \
  -DEDGE_ENABLE_RKLLM=OFF -DEDGE_ENABLE_SHERPA_ASR=OFF \
  -DEDGE_ENABLE_SHERPA_TTS=OFF -DEDGE_ENABLE_SHERPA_VAD=OFF \
  -DEDGE_ENABLE_ASAN=OFF -DEDGE_ENABLE_UBSAN=OFF
cmake --build .cache/build-rk3588-agent --target agent_demo -j4
```

部署时连同目标平台动态依赖一起准备；本次独立包内含libzmq、libstdc++、libgcc_s、libbsd、libmd，各自SHA256和来源写入build-manifest.json。不能将Host的同名.so放入ARM包。

### 在WSL传输

在项目根目录执行：

```bash
ssh root@192.168.10.100 \
  'cat > /tmp/agent-iio-20261110-ff6tnvnc.tar.gz' \
  < .cache/agent-iio-20261110-ff6tnvnc.tar.gz
```

### 在板端解压和校验

新目录必须尚不存在；若已创建过，另选新目录，不覆盖旧实验。

```sh
mkdir -p /userdata/edge_voice_agent
AGENT=/userdata/edge_voice_agent/agent-iio-20261110-ff6tnvnc
if mkdir "$AGENT" && tar -xzf /tmp/agent-iio-20261110-ff6tnvnc.tar.gz -C "$AGENT"; then
    cd "$AGENT"
    sha256sum -c bundle.sha256
    LD_LIBRARY_PATH="$AGENT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$AGENT/bin/agent_demo"
fi
```

校验表应全通过，ldd不得出现not found。归档SHA256为 `7d84001de34943dccb02b31c1c43b80f43407228af46c6edadb138fdd0017d11`。如果板端/tmp空间不足，可使用另一个独立可写目录接收，保留包和校验表。

## 4. 先确认驱动，再运行Agent

```sh
cat /sys/module/edge_dht11/version
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    echo "$d: $(cat "$d/name")"
done

LD_LIBRARY_PATH="$AGENT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$AGENT/bin/agent_demo" --device iio \
  --iio-root /sys/bus/iio/devices --iio-device-name edge_dht11
```

当前预期驱动版本0.2.2-window-qos，并能找到name=edge_dht11。无需重载已正常工作的驱动，也不需要额外运行CPU QoS常驻脚本，驱动自身管理测量窗口请求。

启动输出必须显示backend=iio和rule_agent_planner。温湿度查询的Tool应为get_cabin_environment，Observation含temperature_c、humidity_percent；与/sysfs毫单位换算后对应。多次读取可能命中2秒缓存，之后可随新测量变化。

## 5. 按顺序手动验收

| 顺序输入 | 预期行为 |
| --- | --- |
| 车内温度是多少 | 回答来自IIO的摄氏度和湿度；无Agent error |
| 车内湿度是多少 | 使用同一环境工具查询；单位为%RH |
| 空调状态 | 本次进程初始为关闭 |
| 打开空调 | 提示等待确认，尚未执行 |
| 取消 | 取消本次待确认动作 |
| 空调状态 | 仍关闭 |
| 打开空调 | 再次等待确认 |
| 确认 | 执行逻辑开启动作 |
| 空调状态 | 返回开启 |
| 关闭空调，然后确认 | 再次经过确认，逻辑状态关闭 |
| exit | 正常退出 |

“关闭空调，然后确认”表示先输入关闭空调，再单独输入确认。重启程序会重新创建内存空调状态，不能据此判断物理设备或状态持久化。

保存启动backend、温湿度Observation、确认/取消与状态查询输出。如果出错，保留Agent error和驱动日志；不要把IIO错误当作LLM JSON问题。本入口不使用LLM。

## 6. 验证结果与边界

- 默认Host构建成功，默认CTest本次43/43通过。
- linux_vehicle_device_tests新增3组跨模块行为：IIO值进入Rule工具响应并随后续读取更新；确认/取消与状态变化；非法IIO值/缺失设备的错误传播和恢复。
- 独立ASan+UBSan下该测试1/1通过。
- 本次实跑CLI共8个检查：参数错误4类、help、默认Mock、IIO确认工作流、IIO错误报告，均通过。使用临时目录模拟sysfs，没有访问板端。
- 独立AArch64配置/构建完成，归档构建与依赖信息。使用者随后在板端运行并确认Agent调用正常；该结论不扩展为真实空调控制或语音通过。

本次没有新增CTest条目，也没有修改工具schema、Planner提示词或LLM训练侧格式。原始Host日志位于 `.cache/agent-iio-cli-check-20261110-dw_jf1j_`（Git忽略）。

## 7. 通过后继续语音链路

文字Agent已由使用者确认通过。原先语音日志出现过ALSA播放参数提交失败，下一步先按[独立语音输出手册](board-audio-output.md)验证固定测试音、真实TTS和同一WAV对照，再接回board_voice_assistant测ASR→Agent→TTS/播放及打断。文字Agent通过不代表真实语音通过。

当前工作树接线以[硬件说明](hardware-dht11.md)为准。模型规划的QLoRA接入另行安排，不需要为了真实温湿度查询先切换模型。

本次构建、指纹和实际检查记录见[联调准备记录](experiments/2026-11-10-agent-iio-bringup.md)。
