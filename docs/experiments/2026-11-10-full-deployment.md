# RK3588完整恢复包：2026-11-10

用户删除旧EdgeVoice部署目录后，重新准备完整运行资产。没有登录板端、加载驱动、改设备树或录音；由使用者解压并实际验收。

## 本轮执行

- 现有build-rk3588-bge重新配置：Release、六个后端全ON、BUILD_TESTING OFF、ASan/UBSan OFF；6个用户态程序重新构建成功。
- 修正驱动从当前源码复制到独立目录编译，readelf确认AArch64、version=0.2.2-window-qos、vermagic=6.1.141 SMP mod_unload aarch64。
- 驱动帧解析普通Host验证10组通过；采集/清理与Sanitizer采集回归2项通过（每项9类场景）。
- 4项启动脚本行为检查通过，覆盖工作目录、含空格路径、中文文本参数、默认IIO和自选Mock。
- 11个非系统动态库递归DT_NEEDED闭包检查，全部为AArch64；不打包系统glibc/加载器。
- BGE模型、tokenizer、chunks和embeddings的SHA256均与索引元数据一致，向量文件大小与rows/dimension一致。
- 47个文件/链接（含校验表），归档流式读取逐项校验通过；模型、运行库和模块均已包含，不依赖旧目录。
- 本轮没有改业务逻辑或模型资产，未重复Host全量CTest；前一轮结果见语音输出实验记录，不作为本轮新执行。

## 包信息

- archive：/home/xx/EdgeVoiceRAG/.cache/rk3588-full-20261110-occkka4e/EdgeVoiceRAG-rk3588-20261110-full.tar.gz
- archive_sha256：d93867bcf851f067bd76fc054e0c6fbd0e85c598d359fa97f45952de34c8ff5a
- bytes：1969932367
- uncompressed_file_bytes：2524150809
- library_count：11
- driver_sha256：d4decdd369086027f1c9fe8154407427e78d6c2a31c54f9d799fcf8d52aaa05d
- driver_version：0.2.2-window-qos

6个程序：board_voice_assistant, agent_demo, audio_output_check, retrieval_benchmark, voice_performance_summary, bge_retrieval_profile。模型沿用现有DeepSeek/SenseVoice/Melo/Silero/BGE；未部署QLoRA adapter。包内build-manifest.json保存SDK/cache、编译器、源码和资产指纹。

主程序、模型、驱动全量归档校验不代表板端完整语音已通过；此前ALSA错误仍待独立输出结果定位。操作见[完整恢复说明](../rk3588-deployment.md)，包内同附DEPLOY.md。
