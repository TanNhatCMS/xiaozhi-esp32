# Project Overview

This project is an ESP32-based AI chatbot that serves as a voice interaction entry point. It leverages AI capabilities from large models like Qwen/DeepSeek and implements multi-device control via the MCP (Multi-Control Protocol) protocol.

**Key Features:**
*   Supports Wi-Fi / ML307 Cat.1 4G connectivity.
*   Offline voice wake-up using ESP-SR.
*   Supports Websocket or MQTT+UDP communication protocols.
*   Utilizes OPUS audio codec.
*   Voice interaction based on streaming ASR + LLM + TTS architecture.
*   Speaker verification using 3D Speaker.
*   OLED / LCD display with emoji support.
*   Battery level display and power management.
*   Multi-language support (Chinese, English, Japanese).
*   Supports ESP32-C3, ESP32-S3, ESP32-P4 chip platforms.
*   Device control via on-device MCP (volume, lighting, motors, GPIO, etc.).
*   Cloud-based MCP for extending large model capabilities (smart home control, PC desktop operations, knowledge search, email).
*   Customizable wake words, fonts, emojis, and chat backgrounds with an online asset generator.

# Building and Running

This project uses the ESP-IDF (Espressif IoT Development Framework).

**Prerequisites:**
*   ESP-IDF plugin for Cursor or VSCode.
*   ESP-IDF SDK version 5.4 or above.

**Standard ESP-IDF Workflow:**

1.  **Configure the project:**
    ```bash
    idf.py menuconfig
    ```
2.  **Build the project:**
    ```bash
    idf.py build
    ```
3.  **Flash the firmware to the ESP32 device:**
    ```bash
    idf.py -p <PORT> flash
    ```
    (Replace `<PORT>` with your ESP32's serial port, e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux)
4.  **Monitor serial output:**
    ```bash
    idf.py -p <PORT> monitor
    ```

# Development Conventions

*   **Code Style:** Google C++ code style is used throughout the project.
*   **IDE:** Cursor or VSCode with the ESP-IDF plugin is recommended for development.
*   **Operating System:** Linux is preferred over Windows for faster compilation and fewer driver issues.
