# VirtualMate

**一个有形象的边缘端 AI Agent** —— 在 Linux 设备上跑一个带 VRM 3D 形象的智能体，通过飞书、QQ 机器人等 IM 通道对话，支持语音交互、本地工具执行与可扩展技能，让 AI 不只是一段文字，而是一个能看、能听、能动的虚拟人物。

基于 [TuyaOpen](https://github.com/tuya/TuyaOpen) SDK 与 DuckyClaw Agent 架构，同一套代码可部署到 **Ubuntu 桌面**、**树莓派**、**RK3576** 及各类 ARM Linux 开发板。                （直接写AI Agent）

---

## 你能用它做什么

| 场景 | 效果 |
|------|------|
| IM 聊天 | 飞书 / Telegram / Discord / QQ 机器人等多通道接入，统一消息总线驱动 Agent 回复 |
| 语音对话 | 唤醒词 + 云端 ASR/TTS，数字人边说话边做表情与动作 |
| 3D 形象 | 加载 VRM 模型，VRMA 动作切换，场景天空盒，弹簧骨与情绪驱动 |
| 本地工具 | 读写文件、定时任务、远程执行（Linux）、OpenClaw 网关控制等 MCP 工具 |
| 持久记忆 | `SOUL.md` 人格、`USER.md` 用户画像、`MEMORY.md` 长期记忆，跨会话不丢上下文 |
| 技能扩展 | Markdown 技能文件，Agent 按需加载，也可对话中动态创建 |

---

## 技能系统

首次启动时，会自动在技能目录安装三个内置文件（若尚不存在）：

| 文件 | 说明 |
|------|------|
| `weather.md` | 天气查询（步骤里会调用 `web_search`） |
| `daily-briefing.md` | 每日简报（同上） |
| `skill-creator.md` | 教 Agent 如何创建新技能 |

其中 weather / daily-briefing **依赖 `web_search` 工具**；当前版本尚未注册该 MCP 工具，相关步骤需自行接入搜索能力或改写技能内容。`skill-creator` 与基于 `write_file` 的自定义技能可正常使用。

### 技能目录

| 平台 | 路径 |
|------|------|
| Linux（默认，`CLAW_USE_SDCARD=0`） | `/skills/` |
| SD 卡模式（T5AI 等，`CLAW_USE_SDCARD=1`） | `/sdcard/skills/` |

Linux 上需确保目录可写，例如：

```bash
sudo mkdir -p /skills /config /memory
sudo chown -R "$USER" /skills /config /memory
```

（人格与记忆同理：`/config/SOUL.md`、`/memory/MEMORY.md`，或 SD 卡下的 `/sdcard/config/`、`/sdcard/memory/`。）

### 工作机制

1. 每轮对话构建系统提示时，会扫描技能目录并生成**摘要列表**。
2. 当用户请求匹配某技能时，Agent 必须用 **`read_file` 读取完整 `.md` 文件** 后再执行，不能仅凭摘要猜测。
3. 新建或修改技能文件后，**下一条用户消息**即生效，无需重启。

### 自定义技能格式

文件名建议小写、连字符，如 `my-skill.md`。章节标题建议与内置技能保持一致（英文）：

```markdown
# 我的技能

简短描述这个技能做什么。

## When to use
用户说什么话时触发。

## How to use
1. 调用哪些工具（如 get_current_time、cron_add）
2. 按什么步骤执行

## Example
用户: "……"
-> tool_name ...
-> 回复示例
```

也可以直接对 Agent 说「帮我创建一个 xxx 技能」，它会用 `write_file` 写入 `/skills/<name>.md`（SD 卡模式下为 `/sdcard/skills/<name>.md`）。

---

## 快速上手

### 1. 拉取 SDK 子模块

本仓库依赖 **TuyaOpen** 作为底层 SDK（构建系统、网络、音频、AI 组件、板级支持）：

```bash
git clone --recursive <本仓库 URL>
cd <项目目录>

# 若已 clone 但未拉子模块：
git submodule update --init --recursive
```

### 2. 初始化构建环境

```bash
cd TuyaOpen
. ./export.sh    # 创建 Python venv，导出 OPEN_SDK_ROOT 等环境变量
cd ..
```

跳过交互式平台更新提示（可选）：

```bash
mkdir -p .cache && touch .cache/.dont_prompt_update_platform
```

### 3. 选择板级配置

```bash
cd TuyaOpen
python3 tos.py config choice
```

常用配置（也可直接复制）：

| 配置 | 适用 |
|------|------|
| `config/Ubuntu.config` | Linux x64 桌面（默认推荐开发调试） |
| `config/RaspberryPi.config` | 树莓派 |
| `config/DshanPi_A1.config` | DshanPi A1 等 ARM Linux 板 |
| `config/TaishanPi_3.config` | RK3576 / 立创 TaishanPi_3（ES8388 板载音频） |

### 4. 详细配置（VRM / 音频 / AI）

```bash
python3 tos.py config menu
```

在菜单中重点检查：

| 配置项 | 说明 |
|--------|------|
| `VRM_MODEL_PATH` | 3D 模型路径，支持 VRM / PMX / GLB / GLTF / FBX / OBJ |
| `VRM_ANIM_DIR` | `.vrma` 动作文件目录，运行时可切换 |
| `VRM_SCENE_PARENT_DIR` | 场景目录，子文件夹为 2D 背景或立方体贴图天空盒 |
| `VRM_WINDOW_WIDTH` / `HEIGHT` | 渲染窗口尺寸 |
| `ENABLE_AUDIO_ALSA` | Linux 桌面使用系统音频（Ubuntu 配置已默认开启） |
| `ALSA_DEVICE_CAPTURE` | ALSA 录音设备名（麦克风），见下文 [Linux 音频配置指南](#linux-音频配置指南) |
| `ALSA_DEVICE_PLAYBACK` | ALSA 播放设备名（扬声器/耳机） |

**硬件音频板**：若使用 **YD1036** 等外接音频模块，在 `config menu` 的 Audio Codecs 相关选项中选择对应 codec 名称，并确保板级 Kconfig 已 `select ENABLE_AUDIO_CODECS`。

> 换 USB 声卡、圆头耳麦、蓝牙耳机等 **一般不用改 C 代码**，只需改 `app_default.config` 中的 ALSA 设备名并重新编译。完整说明见 [Linux 音频配置指南](#linux-音频配置指南)。

准备资源文件（示例路径，可按需添加或替换）：

```
resources/
├── models/          # 你的 .vrm 模型
├── animations/      # .vrma 动作
└── scenes/          # 场景子目录---2D或天空盒，怎么命名


```

### 5. 编译

```bash
python3 tos.py build
```

产物位于 `TuyaOpen/dist/`，Linux 目标会生成可直接运行的 ELF 二进制。

### 6. 配置密钥（首次必做）

```bash
cp include/tuya_app_config_secrets.h.example include/tuya_app_config_secrets.h
```

填入 Tuya 云 `PRODUCT_ID`、`UUID`、`AUTHKEY`，以及要使用的 IM 通道凭据。此文件已被 gitignore，不会误提交。

---

## 进阶配置

### IM 通道与云端授权

在 `include/tuya_app_config_secrets.h` 中设置：

```c
#define IM_SECRET_CHANNEL_MODE  "feishu"   // feishu | telegram | discord | qqbot
```

并填写对应 Token / App ID / Secret。也可在设备串口 CLI 中动态配置，例如：

```
cfg_set_fs_appid <id>
cfg_set_fs_appsecret <secret>
cfg_show_im_cfg
```

### 连接 OpenClaw 抓娃娃机网关

```c
#define OPENCLAW_GATEWAY_HOST   "xxx.xxx.xxx.xxx"
#define OPENCLAW_GATEWAY_PORT   18789
#define OPENCLAW_GATEWAY_TOKEN  "your-token"
#define DUCKYCLAW_DEVICE_ID     "duckyclaw-001"
```

Agent 可通过 `openclaw_ctrl` 工具向网关发送控制指令。

### 修改人格与系统提示

人格由 `config/SOUL.md` 与 `config/USER.md` 驱动，系统提示组装逻辑在：

```
agent/context_builder.c
```

修改 SOUL 文件即可调整语气、角色设定；改 `context_builder.c` 可调整全局规则、工具说明等底层 prompt 结构。

### Linux 音频配置指南

Linux 上语音对话依赖 **ALSA** 采集麦克风、播放 TTS。程序固定使用 **16 kHz、单声道、16-bit PCM**；配置项 `plughw:` 前缀会在运行时自动做格式转换，多数设备无需改源码。

#### 核心原则：改配置，不改代码

| 操作 | 是否需要 |
|------|----------|
| 修改 `tdd_audio_alsa.c` 等 C 源码 | **否** |
| 修改 `app_default.config` 中的 ALSA 设备名 | **通常需要** |
| 修改后重新 `python3 tos.py build` | **是**（配置在编译期生效） |
| 调整系统 ALSA / 虚拟机 / 蓝牙设置 | **视情况** |

设备名在 `app_default.config`（或通过 `config menu` 写入同一文件）中设置：

```ini
CONFIG_ENABLE_AUDIO_ALSA=y
CONFIG_ALSA_DEVICE_CAPTURE="plughw:0,0"
CONFIG_ALSA_DEVICE_PLAYBACK="plughw:0,0"
```

录音与播放可以指向不同设备（例如 USB 麦克风 + 板载喇叭）：

```ini
CONFIG_ALSA_DEVICE_CAPTURE="plughw:1,0"
CONFIG_ALSA_DEVICE_PLAYBACK="plughw:0,0"
```

若不显式配置，程序默认使用 ALSA 的 `default` 设备——仅当系统 `/etc/asound.conf` 与默认路由配置正确时才推荐。

#### 换设备时的标准流程

每次更换耳机、声卡或开发板音频方案，按以下步骤操作：

```bash
# 1. 插入/连接音频设备后，查看声卡编号
arecord -l    # 录音（麦克风）
aplay -l      # 播放（扬声器/耳机）

# 2. 手动验证（对着麦克风说话）
arecord -D plughw:X,0 -f S16_LE -r 16000 -c 1 -d 3 /tmp/test.wav
aplay -D plughw:X,0 /tmp/test.wav

# 3. 将验证通过的设备名写入 app_default.config
#    CONFIG_ALSA_DEVICE_CAPTURE="plughw:X,0"
#    CONFIG_ALSA_DEVICE_PLAYBACK="plughw:X,0"

# 4. 重新编译
cd TuyaOpen && python3 tos.py build
```

`X` 为 `arecord -l` / `aplay -l` 输出中的 **card 编号**（如 `card 1` → `plughw:1,0`）。

#### 常见音频方案对照

| 方案 | Ubuntu 中通常表现 | 推荐配置 | 备注 |
|------|-------------------|----------|------|
| **圆头 3.5mm 耳麦** | 走板载/虚拟机声卡，多为 **card 0** | `plughw:0,0` | VMware 需在宿主机与虚拟机中启用麦克风透传 |
| **USB 声卡 / USB 耳麦** | 出现新声卡 **card 1、2…** | `plughw:1,0` 等 | 在 VMware 中需「可移动设备 → 连接到虚拟机」 |
| **USB 口数字耳机** | 同 USB 声卡 | 按 `arecord -l` 的 card 号 | 与圆头不同，会单独占一张声卡 |
| **蓝牙耳机** | 多经 PipeWire/PulseAudio | 优先试 `default` | 需系统蓝牙配对；不一定支持裸 `hw:`/`plughw:` |
| **RK3576 + ES8388 板载 MIC** | 固定 **card 0**，codec 为 ES8388 | `plughw:0,0` + `TaishanPi_3.config` | 板级代码会自动调增益与声道，见下文 |
| **YD1036 等外接模块** | 取决于模块驱动与 card 号 | 按 `arecord -l` 实测 | 在 `config menu` 中选对应 Audio Codec |

**圆头 vs USB 的区别**：3.5mm 模拟耳麦不会单独多出一张「USB 声卡」，在虚拟机里通常仍显示为 card 0；USB 设备插入后才会新增 card 编号。

#### VMware 虚拟机注意事项

若在 Windows/macOS 宿主机上运行 Ubuntu 虚拟机：

1. **虚拟机设置 → 声卡**：勾选「已连接」「启动时连接」；可尝试 Ensoniq 与 HD Audio 两种类型。
2. **圆头耳麦**：在宿主机「设置 → 声音」中将输入/输出选为对应耳麦，并允许 VMware 使用麦克风。
3. **USB 耳麦**：菜单「虚拟机 → 可移动设备 → [设备名] → 连接到虚拟机」。
4. **不要**在 shell 里执行 `CONFIG_ALSA_DEVICE_CAPTURE=...`——那只是临时环境变量，程序读不到；必须写入 `app_default.config`。
5. 若 `/etc/asound.conf` 仍指向已拔掉的硬件（如 `Y1076`），会导致 `default` 失效；可改为：

```bash
sudo tee /etc/asound.conf << 'EOF'
pcm.!default {
    type plug
    slave.pcm "hw:0,0"
}
ctl.!default {
    type hw
    card 0
}
EOF
```

6. 建议将用户加入 `audio` 组：`sudo usermod -aG audio $USER`，然后注销重新登录。

#### 录音增益与「滋滋声」

若 `arecord` 能录到声音但全是电流噪/滋滋声，常见原因是 **Capture 增益过高导致数字削波**，而非设备未连接：

```bash
# 查看 mixer
amixer -c 0 scontents

# 降低录音增益（数值按实测微调，通常 20%~50%）
amixer -c 0 set Capture 35%
amixer -c 0 set 'Mic Boost (+20dB)' off

# 再次测试
arecord -D plughw:0,0 -f S16_LE -r 16000 -c 1 -d 3 /tmp/test.wav
aplay -D plughw:0,0 /tmp/test.wav
```

同时在 **Windows 宿主机** 将麦克风音量设为 60~75%，关闭「麦克风增强 / Microphone Boost」。

#### RK3576 / TaishanPi_3 + ES8388 板载麦克风

立创等 RK3576 开发板若使用 **ES8388** codec 与板载 MIC，可能出现：

- MIC 标称灵敏度约 **-42 dB**，远场/唤醒偏吃力；
- ES8388 默认 PGA 仅 **+9 dB**；
- 双声道 **反相**，应用混成 mono 时 L+R **相互抵消**，人声几乎听不见。

这与 VMware 虚拟声卡的「滋滋声/削波」是 **不同问题**。症状通常是：录音极轻、近乎静音，而非满幅爆音。

**软件修复**（手动或程序启动时自动执行）：

```bash
amixer -c 0 cset numid=52 8   # 左采集 +24 dB
amixer -c 0 cset numid=53 8   # 右采集 +24 dB
amixer -c 0 cset numid=61 1   # ADC 选 Left-Left，避免反相下混抵消
```

使用 `config/TaishanPi_3.config` 编译时，`TuyaOpen/boards/LINUX/TaishanPi_3/board_com_api.c` 会在启动时自动执行上述 ES8388 调参；日志中应出现：

```
ES8388 codec tuned: mic gain +24 dB, ADC Left-Left
```

板载 MIC 验证（双声道原始录音）：

```bash
arecord -D hw:0,0 -f cd -r 44100 -c 2 -V stereo board_stereo.wav
aplay -D plughw:0,0 board_stereo.wav
```

若产品需稳定语音识别/唤醒，硬件上可考虑更高灵敏度 MIC 或前置放大模块。

#### 少改配置的用法：固定 `default`

若希望换耳机时只改系统默认设备、不改 `app_default.config`：

1. 确保 `/etc/asound.conf` 与桌面「默认输入/输出」配置正确；
2. 删除或注释 `app_default.config` 中的 `CONFIG_ALSA_DEVICE_CAPTURE` / `CONFIG_ALSA_DEVICE_PLAYBACK`，让程序回退到 `default`。

此方式在 **单一声卡、无错误 asound.conf** 的环境较省事；多声卡或虚拟机环境更推荐显式写 `plughw:X,0`。

#### 故障排查速查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `Cannot get card index for Y1076` | `/etc/asound.conf` 指向不存在的声卡 | 修改 asound.conf 或改用 `plughw:0,0` |
| `Cannot get card index for 1` | 配置了 card 1 但未插入 USB 设备 | 改回 `plughw:0,0` 或插入 USB 声卡 |
| `Input/output error`（采集中断） | VM 未透传麦克风 / 权限 / 设备被占用 | 检查 VMware 声卡与用户 `audio` 组 |
| 有声音但是滋滋声 | Capture 增益过高削波 | `amixer set Capture 30~50%`，降低 Windows 麦克风电平 |
| 人声极轻或几乎无声（实体板） | ES8388 反相 + mono 下混抵消 | 执行 numid=52/53/61 或使用 TaishanPi_3 配置 |
| 蓝牙无声音 | 未走 PipeWire/Pulse 默认路由 | 系统配对后试 `default`，或改用 USB 有线耳麦调试 |

#### 何时才需要改代码

| 场景 | 说明 |
|------|------|
| 更换 USB/圆头/蓝牙等 Linux ALSA 设备 | **不需要**，改 `app_default.config` 即可 |
| 更换开发板类型 | 换 `config/*.config`（如 `Ubuntu.config` → `TaishanPi_3.config`），非改业务代码 |
| 新 codec（如 ESP32 I2S ES8388） | 使用对应板级 `board_com_api.c`，与 Ubuntu ALSA 路径无关 |
| 启动时自动探测声卡 | 当前版本 **未实现**；需自行扩展或每次按上文流程配置 |

---

## 效果展示

> 以下为效果占位，欢迎补充截图 / 录屏后替换链接。

### 渲染效果

<!-- ![VRM 渲染](docs/images/vrm-render.png) -->

3D 模型加载、VRMA 动作、场景切换、表情与弹簧骨实时驱动。

### 语音对话

<!-- ![语音对话](docs/images/voice-chat.png) -->

唤醒 → 语音识别 → Agent 推理 → TTS 播报，数字人同步口型与情绪。

### IM 机器人对话

<!-- ![飞书对话](docs/images/im-chat.png) -->

同一 Agent 循环处理来自飞书 / QQ / Telegram 等通道的消息，支持多轮工具调用。

### Skill 演示

<!-- ![技能演示](docs/images/skill-demo.png) -->

对话中触发天气、定时简报等技能，或现场创建新技能并立即使用。

---

## 架构一瞥

```
IM 通道 (飞书/QQ/TG/Discord/CLI)
        ↓
   Message Bus  →  Agent Loop (≤10 轮工具迭代)
        ↑              ↓
   出站分发      Context Builder + 云端 AI
                       ↓
              MCP 工具 + VRM 渲染 + 音频
```

- **Agent 循环**：`agent/agent_loop.c` — 外层等待消息，内层 LLM ↔ 工具迭代
- **3D 渲染**：`src/vrm/` — 模型加载、OpenGL 渲染、情绪与动作
- **技能**：`skills/skill_loader.c` — 扫描 `/skills/*.md` 注入系统提示
- **记忆**：`memory/memory_manager.c` — MEMORY / 日记 / SOUL / USER 持久化

更多开发细节见 [AGENTS.md](AGENTS.md) 与 [CLAUDE.md](CLAUDE.md)。

---

## 许可证

Copyright © Tuya Inc. 详见各子模块许可证声明。
