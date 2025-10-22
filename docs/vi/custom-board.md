# Hướng dẫn tạo bo mạch tuỳ chỉnh

Tài liệu này giới thiệu cách xây dựng chương trình khởi tạo bo mạch mới cho dự án chatbot giọng nói Xiaozhi AI. Xiaozhi AI hỗ trợ hơn 70 bo mạch dòng ESP32, mỗi bo mạch có mã khởi tạo riêng nằm trong thư mục tương ứng.

## Lưu ý quan trọng

> **Cảnh báo**: Đối với bo mạch tự tuỳ biến, nếu cấu hình IO khác với bo mạch gốc, tuyệt đối không ghi đè trực tiếp cấu hình hiện có để biên dịch firmware. Bạn phải tạo một loại bo mạch mới, hoặc trong `config.json` định nghĩa các mục `builds` với `name` và macro `sdkconfig` khác nhau để phân biệt. Sử dụng `python scripts/release.py [tên thư mục bo mạch]` để biên dịch và đóng gói firmware.
>
> Nếu ghi đè cấu hình sẵn có, trong tương lai khi cập nhật OTA, firmware tuỳ biến của bạn có thể bị firmware chuẩn của bo mạch gốc thay thế, dẫn tới thiết bị không hoạt động bình thường. Mỗi bo mạch đều có định danh và kênh nâng cấp riêng, hãy giữ định danh là duy nhất.
## Cấu trúc thư mục

Thư mục của mỗi bo mạch thường bao gồm:

- `xxx_board.cc` - Mã khởi tạo cấp bo mạch, hiện thực hoá các chức năng phần cứng
- `config.h` - Tập tin cấu hình, định nghĩa ánh xạ chân và các tham số khác
- `config.json` - Cấu hình biên dịch, chỉ định chip mục tiêu và tuỳ chọn đặc biệt
- `README.md` - Tài liệu mô tả bo mạch

## Các bước tạo bo mạch

### 1. Tạo thư mục mới

Trong thư mục `boards/`, tạo một thư mục mới đặt tên theo định dạng `[nhãn hiệu]-[model]`, ví dụ `m5stack-tab5`:

```bash
mkdir main/boards/my-custom-board
```

### 2. Tạo tập tin cấu hình

#### config.h

Trong `config.h`, định nghĩa mọi cấu hình phần cứng, bao gồm:

- Tần số lấy mẫu âm thanh và cấu hình chân I2S
- Địa chỉ codec âm thanh và cấu hình chân I2C
- Cấu hình nút bấm và LED
- Tham số và chân kết nối màn hình

Ví dụ tham khảo (từ lichuang-c3-dev):

```c
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Cấu hình âm thanh
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_12
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_8
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_11

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_13
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

// Cấu hình nút bấm
#define BOOT_BUTTON_GPIO        GPIO_NUM_9

// Cấu hình màn hình
#define DISPLAY_SPI_SCK_PIN     GPIO_NUM_3
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_5
#define DISPLAY_DC_PIN          GPIO_NUM_6
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_4

#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_2
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true

#endif // _BOARD_CONFIG_H_
```

#### config.json

Trong `config.json`, định nghĩa cấu hình biên dịch để `scripts/release.py` có thể tự động hoá quá trình build:

```json
{
    "target": "esp32s3",  // Chip mục tiêu: esp32, esp32s3, esp32c3, esp32c6, esp32p4...
    "builds": [
        {
            "name": "my-custom-board",  // Tên bo mạch dùng cho gói firmware
            "sdkconfig_append": [
                // Cấu hình dung lượng Flash đặc biệt
                "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y",
                // Sử dụng bảng phân vùng riêng
                "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/8m.csv\""
            ]
        }
    ]
}
```

**Giải thích các mục cấu hình:**
- `target`: Chip đích, phải khớp phần cứng
- `name`: Tên gói firmware đầu ra, nên trùng với tên thư mục
- `sdkconfig_append`: Mảng các tuỳ chọn sdkconfig bổ sung, được ghép thêm vào cấu hình mặc định

