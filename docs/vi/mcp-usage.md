# Hướng dẫn sử dụng MCP cho điều khiển IoT

> Tài liệu này mô tả cách sử dụng giao thức MCP để điều khiển thiết bị ESP32 trong hệ sinh thái Xiaozhi. Luồng giao thức chi tiết xem thêm [`mcp-protocol.md`](./mcp-protocol.md).
## Giới thiệu

MCP (Model Context Protocol) là giao thức thế hệ mới được khuyến nghị cho điều khiển IoT. Giao thức sử dụng định dạng chuẩn JSON-RPC 2.0 để phát hiện và gọi các "công cụ" (Tool) giữa máy chủ và thiết bị, từ đó điều khiển linh hoạt.

## Quy trình điển hình

1. Sau khi khởi động, thiết bị thiết lập kết nối với máy chủ thông qua giao thức nền (WebSocket/MQTT...).
2. Máy chủ khởi tạo phiên làm việc bằng phương thức `initialize` của MCP.
3. Máy chủ gọi `tools/list` để lấy danh sách công cụ mà thiết bị hỗ trợ cùng mô tả tham số.
4. Máy chủ gọi `tools/call` với công cụ cụ thể để điều khiển thiết bị.

Chi tiết định dạng và cách tương tác xem thêm [`mcp-protocol.md`](./mcp-protocol.md).

## Đăng ký công cụ phía thiết bị

Thiết bị đăng ký công cụ thông qua phương thức `McpServer::AddTool`. Chữ ký hàm thường dùng:

```cpp
void AddTool(
    const std::string& name,           // Tên công cụ, nên duy nhất và có cấu trúc, ví dụ self.dog.forward
    const std::string& description,    // Mô tả ngắn gọn giúp mô hình lớn hiểu được chức năng
    const PropertyList& properties,    // Danh sách tham số (có thể rỗng), hỗ trợ bool/int/string
    std::function<ReturnValue(const PropertyList&)> callback // Hàm callback thực thi khi công cụ được gọi
);
```
- `name`: Định danh duy nhất, khuyến nghị dạng "module.chức_năng".
- `description`: Mô tả bằng ngôn ngữ tự nhiên giúp AI/người dùng hiểu.
- `properties`: Danh sách tham số hỗ trợ kiểu boolean, số nguyên, chuỗi; có thể khai báo phạm vi và giá trị mặc định.
- `callback`: Logic thực thi khi nhận yêu cầu, trả về `bool/int/string`.

## Ví dụ đăng ký (ESP-Hi)

```cpp
void InitializeTools() {
    auto& mcp_server = McpServer::GetInstance();
    // Ví dụ 1: Không tham số, điều khiển robot tiến lên
    mcp_server.AddTool("self.dog.forward", "Robot tiến về phía trước", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        servo_dog_ctrl_send(DOG_STATE_FORWARD, NULL);
        return true;
    });
    // Ví dụ 2: Có tham số, đặt màu RGB cho đèn
    mcp_server.AddTool("self.light.set_rgb", "Thiết lập màu RGB", PropertyList({
        Property("r", kPropertyTypeInteger, 0, 255),
        Property("g", kPropertyTypeInteger, 0, 255),
        Property("b", kPropertyTypeInteger, 0, 255)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int r = properties["r"].value<int>();
        int g = properties["g"].value<int>();
        int b = properties["b"].value<int>();
        led_on_ = true;
        SetLedColor(r, g, b);
        return true;
    });
}
```

## Ví dụ JSON-RPC phổ biến

### 1. Lấy danh sách công cụ
```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": { "cursor": "" },
  "id": 1
}
```

### 2. Điều khiển khung gầm tiến lên
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.chassis.go_forward",
    "arguments": {}
  },
  "id": 2
}
```

### 3. Chuyển chế độ đèn
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.chassis.switch_light_mode",
    "arguments": { "light_mode": 3 }
  },
  "id": 3
}
```

### 4. Lật góc camera
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_camera_flipped",
    "arguments": {}
  },
  "id": 4
}
```

## Ghi chú
- Tên công cụ, tham số và giá trị trả về cần bám theo đăng ký `AddTool` ở phía thiết bị.
- Khuyến khích các dự án mới sử dụng thống nhất giao thức MCP để điều khiển IoT.
- Xem thêm [`mcp-protocol.md`](./mcp-protocol.md) để hiểu rõ giao thức và các kỹ thuật nâng cao.
