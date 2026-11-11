# TTS延迟优化与板端验证

日期：2026-11-11。保持原来的板端C++主线，先处理TTS合成等待；模型思考内容播出问题按用户要求暂缓。所有板端测试由使用者操作。

## 1. 已定位的耗时

使用者提供的温湿度语音日志中，Agent工具成功返回31.8℃ / 32%RH，播放也成功，采样率冲突已不再阻止这一轮输出：

| 指标 | 用户板端本轮值 |
| --- | --- |
| ASR | 250.64ms |
| 回答后端 | 24.213ms |
| 首文本 | 24.304ms |
| TTS累计合成 | 14511.653ms |
| 首音频就绪 / 首次调用播放 | 14536.057 / 14536.195ms |
| 播放 | 4398.524ms |
| 句子 / 音频队列push等待 | 均为0 |

TTS是该轮首播等待的主要瓶颈。capture_elapsed_ms约9.31秒包含等待用户说话和录音，不应直接当作算法延迟。第二轮“讲个笑话”没有完整PERF_JSON，不能从该片段生成第二轮耗时或通过率。

## 2. 首选保留Melo，换用同模型FP32

Host相同文本“当前车内温度为31.8摄氏度，湿度为32.0%。”，6线程、每组新进程、模型加载单独记录、1次预热后5次：

| 配置 | 合成P50 | P95 | RTF P50 | 进程峰值RSS |
| --- | --- | --- | --- | --- |
| 原Melo INT8 | 5702.19ms | 5837.60ms | 1.3062 | 343608KiB |
| 同Melo FP32 | 1032.64ms | 1039.46ms | 0.2372 | 454816KiB |
| Icefall AISHELL3备选 | 90.33ms | 103.86ms | 0.0186 | 145440KiB |

同Melo FP32在这台Host约快5.52倍，RSS增加约109MiB；这是实际本地诊断，不是RK3588收益。FP32模型约170.4MB，原INT8约53.5MB。备选模型约30.5MB、输出8kHz，Melo输出44.1kHz；二者音色/频带不同，不把较快模型描述成相同音质的无损替换。

实际检查Melo浮点图有683个FLOAT initializer；现有INT8图含249个ConvInteger、201个DynamicQuantizeLinear及5个MatMulInteger。它并非所有算子都变成整数，也没有因文件更小就自动获得更快CPU执行。动态量化/整数内核效率是后续算子profiling的调查方向；本轮未做逐算子trace，不把节点数量当作根因证明。

初步1/2/4/6线程扫描及最终串行5次原始结果均保留。P95样本少，当前用于板端选路诊断，正式性能报告再扩大固定样本并记录环境。

## 3. 配置与兼容

当前主线默认conf新增：

```ini
tts_model_dir=models/tts/vits-melo-tts-zh_en
tts_model_file=model.onnx
tts_speaker_id=0
tts_num_threads=6
```

tts_model_file是资源目录内的文件名，禁止空值、目录和越界路径。未包含新参数的旧conf仍默认model.int8.onnx / speaker0，保持兼容；说话人上界由实际模型创建时检查。

主程序、audio_output_check与tts_benchmark统一经makeBoardTtsConfig装配模型、词表、date/number FST、线程和说话人，speed保持1.0。不修改播放速度来制造推理加速。

可选低延迟中文配置为config/board_rk3588_tts_fast.conf：官方vits-icefall-zh-aishell3/model.onnx，speaker66，4线程，8k输出。声音经既有ALSA edge_voice_16k转换到16k硬件，升采样不会恢复缺失频带；需要你试听读音与音质后选用。Melo和备选均为CPU provider，本轮没有新增NPU能力。

## 4. 更新包安装

将TTS更新包放到板端/userdata，先退出语音和其他占用设备的程序：

```sh
cd /userdata
tar -xzf EdgeVoiceRAG-rk3588-20261111-tts-update.tar.gz
cd EdgeVoiceRAG-tts-update
sh ./apply.sh /userdata/EdgeVoiceRAG
```

需要已经完成duplex修订的完整部署。包带FP32 Melo权重、轻量备选资源、更新的主程序/探针/基准和所需说明；原Melo INT8、ASR/VAD/BGE/LLM和驱动保留。

安装前校验新增文件和运行库/词表等前提依赖；会将被替换文件和原conf备份到新的backup-tts-XXXXXX目录。只更新现有主conf的tts_model_dir、tts_model_file、tts_speaker_id，保留你调整过的barge_vad_threshold、max_turns、声卡等其他参数；tts_num_threads也保留。备选conf也会从保留下来的主conf生成，仅替换模型/说话人和线程数为4，因此继承当前打断阈值和设备设置。

新的bundle.sha256校验不可变交付文件；主conf因保留用户参数单独写入config-runtime.sha256，可在每次实验前重新记录。未自动停止/启动程序、重载DHT11或改设备树。

