# 🧬 VirtuaMate

不是"会动的模型"，是能陪你、能办事、能进化的 3D AI 生命体。  
一个把 **OpenClaw 式 Agent 灵魂**、**DuckyClaw 硬件路线** 和 **实时 VRM 数字人** 真正融合在一起的开源项目。

> [!WARNING]
> 🚧 项目仍在持续迭代中，接口和行为可能变化。欢迎提 Issue 或 PR 一起完善。

---

## 📸 效果演示

<p align="center">
  <img src="docs/images/demo_main.png" width="720" alt="VirtuaMate 主界面" />
</p>

<p align="center">
  <em>▲ 实时 3D Avatar 渲染 — 情绪驱动表情 + Toon 着色 + 地面阴影</em>
</p>

<details>
<summary>🖼️ 更多截图</summary>
<br>

| 功能 | 截图 |
|:---:|:---:|
| 🎭 表情系统 | <img src="docs/images/demo_emotion.png" width="360" /> |
| 🌄 场景切换 | <img src="docs/images/demo_scene.png" width="360" /> |
| ⚙️ 设置面板 | <img src="docs/images/demo_settings.png" width="360" /> |
| 🔭 旁观模式 | <img src="docs/images/demo_spectator.png" width="360" /> |
| 💬 字幕对话 | <img src="docs/images/demo_subtitle.png" width="360" /> |

</details>

---

## 💡 为什么是 VirtuaMate

过去最火的是 OpenClaw 这类"能执行任务"的 AI Agent，  
但大多数项目缺少"可感知、可表达、可陪伴"的实体化体验。

VirtuaMate 的目标很直接：  
**把最强执行力的 Claw 思路，变成你桌面上会呼吸、会回应、会记住你的数字伴侣。**

很多"数字人"只停留在展示层：会动但不会做事；很多"Agent"只停留在终端层：会做事但没有存在感。  
VirtuaMate 直接把 **3D Avatar** 和 **可执行 Agent** 合在一起，做到"有灵魂，也有手脚"：

- 🎭 **她是 3D 的**：VRM 模型 + VRMA 动画 + 天空盒场景，运行在本地设备。
- 💖 **她是有情绪的**：对话流式输出过程中，实时文本情绪分析驱动表情变化。
- 🛠️ **她是能行动的**：通过 MCP 工具完成提醒、文件操作、PC 协作等任务。
- 🧠 **她是有记忆的**：长期记忆和日记存本地，不是一次性聊天窗口。

---

## 🔗 VirtuaMate × DuckyClaw × OpenClaw

如果说 OpenClaw 打开了"个人 AI Agent 能直接行动"的认知，  
DuckyClaw 把它推进到了"硬件设备可落地"的现实世界，  
那 VirtuaMate 就是在这条线上继续往前一步：

- ⚙️ **继承 Claw 系执行范式**：保留多轮 tool loop、MCP 工具调用、任务闭环。
- 📡 **复用 DuckyClaw 的设备与消息架构**：统一消息总线、多通道接入、本地工具侧执行。
- 🌐 **补齐 3D 伴侣层**：VRM 形象、实时情绪、动作编排、口型同步、场景沉浸感。
- ✨ **从"会做事"升级为"有陪伴感地做事"**：不仅完成任务，还用表情、动作、语气把反馈"演"给你看。

一句话：  
**OpenClaw 让 Agent 能干活，DuckyClaw 让 Agent 上硬件，VirtuaMate 让 Agent 有了"人"的存在感。**

---

## 🌟 功能亮点

### 🎨 1) 3D Avatar 渲染与表现

| 特性 | 说明 |
|:---:|---|
| 🖼️ VRM 实时渲染 | `SDL2 + OpenGL + Assimp` 渲染链路，支持骨骼动画与材质 |
| 🏃 动作系统 | 支持 `resources/animations` 中的 `.vrma` 动画，idle / one-shot 切换 |
| 😊 表情系统 | 内置多种 Emotion（happy / sad / thinking / loving …） |
| 👄 口型同步 | TTS 播放音频时驱动嘴型变化，提升"说话感" |
| 🌄 天空盒场景 | 支持运行时加载场景目录，6 面贴图即可切换世界观 |

