#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_ili9488.h>
#include <esp_log.h>
#include <wifi_station.h>

#include "application.h"
#include "codecs/max98357a_inmp441_codec.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "press_to_talk_mcp_tool.h"
#include "display/lcd_display.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "power_manager.h"
#include "emoji_display.h"
#include "system_reset.h"
#include "wifi_board.h"

#define TAG "ESP32S3LCDILI9488"

class ESP32S3LCDILI9488 : public WifiBoard
{
private:
    LcdDisplay *display_ = nullptr;
    PowerManager *power_manager_ = nullptr;
    Button boot_button_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    PowerSaveTimer *power_save_timer_ = nullptr;
    PressToTalkMcpTool *press_to_talk_tool_ = nullptr;

    void InitializePowerManager()
    {
        power_manager_ =
            new PowerManager(POWER_CHARGE_DETECT_PIN, POWER_ADC_UNIT, POWER_ADC_CHANNEL);
    }

    void InitializePowerSaveTimer()
    {
        power_save_timer_ = new PowerSaveTimer(160, 300);
        power_save_timer_->OnEnterSleepMode([this]()
                                            { GetDisplay()->SetPowerSaveMode(true); });
        power_save_timer_->OnExitSleepMode([this]()
                                           { GetDisplay()->SetPowerSaveMode(false); });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi()
    {
        ESP_LOGI(TAG, "Initializing SPI bus (MOSI:%d, MISO:%d, CLK:%d)",
                 SPI_MOSI, SPI_MISO, SPI_CLOCK);
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.data4_io_num = GPIO_NUM_NC;
        buscfg.data5_io_num = GPIO_NUM_NC;
        buscfg.data6_io_num = GPIO_NUM_NC;
        buscfg.data7_io_num = GPIO_NUM_NC;

        buscfg.max_transfer_sz = DISPLAY_SPI_MAX_TRANSFER_SIZE;
        buscfg.flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO |
                       SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_MASTER;
        buscfg.intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay()
    {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.cs_ena_pretrans = 0;
        io_config.cs_ena_posttrans = 0;
        io_config.flags =
            {
                .dc_low_on_data = 0,
                .octal_mode = 0,
                .sio_mode = 0,
                .lsb_first = 0,
                .cs_high_active = 0};
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");

        const esp_lcd_panel_dev_config_t lcd_config =
            {
                .reset_gpio_num = DISPLAY_RST_PIN,
                .color_space = DISPLAY_RGB_ORDER,
                .bits_per_pixel = 18,
                .flags =
                    {
                        .reset_active_high = 0},
                .vendor_config = NULL};
        static const size_t LV_BUFFER_SIZE = DISPLAY_WIDTH * 25;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(panel_io, &lcd_config, LV_BUFFER_SIZE, &panel));
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 0));
        display_ = new EmojiDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons()
    {
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            if (!press_to_talk_tool_ || !press_to_talk_tool_->IsPressToTalkEnabled()) {
                app.ToggleChatState();
            } });
        boot_button_.OnPressDown([this]()
                                 {
            if (power_save_timer_) {
                power_save_timer_->WakeUp();
            }
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                Application::GetInstance().StartListening();
            } });
        boot_button_.OnPressUp([this]()
                               {
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                Application::GetInstance().StopListening();
            } });
    }

    void RegisterMcpTools()
    {
        auto &mcp_server = McpServer::GetInstance();

        ESP_LOGI(TAG, "Bắt đầu đăng ký công cụ MCP...");

        mcp_server.AddTool("self.battery.get_level", "Lấy mức pin và trạng thái sạc của robot", PropertyList(),
                           [](const PropertyList &properties) -> ReturnValue
                           {
                               auto &board = Board::GetInstance();
                               int level = 0;
                               bool charging = false;
                               bool discharging = false;
                               board.GetBatteryLevel(level, charging, discharging);

                               std::string status =
                                   "{\"level\":" + std::to_string(level) +
                                   ",\"charging\":" + (charging ? "true" : "false") + "}";
                               return status;
                           });

        ESP_LOGI(TAG, "Đăng ký công cụ MCP hoàn tất");
    }

    void InitializeTools()
    {
        press_to_talk_tool_ = new PressToTalkMcpTool();
        press_to_talk_tool_->Initialize();
        static LampController lamp(LAMP_GPIO);
        RegisterMcpTools();
    }

public:
    ESP32S3LCDILI9488() : boot_button_(BOOT_BUTTON_GPIO)
    {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual Led *GetLed() override
    {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec *GetAudioCodec() override
    {
        static Max98357aInmp441Codec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            MAX98357A_SD_MODE_PIN);
        return &audio_codec;
    }

    virtual void SetPowerSaveMode(bool enabled) override
    {
        if (!enabled)
        {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }

    virtual Backlight *GetBacklight() override
    {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC)
        {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) override
    {
        charging = power_manager_->IsCharging();
        discharging = !charging;
        level = power_manager_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(ESP32S3LCDILI9488);
