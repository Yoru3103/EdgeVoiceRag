# ASR / TTS迁移RK3588 NPU的路线

核对日期：2026-11-11。用户询问量化并部署NPU是否可行。本页是接入计划与当前边界，不是已完成的NPU功能。

## 当前项目状态

ASR为SenseVoice INT8 ONNX，TTS原为Melo INT8 ONNX，均显式选择provider=cpu。当前ARM sherpa SDK构建缓存SHERPA_ONNX_ENABLE_RKNN=OFF。已有RKLLM用于LLM，不等于ASR/TTS已有RKNN后端。

量化改变数值表示，RKNN转换/运行时决定是否在NPU执行；FP16也可作为NPU正确性基线，不必先INT8。本轮TTS Host对照还实际发现同Melo FP32快于现有INT8，不能将INT8作为速度保证。

## ASR：优先复用已经支持的SenseVoice RKNN

[sherpa官方文档](https://k2-fsa.github.io/sherpa/onnx/rknn/models.html)提供当前2024-07-17 SenseVoice系列的RK3588模型、导出代码和provider=rknn使用方式，并有5/10/15/20/25/30秒等固定长度版本。当前项目仍需准备启用RKNN的匹配SDK/Runtime，再把模型选择与provider接到应用配置。

输入短于固定长度会补齐，超过会截断。因此要配合VAD长度与padding制定策略，不能静默丢掉长句，也不能默认20秒固定形状对所有短命令都最省时。特征提取、解码等CPU阶段仍应分别计时。

验收顺序：
1. 固定一批真实16k录音、文本标签和heldout，保留CPU识别结果、CER/命令词/数值准确率与延迟。
2. 选同源RKNN参考模型，确认Toolkit/Runtime/板端rknpu驱动兼容；先独立ASR，再接会话。
3. 验证短音频、边界长度和超长处理；CPU/NPU对照不能仅以程序未报错通过。
4. 若进一步PTQ，从可追溯浮点图建立对照，用真实音频/特征校准，保留量化后的识别与延迟/内存结果。

## TTS：需要按当前图适配

[瑞芯微官方MMS-TTS示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/mms_tts)支持RK3588，示例采用英文mms-tts-eng的encoder/decoder拆分，默认非量化fp路线，也有i8转换选项。这证明存在TTS上NPU的工程路线，不能证明当前中文Melo可原样转换。

Melo/VITS需核对动态文本/音频长度、时长预测、alignment/latent处理、随机输入和flow/vocoder算子。可先保留中文前端与动态控制在CPU，将适配后的固定形状计算子图迁移NPU，再做padding/cropping和输出长度验证。具体拆分与支持范围以实际RKNN2转换结果为准，不预先宣称整模型支持。

先建立ONNX浮点→RKNN FP16的子图数值/音频对照，再探索INT8校准与敏感层策略。听清温湿度数值、单位、确认/取消词和手册内容，比只看到RTF变小更重要；质量记录包含可懂度、失真、读音和必要的人工试听。

## 与现有主线共同运行

用户日志中RKLLM Runtime为1.2.3，rknpu驱动0.9.8。未来新增RKNN依赖需核对整套兼容关系，在独立构建与部署目录验证，不直接覆盖已正常工作的系统运行库/驱动。

LLM流式生成与TTS会重叠；二者都使用NPU时需测单独与共同运行的首播/P95、核分配和等待，不能把单模型加速简单相加。底层buffer/DMA/Cache/IOMMU学习可随真实Runtime接入推进，但目前没有这些自建能力。

优先顺序仍是：完成[TTS板端延迟验证](tts-optimization.md) → 固定语音基线与打断/回收 → SenseVoice RKNN → TTS子图与PTQ/误差分析。当前用户要求先处理TTS，ASR迁移没有在本轮擅自开始。

2026-11-12：用户已认可当前TTS速度，主线进入[固定语音基线与打断/回收](board-mainline-acceptance.md)。思考输出继续挂起；本轮新增半双工开关供外放稳定测量，随后推进SenseVoice RKNN独立对照。
