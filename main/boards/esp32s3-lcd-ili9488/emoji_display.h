#pragma once

#include <libs/gif/lv_gif.h>

#include "display/lcd_display.h"
#include "otto_emoji_gif.h"

/**
 * @brief Lớp hiển thị biểu cảm GIF robot Otto
 * Kế thừa LcdDisplay, thêm hỗ trợ biểu cảm GIF
 */
class EmojiDisplay : public SpiLcdDisplay
{
public:
    /**
     * @brief Hàm khởi tạo, tham số giống như SpiLcdDisplay
     */
    EmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                 int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                 bool swap_xy);

    virtual ~EmojiDisplay() = default;

    // Ghi đè phương thức thiết lập biểu cảm
    virtual void SetEmotion(const char *emotion) override;

    // Ghi đè phương thức thiết lập tin nhắn chat
    virtual void SetChatMessage(const char *role, const char *content) override;

    virtual void SetMusicInfo(const char *song_name) override;

private:
    void SetupGifContainer();

    lv_obj_t *emotion_gif_; ///< Thành phần GIF biểu cảm

    // Ánh xạ biểu cảm
    struct EmotionMap
    {
        const char *name;
        const lv_img_dsc_t *gif;
    };

    static const EmotionMap emotion_maps_[];
};