# 2026-11-10：ASoC采样率对称冲突与Rule环境问句修复

证据为使用者在板端手动运行后提交：ASR识别“现在多少度”，Router选择environment_query，但Rule返回能力提示而未执行工具；内核随后明确报unmatched rate symmetry 16000/44100与-EINVAL。此前“采样率/period/打断并发均待定位”的状态由这份新日志推进；本次拒绝的直接原因已明确为硬件录放采样率不一致。

## 修改与本轮验证

- RuleAgentPlanner复用EnvironmentQuery分类，保持原有温湿度关键词兼容，不修改工具schema、确认语义或训练提示词。
- 新增项目私有config/alsa-rk3588.conf：加载系统定义，edge_voice_16k使用plug + 内置linear、slave.rate=16000；物理PCM仍为ES8388。
- 两路设备配置改为edge_voice_16k，包装启动脚本设置ALSA_CONFIG_PATH；直接运行时须先source env.sh。
- C++播放/停止线程及硬件协商代码本轮未修改；VAD/ASR仍16k，TTS源仍44.1k，由ALSA实际转换。
- 默认Host构建及CTest43/43通过，ALSA-only构建及CTest45/45通过。
- 变更profile明确使用linear后，单独ALSA回归再次1/1通过；同一测试含持有16k采集handle时播放44.1k、非零底层文件约16k帧和源PCM不变。
- Router→Rule→工具读数新增口语问句与读数更新语义断言；IIO集成后续刷新改用“现在多少度”。
- 实际Host TTS经同一profile转换并重复播放2次通过，源44100Hz、131072帧；16k file/null端94976帧，允许重采样尾部少量延迟。该结果不代表真实声卡输出或优化收益。
- ARM Release全后端、BUILD_TESTING OFF下6个交付目标编译链接成功。
- 更新包在完整旧部署的本地副本上执行apply.sh，备份旧二进制、替换和完整SHA256校验均成功；未启动ARM程序。
- 新启动脚本的ALSA_CONFIG_PATH与含空格路径行为检查通过。
- 两个归档逐文件校验通过，DHT11驱动未修改，仍为用户已验证的0.2.2-window-qos；不要求重新加载。

构建/CTest原始日志：.cache/duplex-fix-build-20261110.log；TTS日志目录：/home/xx/EdgeVoiceRAG/.cache/duplex-tts-20261110-5z2q3iam。更新安装与ARM构建日志位于/home/xx/EdgeVoiceRAG/.cache/rk3588-duplex-20261110-modqq7_7。

## 交付资产

- update_archive：/home/xx/EdgeVoiceRAG/.cache/rk3588-duplex-20261110-modqq7_7/EdgeVoiceRAG-rk3588-20261110-duplex-fix-update.tar.gz
- update_bytes：13371793
- update_sha256：47c724da5167cd66ad6f60bf5981bd1975813fdcb4cfd2db70a24455d1777fbf
- full_archive：/home/xx/EdgeVoiceRAG/.cache/rk3588-duplex-20261110-modqq7_7/EdgeVoiceRAG-rk3588-20261110-duplex-fix-full.tar.gz
- full_bytes：1969939165
- full_sha256：43b00cb916c0753ec464c061238607fda1a829e5e94116bac16c0d58aa8298d0
- driver_sha256：d4decdd369086027f1c9fe8154407427e78d6c2a31c54f9d799fcf8d52aaa05d

旧完整归档保持原样，新版使用duplex-fix文件名。板端需由用户复测“现在多少度”的工具调用、可听播报与播放时打断，读取/proc/asound底层hw_params确认rate相同。没有将Host文件端结果记为板端通过，未实现声学回声消除。

操作见[修复与更新说明](../rk3588-duplex-fix.md)。