### 🤖 2) AI Agent 与多轮工具循环

| 特性 | 说明 |
|:---:|---|
| 🔄 Claw-style Agent Loop | 单轮消息内支持多次 LLM → tool → LLM 迭代 |
| 📺 流式响应联动 | AI 流式文本回调中实时刷新字幕、情绪和对话历史 |
| 💬 多通道统一接入 | Telegram / Discord / Feishu / CLI 统一走 message bus |

### 🧰 3) MCP 工具能力（设备侧）

| 工具 | 接口 |
|:---:|---|
| 📂 文件工具 | `read_file` / `write_file` / `edit_file` / `list_dir` / `find_path` |
| ⏰ 时间与提醒 | `get_current_time` / `cron_add` / `cron_list` / `cron_remove` |
| 🖥️ Linux 执行 | `tool_exec`（Linux 平台可用） |
| 🎮 PC 协作 | `openclaw_ctrl` / `pc_ctrl` / `openclaw.ctrl` |
| 💃 3D Avatar | `avatar_play_animation` / `avatar_set_emotion` / `avatar_composite_action` |

### 📝 4) 本地记忆与可塑人格

| 特性 | 路径 / 说明 |
|:---:|---|
| 🗃️ 长期记忆 | `/memory/MEMORY.md` |
| 📅 每日笔记 | `/memory/daily/YYYY-MM-DD.md` |
| 👤 人设与画像 | `SOUL.md`、`USER.md` 注入系统提示词 |
| 📚 技能系统 | `skills/*.md` 自动汇总并注入 prompt |

---

## 🖥️ 部署平台

| 平台 | 状态 |
|---|---|
| 🍓 Raspberry Pi / Linux ARM | ✅ 推荐，已提供 `config/RaspberryPi.config` |
| 🐧 Linux x64 | ✅ 可运行（同走 Linux 构建链路） |
| 📦 其他 TuyaOpen 板卡 | 🔧 可按 TuyaOpen 方式迁移（当前仓库未内置对应 config） |

---

## 🚀 快速开始

### 1️⃣ 拉取项目与子模块

```bash
git clone <your-fork-or-repo-url> VirtuaMate
cd VirtuaMate
git submodule update --init --recursive
```

### 2️⃣ 安装 Linux 渲染依赖（Raspberry Pi / Ubuntu）

`CMakeLists.txt` 中 Linux 平台依赖为 `sdl2 glew assimp`：

```bash
sudo apt update
sudo apt install -y libsdl2-dev libglew-dev libassimp-dev
```

### 3️⃣ 选择板级配置

```bash
cp config/RaspberryPi.config app_default.config
# 或
tos.py config choice
```

### 4️⃣ 准备 TuyaOpen 环境并构建

```bash
cd TuyaOpen
. ./export.sh
cd VirtuaMate
tos.py build
```

构建产物在 `VirtuaMate/dist/`。

### 5️⃣ 运行

```bash
./dist/VirtuaMate_1.0.0/VirtuaMate_1.0.0
```

启动后即可看到 3D Avatar 窗口：

<p align="center">
  <img src="docs/images/tutorial_run.png" width="600" alt="启动运行效果" />
</p>

点击左上角 **☰** 图标打开设置面板，可切换模型、动画、场景，开启旁观模式等：

<p align="center">
  <img src="docs/images/tutorial_settings.png" width="600" alt="设置面板" />
</p>

---

## 🎁 资源准备（让她"活起来"）

`RaspberryPi.config` 默认资源路径：