**Các mục `sdkconfig_append` phổ biến:**
```json
// Dung lượng Flash
"CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y"   // Flash 4MB
"CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"   // Flash 8MB
"CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"  // Flash 16MB

// Bảng phân vùng
"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/4m.csv\""  // Bảng cho Flash 4MB
"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/8m.csv\""  // Bảng cho Flash 8MB
"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/16m.csv\"" // Bảng cho Flash 16MB

// Ngôn ngữ
"CONFIG_LANGUAGE_EN_US=y"  // Tiếng Anh
"CONFIG_LANGUAGE_ZH_CN=y"  // Tiếng Trung giản thể

// Từ khoá đánh thức
"CONFIG_USE_DEVICE_AEC=y"          // Bật AEC trên thiết bị
"CONFIG_WAKE_WORD_DISABLED=y"      // Tắt từ khoá đánh thức
```

### 3. Viết mã khởi tạo bo mạch

Tạo tập tin `my_custom_board.cc` và triển khai toàn bộ logic khởi tạo.

Một lớp bo mạch cơ bản bao gồm:

1. **Định nghĩa lớp**: kế thừa `WifiBoard` hoặc `Ml307Board`
2. **Hàm khởi tạo**: thiết lập I2C, màn hình, nút bấm, IoT...
3. **Ghi đè hàm ảo**: như `GetAudioCodec()`, `GetDisplay()`, `GetBacklight()`
4. **Đăng ký bo mạch**: dùng macro `DECLARE_BOARD`

```cpp
#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>

#define TAG "MyCustomBoard"

class MyCustomBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;

    // Khởi tạo I2C
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    // Khởi tạo SPI (cho màn hình)
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // Khởi tạo nút bấm
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // Khởi tạo màn hình (ví dụ ST7789)
    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        // Tạo đối tượng hiển thị
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                    DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // Khởi tạo MCP Tools
    void InitializeTools() {
        // Tham khảo tài liệu MCP
    }

public:
    // Hàm tạo
    MyCustomBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->SetBrightness(100);
    }

    // Lấy codec âm thanh
    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    // Lấy màn hình
    virtual Display* GetDisplay() override {
        return display_;
    }

    // Lấy điều khiển đèn nền
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

// Đăng ký bo mạch
DECLARE_BOARD(MyCustomBoard);
```
### 4. Bổ sung cấu hình hệ thống build
#### Thêm lựa chọn bo mạch trong Kconfig.projbuild
Mở `main/Kconfig.projbuild`, trong phần `choice BOARD_TYPE` thêm mục mới:
```kconfig
choice BOARD_TYPE
    prompt "Board Type"
    default BOARD_TYPE_BREAD_COMPACT_WIFI
    help
        Board type. 开发板类型
    # ... các tuỳ chọn khác ...
    config BOARD_TYPE_MY_CUSTOM_BOARD
        bool "My Custom Board (我的自定义开发板)"
        depends on IDF_TARGET_ESP32S3  # chỉnh theo chip mục tiêu của bạn
endchoice
```

**Lưu ý:**
- `BOARD_TYPE_MY_CUSTOM_BOARD` phải viết hoa và dùng gạch dưới
- `depends on` chỉ định loại chip (`IDF_TARGET_ESP32S3`, `IDF_TARGET_ESP32C3`...)
- Mô tả có thể dùng song ngữ

#### Thêm cấu hình trong CMakeLists.txt

Mở `main/CMakeLists.txt`, trong phần phân nhánh theo loại bo mạch thêm cấu hình mới:

```cmake
# Bổ sung vào chuỗi elseif
elseif(CONFIG_BOARD_TYPE_MY_CUSTOM_BOARD)
    set(BOARD_TYPE "my-custom-board")  # Trùng tên thư mục
    set(BUILTIN_TEXT_FONT font_puhui_basic_20_4)  # Chọn font phù hợp với kích thước màn hình
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)  # Tuỳ chọn, dùng khi cần hiển thị biểu cảm
endif()
```

**Giải thích cấu hình font và biểu cảm:**

Chọn kích cỡ font theo độ phân giải màn hình:
- Màn hình nhỏ (OLED 128x64): `font_puhui_basic_14_1` / `font_awesome_14_1`
- Màn hình trung bình nhỏ (240x240): `font_puhui_basic_16_4` / `font_awesome_16_4`
- Màn hình trung bình (240x320): `font_puhui_basic_20_4` / `font_awesome_20_4`
- Màn hình lớn (>=480x320): `font_puhui_basic_30_4` / `font_awesome_30_4`

