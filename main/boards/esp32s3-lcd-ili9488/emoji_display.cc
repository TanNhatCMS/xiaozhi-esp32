#include "emoji_display.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <font_awesome.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "display/lcd_display.h"

#define TAG "EmojiDisplay"

// Bảng ánh xạ biểu cảm - Ánh xạ 21 biểu cảm gốc thành 6 GIF hiện có
const EmojiDisplay::EmotionMap EmojiDisplay::emotion_maps_[] = {
    // Biểu cảm trung tính/bình tĩnh -> staticstate
    {"neutral", &staticstate},
    {"relaxed", &staticstate},
    {"sleepy", &staticstate},

    // Biểu cảm tích cực/vui vẻ -> happy
    {"happy", &happy},
    {"laughing", &happy},
    {"funny", &happy},
    {"loving", &happy},
    {"confident", &happy},
    {"winking", &happy},
    {"cool", &happy},
    {"delicious", &happy},
    {"kissy", &happy},
    {"silly", &happy},

    // Biểu cảm buồn bã -> sad
    {"sad", &sad},
    {"crying", &sad},

    // Biểu cảm tức giận -> anger
    {"angry", &anger},

    // Biểu cảm ngạc nhiên -> scare
    {"surprised", &scare},
    {"shocked", &scare},

    // Biểu cảm suy tư/bối rối -> buxue
    {"thinking", &buxue},
    {"confused", &buxue},
    {"embarrassed", &buxue},

    {nullptr, nullptr} // Dấu hiệu kết thúc
};

EmojiDisplay::EmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x,
                           bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy),
      emotion_gif_(nullptr)
{
    SetupGifContainer();
};

void EmojiDisplay::SetupGifContainer()
{
    DisplayLockGuard lock(this);

    if (emoji_label_)
    {
        lv_obj_del(emoji_label_);
    }

    if (chat_message_label_)
    {
        lv_obj_del(chat_message_label_);
    }
    if (content_)
    {
        lv_obj_del(content_);
    }

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(content_, LV_HOR_RES, LV_HOR_RES);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_center(content_);

    emoji_label_ = lv_label_create(content_);
    lv_label_set_text(emoji_label_, "");
    lv_obj_set_width(emoji_label_, 0);
    lv_obj_set_style_border_width(emoji_label_, 0, 0);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    emotion_gif_ = lv_gif_create(content_);
    int gif_size = LV_HOR_RES;
    lv_obj_set_size(emotion_gif_, gif_size, gif_size);
    lv_obj_set_style_border_width(emotion_gif_, 0, 0);
    lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);
    lv_obj_center(emotion_gif_);
    lv_gif_set_src(emotion_gif_, &staticstate);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    lv_obj_set_style_border_width(chat_message_label_, 0, 0);

    lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
    lv_obj_set_style_pad_ver(chat_message_label_, 5, 0);

    lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, 0);

    auto &theme_manager = LvglThemeManager::GetInstance();
    auto theme = theme_manager.GetTheme("dark");
    if (theme != nullptr)
    {
        LcdDisplay::SetTheme(theme);
    }
}

void EmojiDisplay::SetEmotion(const char *emotion)
{
    if (!emotion || !emotion_gif_)
    {
        return;
    }

    DisplayLockGuard lock(this);

    for (const auto &map : emotion_maps_)
    {
        if (map.name && strcmp(map.name, emotion) == 0)
        {
            lv_gif_set_src(emotion_gif_, map.gif);
            ESP_LOGI(TAG, "Đặt biểu cảm: %s", emotion);
            return;
        }
    }

    lv_gif_set_src(emotion_gif_, &staticstate);
    ESP_LOGI(TAG, "Biểu cảm không xác định '%s', sử dụng mặc định", emotion);
}

void EmojiDisplay::SetChatMessage(const char *role, const char *content)
{
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr)
    {
        return;
    }

    if (content == nullptr || strlen(content) == 0)
    {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(chat_message_label_, content);
    lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Đặt tin nhắn chat [%s]: %s", role, content);
}

void EmojiDisplay::SetMusicInfo(const char *song_name)
{
    if (!song_name)
    {
        return;
    }

    DisplayLockGuard lock(this);

    if (chat_message_label_ == nullptr)
    {
        return;
    }

    if (strlen(song_name) > 0)
    {
        std::string music_text = song_name;
        lv_label_set_text(chat_message_label_, music_text.c_str());
        lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

        ESP_LOGI(TAG, "Đặt thông tin nhạc: %s", song_name);
    }
    else
    {
        // Xóa hiển thị thông tin bài hát
        lv_label_set_text(chat_message_label_, "");
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    }
}