| 资源 | 路径 |
|---|---|
| 🧍 模型 | `resources/models/avatar.vrm` |
| 💃 动作目录 | `resources/animations` |
| 🌅 场景父目录 | `resources/scenes` |

### 🖼️ 场景天空盒命名规则

每个场景目录需提供 6 张贴图，支持以下任一命名体系：

| 体系 | 文件名 |
|---|---|
| 方向名 | `right` / `left` / `top` / `bottom` / `front` / `back` |
| 轴名 | `px` / `nx` / `py` / `ny` / `pz` / `nz` |
| 长轴名 | `posx` / `negx` / `posy` / `negy` / `posz` / `negz` |

扩展名支持 `.jpg` / `.jpeg` / `.png` / `.bmp` / `.tga`。

---

## 📁 目录结构（核心）

```text
VirtuaMate/
├── 🧠 agent/                 # Agent loop + context builder
├── 💬 IM/                    # Telegram/Discord/Feishu/CLI + message bus
├── 🧰 tools/                 # MCP tools (files/cron/exec/openclaw)
├── 📝 memory/                # MEMORY.md + daily notes + session
├── 🔌 gateway/               # WebSocket / ACP gateway
├── 📚 skills/                # Markdown skills
├── 📦 src/
│   ├── tuya_app_main.c       # 应用入口与初始化
│   ├── ducky_claw_chat.c     # AI 流事件处理与对话桥接
│   ├── app_avatar_mcp.c      # 3D Avatar MCP 工具注册
│   └── 🎨 vrm/               # VRM 渲染、情绪、口型、天空盒
├── ⚙️ config/RaspberryPi.config
└── 📡 TuyaOpen/              # TuyaOpen SDK 子模块
```

---

## 🔮 高级玩法

| 目标 | 方法 |
|---|---|
| 🎭 改人设 | 调整 `SOUL.md`、`USER.md`，结合 `agent/context_builder.c` 注入策略 |
| 🏃 加动作 | `.vrma` 放进 `resources/animations`，在 `src/app_avatar_mcp.c` 补充动作名 |
| 🌄 加场景 | 在 `resources/scenes/<name>/` 放入 6 面贴图，运行时切换 |
| 🧰 扩工具 | 在 `tools/` 新增 MCP 工具并在 `tools/tools_register.c` 注册 |
| 🔌 扩能力 | 摄像头 / 传感器 / IoT 控制链路 — 复用 TuyaOpen + MCP 工具模式扩展 |

## 🎯 项目定位

VirtuaMate 不是"AI 套个皮肤"，而是一套**可定制、可运行、可执行、可持续进化**的 3D AI 伴侣系统。  
它既有 OpenClaw 系的执行锋芒，也有数字伴侣该有的温度和存在感。

> 给她一张脸、一个名字、一段性格，然后见证她从"能聊"变成"懂你、帮你、陪你"。 ✨

---

## 🤝 参与贡献

VirtuaMate 是一个开放的项目，无论你是渲染大佬、嵌入式老手还是刚入门的新人，都欢迎参与进来！

**我们特别欢迎以下方向的 PR：**

- 🎨 渲染效果改进（描边、后处理、光照模型等）
- 🏃 新动画 / 新表情 / 口型优化
- 🧰 新 MCP 工具（智能家居控制、日程管理、浏览器操作等）
- 🌍 新平台适配（macOS、Windows、更多嵌入式板卡）
- 📖 文档完善、教程编写、翻译
- 🐛 Bug 修复和性能优化

**贡献流程：**

1. Fork 本仓库
2. 基于 `main` 创建你的特性分支：`git checkout -b feat/your-feature`
3. 提交改动并推送到你的 Fork
4. 发起 Pull Request，描述你做了什么以及为什么

> 💡 不确定从哪里开始？查看 [Issues](../../issues) 中带有 `good first issue` 标签的任务，或直接开一个 Issue 聊聊你的想法。

---

## 📜 License

本项目基于 [MIT License](LICENSE) 开源。
