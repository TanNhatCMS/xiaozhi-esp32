# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Common commands

- Initialize ESP-IDF target and configure board
  ```bash path=null start=null
  idf.py set-target esp32s3   # or esp32c3 / esp32 / esp32c6 / esp32p4 per board
  idf.py menuconfig           # Board Type, Flash Assets, Default Language, etc.
  ```

- Build, flash, and monitor
  ```bash path=null start=null
  idf.py build
  idf.py -p <PORT> flash
  idf.py -p <PORT> monitor    # Ctrl-] to quit
  ```

- Build a specific board variant (CI-quality, reproducible)
  ```bash path=null start=null
  # List all board variants discovered from main/boards/**/config.json
  python scripts/release.py --list-boards --json

  # Build and package one variant (adds sdkconfig appends, sets target, defines BOARD_NAME/BOARD_TYPE)
  python scripts/release.py <board_dir_name> --name <variant_name>
  # Output: build/merged-binary.bin and releases/v<project_ver>_<variant_name>.zip
  ```

- Package the current local build (no board arg)
  ```bash path=null start=null
  # Merges current build to build/merged-binary.bin and zips to releases/v<ver>_<BOARD_TYPE>.zip
  python scripts/release.py
  ```

- Useful IDF helpers
  ```bash path=null start=null
  idf.py merge-bin   # generate build/merged-binary.bin
  idf.py clean && idf.py build
  ```

- Assets flashing (optional)
  ```bash path=null start=null
  # In menuconfig:
  #   Xiaozhi Assistant > Flash Assets: (Default | Custom | None)
  #   If Custom: set Xiaozhi Assistant > Custom Assets File (local path or https URL)
  # Assets are flashed into the `assets` partition by the CMake rules in main/CMakeLists.txt
  ```

Notes
- ESP-IDF 5.4+ is expected (see README). CI builds use the espressif/idf:release-v5.5 container.
- No project linter/test suite is configured in-repo. Follow Google C++ style (per README) if you format locally.

## High-level architecture

- Build system and targets
  - Pure ESP-IDF CMake project (CMakeLists.txt, main/CMakeLists.txt). Project version via set(PROJECT_VER ...).
  - Kconfig in main/Kconfig.projbuild defines Board Type, language, and assets flashing.
  - main/CMakeLists.txt selects sources by BOARD_TYPE, language assets, and sets compile defs: BOARD_TYPE, BOARD_NAME.
  - Custom CMake rules build and optionally flash default or custom assets.bin to the assets partition.

- Application orchestration (main/application.{h,cc}, main/main.cc)
  - app_main initializes NVS and starts Application.
  - Application owns the lifecycle: FreeRTOS event groups/timers, main event loop, device state machine, scheduling via Application::Schedule.
  - Initializes Board, AudioService, selects Protocol (MQTT or WebSocket) based on OTA config, and handles OTA/asset updates.
  - Processes incoming JSON types: tts/stt/llm/mcp/system/alert; updates UI/audio and state accordingly.

- Board abstraction (main/boards/**)
  - Base classes: Board, WifiBoard, Ml307Board provide AudioCodec/Display/Network/Backlight/LED/Camera/Music accessors and power-save hooks.
  - Per-board implementations live under main/boards/<board>/ with config.h and initialization code; board selection via Kconfig BOARD_TYPE.
  - Per-board config.json files drive reproducible builds via scripts/release.py (target chip, sdkconfig appends, variant names).

- Audio pipeline (main/audio/**)
  - AudioService manages two flows: MIC -> processors -> Opus encoder -> send queue; and incoming -> Opus decoder -> playback queue.
  - Optional AFE processing and wake-word (ESP-SR on S3/P4; ESP wake-word on other chips). Resamplers handle rate mismatches.
  - Queues/tasks: input/output/codec tasks with timing and power gating; callbacks feed protocol send and UI VAD updates.

- Protocol layer (main/protocols/**)
  - Protocol interface with MQTT and WebSocket implementations. Binary framing for audio plus JSON side-channel.
  - Handles audio channel open/close, server sample-rate negotiation, session and error handling.

- Display and UI (main/display/**)
  - Abstract Display and LvglDisplay variants (HAVE_LVGL). Shows status, chat messages, emotions, notifications, battery/network.
  - Assets/locales provide strings and OGG audio prompts per language, with en-US fallback logic.

- Assets and OTA (main/assets.{h,cc}, main/ota.{h,cc})
  - Assets manager mmaps the assets partition, validates checksum, downloads and applies updates when configured.
  - OTA component checks server, handles activation flow, firmware upgrades, and decides protocol config.

- MCP tools (main/mcp_server.{h,cc})
  - Registers built-in tools for device status, audio volume, screen brightness/theme, camera snapshot/explain (if available), music control, reboot and firmware upgrade.
  - Boards can add custom tools in their initialization.

Key doc references in repo
- README.md / README_en.md for overview, environment expectations, and links to device/protocol docs.
- docs/*.md for custom board guide, MCP usage/protocol, MQTT+UDP and WebSocket specs, and assets tooling.