## 5. 先听，再测同模型对照

```sh
cd /userdata/EdgeVoiceRAG
. ./env.sh
grep -E '^tts_(model_dir|model_file|speaker_id|num_threads)=' config/board_rk3588.conf
./run-audio-check.sh --mode tts --repeat 2
```

应两次听到测试句，启动/探针输出显示model.onnx。接着从相同文本独立比较CPU合成，不录音、不播放、不加载LLM：

```sh
RUN=$(mktemp -d ./tts-perf-XXXXXX)
sha256sum ./tts_benchmark config/board_rk3588.conf benchmarks/tts_cases.json \
  models/tts/vits-melo-tts-zh_en/model.onnx \
  models/tts/vits-melo-tts-zh_en/model.int8.onnx > "$RUN/inputs.sha256"
uname -a > "$RUN/kernel.txt"
for MODEL in model.int8.onnx model.onnx; do
    ./tts_benchmark --config config/board_rk3588.conf \
      --model-file "$MODEL" --threads 6 \
      --text "当前车内温度为31.8摄氏度，湿度为32.0%。" \
      --warmup 1 --runs 3 > "$RUN/$MODEL.log" 2>&1
    RC=$?
    echo "$MODEL rc=$RC"
    cat "$RUN/$MODEL.log"
done
```

基准的warmup样本仍输出，first_inference=true保留首调用时间；summary只统计measured。RTF=合成毫秒/(输出秒数×1000)，小于1表示本次合成快于实时。模型读取指纹会访问文件，model_load_ms不是冷磁盘启动测量。RSS是Linux进程累计峰值KiB，不是单次模型增量。

默认不指定--text时使用benchmarks/tts_cases.json四条固定文本；正式对照可--runs 30，先完成3次快速诊断再决定。--model-dir、--model-file、--speaker-id、--threads只覆盖这次工具运行，不修改conf。

## 6. 接回完整语音

```sh
cd /userdata/EdgeVoiceRAG
./run.sh
```

说“现在多少度？”，保留启动中的TTS model/thread/speaker与完整PERF_JSON，重点对照first_audio_ready_ms、tts_elapsed_ms、首播、RSS与是否可听。独立基准快不等于完整场景同样快：还需考虑VAD采集、CPU亲和性/频率、负载与线程竞争。

想试听备选时，先用探针：

```sh
./run-audio-check.sh --config config/board_rk3588_tts_fast.conf --mode tts --repeat 2
# 确认音质与参数后，主程序可选此配置
./run.sh config/board_rk3588_tts_fast.conf
```

恢复原Melo INT8只需在主conf中改tts_model_file=model.int8.onnx、tts_speaker_id=0并重启；安装脚本也保留了原文件备份。

本轮保留了“%被Melo词表忽略”的原始警告，需听数字/单位是否完整；没有将合成成功当作发音准确率或MOS。用户报告的思考内容播出另列后续，未更改Planner、LLM提示词或回答过滤。

## 7. 后续主线与NPU

先由使用者确认TTS改动在板端确实降低延迟，再做完整固定语音性能基线、打断/线程回收验收；随后接SenseVoice RKNN与CPU精度对照。Melo子图NPU与PTQ校准/误差分析是进一步工作，不能把本轮FP32切换描述为已完成量化优化。

NPU可行性与接入边界见[语音NPU路线](speech-npu-roadmap.md)。轻量模型来源：[sherpa官方模型说明](https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/vits.html)，本地保存下载URL、归档和模型SHA256；官方RTF表只作选型线索，不作为本项目板端结果。

## 本次交付位置

归档：`.cache/rk3588-tts-20261111-6co5z1q7/EdgeVoiceRAG-rk3588-20261111-tts-update.tar.gz`，191444882字节（约191MB），SHA256：3841b3587d9766254176b5bd44288b7170c0f8a0c4468d487f83520d8e1d5d59。旁边同名.sha256可用于传输校验；包只属于当前工作区，不随Git提交。

本轮证据、构建与检查见[实验记录](experiments/2026-11-11-tts-optimization.md)。


## 用户板端确认后的阶段更新（2026-11-12）

用户提供fast实际日志：温湿度查询TTS320.667ms、首次调用播放345.536ms；澄清回合TTS累计501.061ms、首次调用播放236.759ms。用户认可当前速度可继续主线。默认配置片段不是同输入/同句数，未据此计算加速比。

本阶段不要求再次完成INT8/FP32全套对照才推进；接下来是[固定语音基线与打断/回收](board-mainline-acceptance.md)，随后SenseVoice RKNN。思考过滤按用户明确要求继续挂起，不阻塞主线；记录见[阶段实测](experiments/2026-11-12-board-mainline-control.md)。
