# Chatbot dựa trên MCP

([中文](README.md) | [English](README_en.md) | [日本語](README_ja.md))

## Giới thiệu

👉 [Con người: lắp camera cho AI vs AI: phát hiện chủ nhân ba ngày chưa gội đầu【bilibili】](https://www.bilibili.com/video/BV1bpjgzKEhd/)

👉 [Tự tay chế tạo người bạn gái AI của bạn, hướng dẫn nhập môn cho người mới【bilibili】](https://www.bilibili.com/video/BV1XnmFYLEJN/)

Xiaozhi AI là một chatbot điều khiển bằng giọng nói, khai thác khả năng của các mô hình lớn như Qwen / DeepSeek và giao tiếp qua giao thức MCP để điều khiển nhiều thiết bị.

<img src="docs/mcp-based-graph.jpg" alt="Điều khiển vạn vật qua MCP" width="320">

### Ghi chú phiên bản

Phiên bản v2 hiện tại không tương thích bảng phân vùng với v1, vì vậy không thể nâng cấp OTA trực tiếp từ v1 lên v2. Tham khảo [partitions/v2/README.md](partitions/v2/README.md) để biết chi tiết.

Tất cả phần cứng đang dùng v1 đều có thể nâng cấp lên v2 bằng cách nạp thủ công firmware.

Bản ổn định cuối cùng của v1 là 1.9.2, bạn có thể chuyển sang nhánh v1 bằng `git checkout v1`. Nhánh này sẽ được bảo trì đến tháng 2 năm 2026.

### Các tính năng đã hoàn thiện

- Wi-Fi / ML307 Cat.1 4G
- Đánh thức bằng giọng nói ngoại tuyến [ESP-SR](https://github.com/espressif/esp-sr)
- Hỗ trợ hai giao thức truyền thông ([Websocket](docs/vi/websocket.md) hoặc MQTT+UDP)
- Mã hoá/giải mã âm thanh OPUS
- Tương tác giọng nói dựa trên kiến trúc streaming ASR + LLM + TTS
- Nhận diện người nói, xác định danh tính người đang nói [3D Speaker](https://github.com/modelscope/3D-Speaker)
- Màn hình OLED / LCD hiển thị biểu cảm
- Hiển thị dung lượng pin và quản lý nguồn
- Hỗ trợ đa ngôn ngữ (tiếng Trung, tiếng Anh, tiếng Nhật)
- Hỗ trợ các dòng chip ESP32-C3, ESP32-S3, ESP32-P4
- Điều khiển thiết bị qua MCP trên thiết bị (âm lượng, ánh sáng, động cơ, GPIO...)
- Mở rộng năng lực mô hình lớn qua MCP trên đám mây (điều khiển nhà thông minh, thao tác máy tính, tìm kiếm tri thức, gửi/nhận email...)
- Tuỳ biến từ khoá đánh thức, phông chữ, biểu cảm và hình nền trò chuyện ngay trên web ([Trình tạo tài sản tuỳ biến](https://github.com/78/xiaozhi-assets-generator))

## Phần cứng

### Thực hành chế tạo trên breadboard

Xem hướng dẫn chi tiết trong tài liệu Feishu:

👉 ["Bách khoa toàn thư về chatbot Xiaozhi AI"](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb?from=from_copylink)

Hình ảnh breadboard mẫu:

![Ảnh breadboard](docs/v1/wiring2.jpg)

### Hỗ trợ hơn 70 thiết bị phần cứng nguồn mở (danh sách một phần)

- <a href="https://oshwhub.com/li-chuang-kai-fa-ban/li-chuang-shi-zhan-pai-esp32-s3-kai-fa-ban" target="_blank" title="Lichuang·Shizhanpai ESP32-S3 Development Board">Bo mạch phát triển Lichuang·Shizhanpai ESP32-S3</a>
- <a href="https://github.com/espressif/esp-box" target="_blank" title="Espressif ESP32-S3-BOX3">Espressif ESP32-S3-BOX3</a>
- <a href="https://docs.m5stack.com/zh_CN/core/CoreS3" target="_blank" title="M5Stack CoreS3">M5Stack CoreS3</a>
- <a href="https://docs.m5stack.com/en/atom/Atomic%20Echo%20Base" target="_blank" title="AtomS3R + Echo Base">M5Stack AtomS3R + Echo Base</a>
- <a href="https://gf.bilibili.com/item/detail/1108782064" target="_blank" title="Magic Button 2.4">Magic Button 2.4</a>
- <a href="https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.8.htm" target="_blank" title="Waveshare ESP32-S3-Touch-AMOLED-1.8">Waveshare ESP32-S3-Touch-AMOLED-1.8</a>
- <a href="https://github.com/Xinyuan-LilyGO/T-Circle-S3" target="_blank" title="LILYGO T-Circle-S3">LILYGO T-Circle-S3</a>
- <a href="https://oshwhub.com/tenclass01/xmini_c3" target="_blank" title="Xiage Mini C3">Xiage Mini C3</a>
- <a href="https://oshwhub.com/movecall/cuican-ai-pendant-lights-up-y" target="_blank" title="Movecall CuiCan ESP32S3">Movecall CuiCan ESP32S3</a>
- <a href="https://github.com/WMnologo/xingzhi-ai" target="_blank" title="Nologo Xingzhi 1.54">Nologo Xingzhi 1.54</a>
- <a href="https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html" target="_blank" title="SenseCAP Watcher">SenseCAP Watcher</a>
- <a href="https://www.bilibili.com/video/BV1BHJtz6E2S/" target="_blank" title="ESP-HI Low-cost Robot Dog">Chó robot ESP-HI siêu tiết kiệm chi phí</a>

<div style="display: flex; justify-content: space-between;">
  <a href="docs/v1/lichuang-s3.jpg" target="_blank" title="Bo mạch Lichuang·Shizhanpai ESP32-S3">
    <img src="docs/v1/lichuang-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/espbox3.jpg" target="_blank" title="Espressif ESP32-S3-BOX3">
    <img src="docs/v1/espbox3.jpg" width="240" />
  </a>
  <a href="docs/v1/m5cores3.jpg" target="_blank" title="M5Stack CoreS3">
    <img src="docs/v1/m5cores3.jpg" width="240" />
  </a>
  <a href="docs/v1/atoms3r.jpg" target="_blank" title="AtomS3R + Echo Base">
    <img src="docs/v1/atoms3r.jpg" width="240" />
  </a>
  <a href="docs/v1/magiclick.jpg" target="_blank" title="Magic Button 2.4">
    <img src="docs/v1/magiclick.jpg" width="240" />
  </a>
  <a href="docs/v1/waveshare.jpg" target="_blank" title="Waveshare ESP32-S3-Touch-AMOLED-1.8">
    <img src="docs/v1/waveshare.jpg" width="240" />
  </a>
  <a href="docs/v1/lilygo-t-circle-s3.jpg" target="_blank" title="LILYGO T-Circle-S3">
    <img src="docs/v1/lilygo-t-circle-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/xmini-c3.jpg" target="_blank" title="Xiage Mini C3">
    <img src="docs/v1/xmini-c3.jpg" width="240" />
  </a>
  <a href="docs/v1/movecall-cuican-esp32s3.jpg" target="_blank" title="CuiCan">
    <img src="docs/v1/movecall-cuican-esp32s3.jpg" width="240" />
  </a>
  <a href="docs/v1/wmnologo_xingzhi_1.54.jpg" target="_blank" title="Nologo Xingzhi 1.54">
    <img src="docs/v1/wmnologo_xingzhi_1.54.jpg" width="240" />
  </a>
  <a href="docs/v1/sensecap_watcher.jpg" target="_blank" title="SenseCAP Watcher">
    <img src="docs/v1/sensecap_watcher.jpg" width="240" />
  </a>
  <a href="docs/v1/esp-hi.jpg" target="_blank" title="Chó robot ESP-HI">
    <img src="docs/v1/esp-hi.jpg" width="240" />
  </a>
</div>

## Phần mềm

### Nạp firmware

Người mới nên bắt đầu bằng cách nạp firmware có sẵn mà không cần thiết lập môi trường phát triển.

Firmware mặc định kết nối tới máy chủ chính thức [xiaozhi.me](https://xiaozhi.me). Người dùng cá nhân đăng ký tài khoản có thể sử dụng miễn phí mô hình thời gian thực Qwen.

👉 [Hướng dẫn nạp firmware cho người mới](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)

### Môi trường phát triển

- Cursor hoặc VSCode
- Cài đặt plugin ESP-IDF, chọn phiên bản SDK 5.4 trở lên
- Linux tốt hơn Windows: tốc độ biên dịch nhanh và không cần xử lý driver
- Dự án áp dụng phong cách mã hoá C++ của Google, hãy đảm bảo mã của bạn tuân thủ trước khi gửi PR

### Tài liệu cho nhà phát triển

- [Hướng dẫn tạo bo mạch tuỳ chỉnh](docs/vi/custom-board.md) - Học cách tạo bo mạch tuỳ chỉnh cho Xiaozhi AI
- [Hướng dẫn sử dụng MCP để điều khiển IoT](docs/vi/mcp-usage.md) - Tìm hiểu cách dùng MCP để điều khiển thiết bị IoT
- [Luồng tương tác của giao thức MCP](docs/vi/mcp-protocol.md) - Giải thích cách triển khai MCP ở phía thiết bị
- [Tài liệu giao thức truyền thông kết hợp MQTT + UDP](docs/vi/mqtt-udp.md)
- [Tài liệu chi tiết về giao thức WebSocket](docs/vi/websocket.md)

## Cấu hình mô hình lớn

Nếu bạn đã sở hữu một thiết bị Xiaozhi AI và đã kết nối với máy chủ chính thức, hãy đăng nhập bảng điều khiển [xiaozhi.me](https://xiaozhi.me) để cấu hình.

👉 [Video hướng dẫn thao tác backend (giao diện cũ)](https://www.bilibili.com/video/BV1jUCUY2EKM/)

## Dự án nguồn mở liên quan

Nếu muốn tự triển khai máy chủ trên máy tính cá nhân, bạn có thể tham khảo các dự án nguồn mở sau:

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Máy chủ Python
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Máy chủ Java
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Máy chủ Golang

Các dự án khách hàng sử dụng giao thức Xiaozhi:

- [huangjunsen0406/py-xiaozhi](https://github.com/huangjunsen0406/py-xiaozhi) Khách hàng Python
- [TOM88812/xiaozhi-android-client](https://github.com/TOM88812/xiaozhi-android-client) Khách hàng Android
- [100askTeam/xiaozhi-linux](http://github.com/100askTeam/xiaozhi-linux) Khách hàng Linux do 100ask phát triển
- [78/xiaozhi-sf32](https://github.com/78/xiaozhi-sf32) Firmware chip Bluetooth của Sicheng Technology
- [QuecPython/solution-xiaozhiAI](https://github.com/QuecPython/solution-xiaozhiAI) Firmware QuecPython của Quectel

## Về dự án

Đây là một dự án ESP32 mã nguồn mở do Xiage phát hành theo giấy phép MIT. Bạn có thể tự do sử dụng, sửa đổi hoặc dùng cho mục đích thương mại.

Chúng tôi hy vọng dự án giúp mọi người dễ dàng tiếp cận phát triển phần cứng AI và đưa các mô hình ngôn ngữ lớn đang phát triển nhanh chóng vào thiết bị thực tế.

Nếu bạn có bất kỳ ý tưởng hay góp ý nào, hãy tạo Issues hoặc tham gia nhóm QQ: 1011329060.

## Lịch sử Star

<a href="https://star-history.com/#78/xiaozhi-esp32&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
   <img alt="Biểu đồ Star History" src="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
 </picture>
</a>