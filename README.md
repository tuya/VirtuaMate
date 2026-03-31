# VirtuaMate

VirtuaMate (DuckyClaw) is a hardware-oriented AI agent built on the [TuyaOpen](https://github.com/tuya/TuyaOpen) C SDK. It runs on edge devices — Raspberry Pi, Tuya T5AI, ESP32-S3, or any Linux desktop — and lets users chat with the device through IM channels (Telegram, Discord, Feishu). The on-device agent loop receives natural language instructions, invokes MCP-style tools to perform real actions, and replies through the same channel.

## Features

- **Multi-channel IM** — Telegram, Discord, Feishu, WebSocket, and serial CLI all feed into a single agent loop.
- **Tool loop** — Each user message can trigger up to 10 LLM ↔ tool iterations before a final reply is produced.
- **MCP Tools** — File operations, cron scheduling, remote command execution (Linux), and OpenClaw gateway control.
- **Persistent memory** — Long-term memory (`MEMORY.md`), daily notes, personality (`SOUL.md`), and user profile (`USER.md`) stored on flash/SD.
- **3D avatar** — Real-time VRM/PMX/GLB model rendering with emotion-driven facial expressions, spring-bone physics, lip sync, and skybox scenes (Raspberry Pi / Linux with OpenGL).
- **Cross-platform** — One codebase compiles for Raspberry Pi, Tuya T5AI, ESP32-S3, and Linux via Kconfig conditional compilation.

## Architecture

```
IM Channels (Telegram / Discord / Feishu / WS / CLI)
                    │
              Message Bus
                    │
               Agent Loop  ←→  Cloud AI (streaming)
                    │
              MCP Tools Layer
         ┌────┬────┬─────┬──────────┐
       files  cron  exec  openclaw  avatar
         │                            │
    Memory / Session            VRM 3D Renderer
```

## Prerequisites

| Item | Notes |
|------|-------|
| **TuyaOpen SDK** | Cloned as a git submodule under `TuyaOpen/` |
| **Python 3** | Required by `tos.py` build system |
| **CMake ≥ 3.16** | Build generator |
| **Tuya cloud credentials** | Product ID, UUID, AuthKey from [Tuya IoT Platform](https://platform.tuya.com) |
| **IM bot token** | At least one of: Telegram bot token, Discord bot token, or Feishu app credentials |
| **(Linux/RPi only)** SDL2, GLEW, Assimp | For the VRM 3D avatar renderer: `sudo apt install libsdl2-dev libglew-dev libassimp-dev` |

## Getting Started

### 1. Clone the repository

```bash
git clone --recurse-submodules https://github.com/<your-org>/VirtuaMate.git
cd VirtuaMate
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### 2. Initialize the TuyaOpen environment

```bash
cd TuyaOpen
source ./export.sh
cd ..
```

This creates a Python virtual environment and exports `OPEN_SDK_ROOT`.

### 3. Configure secrets

```bash
cp include/tuya_app_config_secrets.h.example include/tuya_app_config_secrets.h
```

Edit `include/tuya_app_config_secrets.h` and fill in your credentials:

| Define | Description |
|--------|-------------|
| `TUYA_PRODUCT_ID` | Tuya product ID |
| `TUYA_OPENSDK_UUID` | Tuya OpenSDK UUID |
| `TUYA_OPENSDK_AUTHKEY` | Tuya OpenSDK AuthKey |
| `IM_SECRET_CHANNEL_MODE` | `"feishu"`, `"telegram"`, or `"discord"` |
| `IM_SECRET_TG_TOKEN` | Telegram bot token (if using Telegram) |
| `IM_SECRET_DC_TOKEN` / `IM_SECRET_DC_CHANNEL_ID` | Discord bot token and channel ID (if using Discord) |
| `IM_SECRET_FS_APP_ID` / `IM_SECRET_FS_APP_SECRET` | Feishu app credentials (if using Feishu) |
| `CLAW_WS_AUTH_TOKEN` | WebSocket auth token (optional) |
| `OPENCLAW_GATEWAY_*` | OpenClaw gateway host, port, token (optional) |

> **Note:** `tuya_app_config_secrets.h` is gitignored — never commit real credentials.

### 4. Select a board configuration

Copy one of the pre-built configs from `config/` to the project root:

```bash
# Raspberry Pi
cp config/RaspberryPi.config app_default.config
```

Or create your own via `menuconfig`:

```bash
cd TuyaOpen
python3 tos.py menuconfig
cd ..
```

### 5. Build

```bash
# Skip interactive platform prompts (optional, one-time)
mkdir -p .cache && touch .cache/.dont_prompt_update_platform

cd TuyaOpen
python3 tos.py build
```

Build output goes to the `dist/` directory. On Linux targets this produces a native ELF binary.

### 6. Install VRM dependencies (Linux / Raspberry Pi only)

If you are building on Linux or Raspberry Pi and want the 3D avatar renderer:

```bash
sudo apt install libsdl2-dev libglew-dev libassimp-dev
```

Configure model paths in `menuconfig` or directly in `app_default.config`:

```
CONFIG_VRM_MODEL_PATH="/path/to/your/avatar.vrm"
CONFIG_VRM_ANIM_DIR="/path/to/vrma/animations"
CONFIG_VRM_WINDOW_WIDTH=1024
CONFIG_VRM_WINDOW_HEIGHT=768
```

### 7. Run

```bash
# Linux
./dist/VirtuaMate
```

On Raspberry Pi, flash or copy the binary and run it. The agent will connect to Tuya cloud, initialize the configured IM channel, and start listening for messages.

## Project Structure

```
VirtuaMate/
├── agent/          # Core agent loop and context builder
├── IM/             # IM abstraction: message bus, channel bots, proxy, CLI
├── tools/          # MCP tool implementations (files, cron, exec, openclaw)
├── memory/         # Persistent memory and session management
├── gateway/        # WebSocket server and ACP client
├── cron_service/   # Background scheduled-task service
├── heartbeat/      # Heartbeat service
├── skills/         # Skill loader for .md skill files
├── src/            # App glue: main entry, AI chat handler, IM dispatch
│   └── vrm/        # VRM 3D avatar renderer (OpenGL)
├── include/        # Global headers and config
├── ai_components/  # TuyaOpen AI component adapters
├── config/         # Board-level Kconfig presets
├── TuyaOpen/       # TuyaOpen SDK (git submodule)
├── CMakeLists.txt  # Application-level build script
└── Kconfig         # Top-level Kconfig menu
```

## License

Copyright (c) Tuya Inc. All Rights Reserved.