Tuỳ chọn bộ biểu cảm:
- `twemoji_32` - Biểu cảm 32x32 (màn hình nhỏ)
- `twemoji_64` - Biểu cảm 64x64 (màn hình lớn)

### 5. Cấu hình và biên dịch

#### Cách 1: Dùng `idf.py`

1. **Thiết lập chip mục tiêu** (lần đầu hoặc khi đổi chip):
   ```bash
   # ESP32-S3
   idf.py set-target esp32s3

   # ESP32-C3
   idf.py set-target esp32c3

   # ESP32
   idf.py set-target esp32
   ```

2. **Xoá cấu hình cũ**:
   ```bash
   idf.py fullclean
   ```

3. **Mở menu cấu hình**:
   ```bash
   idf.py menuconfig
   ```

   Trong menu, đi tới `Xiaozhi Assistant` -> `Board Type` và chọn bo mạch của bạn.

4. **Biên dịch và nạp**:
   ```bash
   idf.py build
   idf.py flash monitor
   ```

#### Cách 2: Dùng script `release.py` (khuyến nghị)

Nếu thư mục bo mạch có `config.json`, bạn có thể chạy:

```bash
python scripts/release.py my-custom-board
```

Script sẽ tự động:
- Đọc `target` trong `config.json` và đặt chip mục tiêu
- Áp dụng các tuỳ chọn trong `sdkconfig_append`
- Biên dịch và đóng gói firmware

### 6. Viết README.md

Trong README mô tả đặc điểm bo mạch, yêu cầu phần cứng, bước biên dịch và nạp firmware.

## Các thành phần bo mạch phổ biến

### 1. Màn hình

Dự án hỗ trợ nhiều driver màn hình, ví dụ:
- ST7789 (SPI)
- ILI9341 (SPI)
- SH8601 (QSPI)
- ...

### 2. Codec âm thanh

Các codec được hỗ trợ:
- ES8311 (phổ biến)
- ES7210 (mảng microphone)
- AW88298 (khuếch đại công suất)
- ...

### 3. Quản lý nguồn

Một số bo mạch sử dụng chip quản lý nguồn:
- AXP2101
- Các PMIC khác

### 4. Điều khiển thiết bị qua MCP

Bạn có thể thêm các MCP tool để AI tương tác với:
- Speaker (điều khiển loa)
- Screen (điều chỉnh độ sáng)
- Battery (đọc dung lượng pin)
- Light (điều khiển đèn)
- ...

## Quan hệ kế thừa giữa các lớp bo mạch

- `Board` - Lớp cơ sở
  - `WifiBoard` - Bo mạch dùng Wi-Fi
  - `Ml307Board` - Bo mạch dùng mô-đun 4G
  - `DualNetworkBoard` - Bo mạch hỗ trợ chuyển đổi Wi-Fi/4G

## Mẹo phát triển

1. **Tham khảo bo mạch tương tự**: nếu thiết kế mới giống bo mạch hiện có, hãy học từ mã nguồn sẵn
2. **Gỡ lỗi theo bước**: triển khai các chức năng cơ bản (màn hình) trước rồi mới thêm âm thanh...
3. **Ánh xạ chân chính xác**: đảm bảo mọi chân được cấu hình đúng trong `config.h`
4. **Kiểm tra tương thích phần cứng**: xác nhận các chip và driver tương thích

## Các sự cố thường gặp

1. **Màn hình hiển thị sai**: kiểm tra cấu hình SPI, thông số đảo chiều, tuỳ chọn đảo màu
2. **Không có âm thanh**: kiểm tra cấu hình I2S, chân bật PA và địa chỉ codec
3. **Không kết nối được mạng**: kiểm tra thông tin Wi-Fi và cấu hình mạng
4. **Không giao tiếp được với máy chủ**: kiểm tra cấu hình MQTT hoặc WebSocket

## Tài liệu tham khảo

- Tài liệu ESP-IDF: https://docs.espressif.com/projects/esp-idf/
- Tài liệu LVGL: https://docs.lvgl.io/
- Tài liệu ESP-SR: https://github.com/espressif/esp-sr