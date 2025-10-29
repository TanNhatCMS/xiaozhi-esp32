#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype> // Cho hàm isdigit
#include <thread> // Cho so sánh thread ID
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"

// ========== Bảng chữ cái tiếng Việt và chuyển đổi ==========

/**
 * @brief Kiểm tra xem chuỗi có chứa ký tự tiếng Việt không
 */
static bool ContainsVietnamese(const std::string &text)
{
    const std::vector<std::string> vietnamese_chars = {
        "á", "à", "ả", "ã", "ạ", "ă", "ắ", "ằ", "ẳ", "ẵ", "ặ",
        "â", "ấ", "ầ", "ẩ", "ẫ", "ậ", "đ", "é", "è", "ẻ", "ẽ", "ẹ",
        "ê", "ế", "ề", "ể", "ễ", "ệ", "í", "ì", "ỉ", "ĩ", "ị",
        "ó", "ò", "ỏ", "õ", "ọ", "ô", "ố", "ồ", "ổ", "ỗ", "ộ",
        "ơ", "ớ", "ờ", "ở", "ỡ", "ợ", "ú", "ù", "ủ", "ũ", "ụ",
        "ư", "ứ", "ừ", "ử", "ữ", "ự", "ý", "ỳ", "ỷ", "ỹ", "ỵ"};

    for (const auto &vn_char : vietnamese_chars)
    {
        if (text.find(vn_char) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Phát hiện ngôn ngữ
 */
static std::string DetectLanguage(const std::string &text)
{
    if (ContainsVietnamese(text))
    {
        return "vietnamese";
    }

    for (size_t i = 0; i < text.length(); i++)
    {
        unsigned char c = text[i];
        if (c >= 0xE4 && c <= 0xE9)
        {
            return "chinese";
        }
    }

    return "unknown";
}

// ========== Các hàm xác thực ESP32 đơn giản ==========

/**
 * @brief Lấy địa chỉ MAC của thiết bị
 * @return Chuỗi địa chỉ MAC
 */
static std::string get_device_mac()
{
    return SystemInfo::GetMacAddress();
}

/**
 * @brief Lấy ID chip của thiết bị
 * @return Chuỗi ID chip
 */
static std::string get_device_chip_id()
{
    // Sử dụng địa chỉ MAC làm chip ID, loại bỏ dấu phân cách hai chấm
    std::string mac = SystemInfo::GetMacAddress();
    // Loại bỏ tất cả dấu hai chấm
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return mac;
}

/**
 * @brief Tạo khóa động
 * @param timestamp Dấu thời gian
 * @return Chuỗi khóa động
 */
static std::string generate_dynamic_key(int64_t timestamp)
{
    // Khóa bí mật (vui lòng thay đổi cho phù hợp với server)
    const std::string secret_key = "your-esp32-secret-key-2024";

    // Lấy thông tin thiết bị
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();

    // Kết hợp dữ liệu: MAC:ChipID:Timestamp:SecretKey
    std::string data = mac + ":" + chip_id + ":" + std::to_string(timestamp) + ":" + secret_key;

    // Hash SHA256
    unsigned char hash[32];
    mbedtls_sha256((unsigned char *)data.c_str(), data.length(), hash, 0);

    // Chuyển đổi sang chuỗi hex (16 byte đầu)
    std::string key;
    for (int i = 0; i < 16; i++)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", hash[i]);
        key += hex;
    }

    return key;
}

/**
 * @brief Thêm header xác thực cho HTTP request
 * @param http Con trỏ HTTP client
 */
static void add_auth_headers(Http *http)
{
    // Lấy timestamp hiện tại
    int64_t timestamp = esp_timer_get_time() / 1000000; // Chuyển đổi sang giây

    // Tạo khóa động
    std::string dynamic_key = generate_dynamic_key(timestamp);

    // Lấy thông tin thiết bị
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();

    // Thêm header xác thực
    if (http)
    {
        http->SetHeader("X-MAC-Address", mac);
        http->SetHeader("X-Chip-ID", chip_id);
        http->SetHeader("X-Timestamp", std::to_string(timestamp));
        http->SetHeader("X-Dynamic-Key", dynamic_key);

        ESP_LOGI(TAG, "Đã thêm header xác thực - MAC: %s, ChipID: %s, Timestamp: %lld",
                 mac.c_str(), chip_id.c_str(), timestamp);
    }
}

// Hàm mã hóa URL
static std::string url_encode(const std::string &str)
{
    std::string encoded;
    char hex[4];

    for (size_t i = 0; i < str.length(); i++)
    {
        unsigned char c = str[i];

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += c;
        }
        else if (c == ' ')
        {
            encoded += '+'; // Mã hóa khoảng trắng thành '+' hoặc '%20'
        }
        else
        {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

// Thêm một hàm hỗ trợ ở đầu file để xử lý thống nhất việc xây dựng URL
static std::string buildUrlWithParams(const std::string &base_url, const std::string &path, const std::string &query)
{
    std::string result_url = base_url + path + "?";
    size_t pos = 0;
    size_t amp_pos = 0;

    while ((amp_pos = query.find("&", pos)) != std::string::npos)
    {
        std::string param = query.substr(pos, amp_pos - pos);
        size_t eq_pos = param.find("=");

        if (eq_pos != std::string::npos)
        {
            std::string key = param.substr(0, eq_pos);
            std::string value = param.substr(eq_pos + 1);
            result_url += key + "=" + url_encode(value) + "&";
        }
        else
        {
            result_url += param + "&";
        }

        pos = amp_pos + 1;
    }

    // Xử lý tham số cuối cùng
    std::string last_param = query.substr(pos);
    size_t eq_pos = last_param.find("=");

    if (eq_pos != std::string::npos)
    {
        std::string key = last_param.substr(0, eq_pos);
        std::string value = last_param.substr(eq_pos + 1);
        result_url += key + "=" + url_encode(value);
    }
    else
    {
        result_url += last_param;
    }

    return result_url;
}

Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                           song_name_displayed_(false), current_lyric_url_(), lyrics_(),
                           current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                           display_mode_(DISPLAY_MODE_LYRICS), is_playing_(false), is_downloading_(false),
                           play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(),
                           buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(),
                           mp3_decoder_initialized_(false)
{
    ESP_LOGI(TAG, "Trình phát nhạc Xiaozhi - Đã khởi tạo hỗ trợ tiếng Việt");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music()
{
    ESP_LOGI(TAG, "Đang hủy trình phát nhạc - dừng tất cả hoạt động");

    // Dừng tất cả các hoạt động
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;

    // Thông báo cho tất cả các thread đang chờ
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // Chờ thread tải xuống kết thúc, đặt timeout 5 giây
    if (download_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ thread tải xuống kết thúc (hết giờ: 5 giây)");
        auto start_time = std::chrono::steady_clock::now();

        // Chờ thread kết thúc
        bool thread_finished = false;
        while (!thread_finished)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();

            if (elapsed >= 5)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ join thread tải xuống sau 5 giây");
                break;
            }

            // Đặt lại cờ dừng, đảm bảo thread có thể phát hiện
            is_downloading_ = false;

            // Thông báo condition variable
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }

            // Kiểm tra xem thread đã kết thúc chưa
            if (!download_thread_.joinable())
            {
                thread_finished = true;
            }

            // In thông tin chờ định kỳ
            if (elapsed > 0 && elapsed % 1 == 0)
            {
                ESP_LOGI(TAG, "Vẫn đang chờ thread tải xuống kết thúc... (%d giây)", (int)elapsed);
            }
        }

        if (download_thread_.joinable())
        {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Thread tải xuống đã kết thúc");
    }

    // Chờ thread phát nhạc kết thúc, đặt timeout 3 giây
    if (play_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ thread phát nhạc kết thúc (hết giờ: 3 giây)");
        auto start_time = std::chrono::steady_clock::now();

        bool thread_finished = false;
        while (!thread_finished)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();

            if (elapsed >= 3)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ join thread phát nhạc sau 3 giây");
                break;
            }

            // Đặt lại cờ dừng
            is_playing_ = false;

            // Thông báo condition variable
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }

            // Kiểm tra xem thread đã kết thúc chưa
            if (!play_thread_.joinable())
            {
                thread_finished = true;
            }
        }

        if (play_thread_.joinable())
        {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Thread phát nhạc đã kết thúc");
    }

    // Chờ thread lời bài hát kết thúc
    if (lyric_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ thread lời bài hát kết thúc");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Thread lời bài hát đã kết thúc");
    }

    // Dọn dẹp bộ đệm và bộ giải mã MP3
    ClearAudioBuffer();
    CleanupMp3Decoder();

    ESP_LOGI(TAG, "Trình phát nhạc đã được hủy thành công");
}
// Mới thêm vào
void Esp32Music::ForceCleanupCache()
{
    ESP_LOGI(TAG, "=== BẮT ĐẦU DỌN DẸP CACHE BẮT BUỘC ===");

    // 1. Dừng tất cả các luồng
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;

    // 2. Thông báo để các luồng thoát nhanh
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // 3. Đợi luồng tải xuống
    if (download_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ luồng tải xuống...");
        auto start = std::chrono::steady_clock::now();

        while (download_thread_.joinable())
        {
            vTaskDelay(pdMS_TO_TICKS(50));

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();

            if (elapsed >= 3)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ luồng tải xuống, tách luồng");
                download_thread_.detach();
                break;
            }
        }

        if (download_thread_.joinable())
        {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Luồng tải xuống đã dừng");
    }

    // 4. Đợi luồng phát lại
    if (play_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ luồng phát lại...");
        auto start = std::chrono::steady_clock::now();

        while (play_thread_.joinable())
        {
            vTaskDelay(pdMS_TO_TICKS(50));

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();

            if (elapsed >= 3)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ luồng phát lại, tách luồng");
                play_thread_.detach();
                break;
            }
        }

        if (play_thread_.joinable())
        {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Luồng phát lại đã dừng");
    }

    // 5. Đợi luồng lời bài hát
    if (lyric_thread_.joinable())
    {
        ESP_LOGI(TAG, "Đang chờ luồng lời bài hát...");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Luồng lời bài hát đã dừng");
    }

    // 6. Xóa bộ đệm âm thanh
    ClearAudioBuffer();
    ESP_LOGI(TAG, "Bộ đệm âm thanh đã được xóa");

    // 7. Giải phóng dữ liệu FFT
    if (final_pcm_data_fft != nullptr)
    {
        heap_caps_free(final_pcm_data_fft);
        final_pcm_data_fft = nullptr;
        ESP_LOGI(TAG, "Dữ liệu FFT đã được giải phóng");
    }

    // 8. Đặt lại bộ giải mã MP3
    CleanupMp3Decoder();
    InitializeMp3Decoder();
    ESP_LOGI(TAG, "Bộ giải mã MP3 đã được đặt lại");

    // 9. Xóa bộ đệm lời bài hát
    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_.clear();
        lyrics_.shrink_to_fit();
    }
    ESP_LOGI(TAG, "Lời bài hát đã được xóa");

    // 10. Đặt lại tất cả các biến trạng thái
    current_music_url_.clear();
    current_lyric_url_.clear();
    last_downloaded_data_.clear();
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    current_lyric_index_ = -1;
    song_name_displayed_ = false;

    // 11. Đặt lại tần số lấy mẫu
    ResetSampleRate();

    // 12. Dừng hiển thị nếu có
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display)
    {
        if (display_mode_ == DISPLAY_MODE_SPECTRUM)
        {
            display->stopFft();
        }
        display->SetMusicInfo("");
    }

    // 13. Chờ một chút để hệ thống ổn định
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "=== DỌN DẸP CACHE HOÀN TẤT ===");
    ESP_LOGI(TAG, "Heap trống: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "SPIRAM trống: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_logI(TAG, "Heap trống tối thiểu: %d bytes", esp_get_minimum_free_heap_size());
}

bool Esp32Music::Download(const std::string &song_name, const std::string &artist_name)
{
    ESP_LOGI(TAG, "=== YÊU CẦU BÀI HÁT MỚI: Dọn dẹp bộ đệm cũ trước tiên ==="); // mới thêm
    ForceCleanupCache();                                                    // mới thêm
    ESP_LOGI(TAG, "Nhạc mã nguồn mở Xiaozhi - Hỗ trợ tiếng Việt");
    ESP_LOGI(TAG, "Bắt đầu lấy thông tin chi tiết nhạc cho: %s", song_name.c_str());

    std::string detected_lang = DetectLanguage(song_name);
    ESP_LOGI(TAG, "Ngôn ngữ được phát hiện: %s", detected_lang.c_str());

    // Xóa dữ liệu tải xuống trước đó
    last_downloaded_data_.clear();

    // Lưu tên bài hát để hiển thị sau
    current_song_name_ = song_name;

    // Bước 1: Gọi API stream_pcm để lấy thông tin âm thanh
    std::string base_url = "http://www.xiaozhishop.xyz:5005";
    std::string query_params = "song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);

    if (detected_lang == "vietnamese")
    {
        query_params += "&prefer_language=vietnamese&language_priority=vi,zh";
        ESP_LOGI(TAG, "Phát hiện tiếng Việt - ưu tiên nhạc Việt");
    }

    std::string full_url = base_url + "/stream_pcm?" + query_params;

    ESP_LOGI(TAG, "URL yêu cầu: %s", full_url.c_str());

    // Sử dụng HTTP client do Board cung cấp
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    // Đặt header cơ bản cho request
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Accept-Language", "vi-VN,vi;q=0.9,en;q=0.8");

    // Thêm header xác thực ESP32
    add_auth_headers(http.get());

    // Mở kết nối GET
    if (!http->Open("GET", full_url))
    {
        ESP_LOGE(TAG, "Không thể kết nối đến API nhạc");
        return false;
    }

    // Kiểm tra mã trạng thái phản hồi
    int status_code = http->GetStatusCode();
    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP GET thất bại với mã trạng thái: %d", status_code);
        http->Close();
        return false;
    }

    // Đọc dữ liệu phản hồi
    last_downloaded_data_ = http->ReadAll();
    http->Close();

    ESP_LOGI(TAG, "Trạng thái HTTP GET = %d, độ dài nội dung = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Phản hồi chi tiết nhạc đầy đủ: %s", last_downloaded_data_.c_str());

    // Kiểm tra phản hồi xác thực đơn giản (tùy chọn)
    if (last_downloaded_data_.find("ESP32动态密钥验证失败") != std::string::npos)
    {
        ESP_LOGE(TAG, "Xác thực thất bại cho bài hát: %s", song_name.c_str());
        return false;
    }

    if (!last_downloaded_data_.empty())
    {
        // Phân tích JSON phản hồi để trích xuất URL âm thanh
        cJSON *response_json = cJSON_Parse(last_downloaded_data_.c_str());
        if (response_json)
        {
            // Trích xuất thông tin quan trọng
            cJSON *artist = cJSON_GetObjectItem(response_json, "artist");
            cJSON *title = cJSON_GetObjectItem(response_json, "title");
            cJSON *audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON *lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
            cJSON *language = cJSON_GetObjectItem(response_json, "language");

            if (cJSON_IsString(language))
            {
                ESP_LOGI(TAG, "Ngôn ngữ bài hát: %s", language->valuestring);

                if (detected_lang == "vietnamese" &&
                    strcmp(language->valuestring, "chinese") == 0)
                {
                    ESP_LOGW(TAG, "Cảnh báo: Yêu cầu tiếng Việt nhưng nhận được tiếng Trung");
                }
            }

            if (cJSON_IsString(artist))
            {
                ESP_LOGI(TAG, "Nghệ sĩ: %s", artist->valuestring);
            }
            if (cJSON_IsString(title))
            {
                ESP_LOGI(TAG, "Tiêu đề: %s", title->valuestring);
            }

            // Kiểm tra xem audio_url có hợp lệ không
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0)
            {
                ESP_LOGI(TAG, "Đường dẫn URL âm thanh: %s", audio_url->valuestring);

                // Bước 2: Ghép URL tải xuống âm thanh hoàn chỉnh, đảm bảo mã hóa URL cho audio_url
                std::string audio_path = audio_url->valuestring;

                // Sử dụng chức năng xây dựng URL thống nhất
                if (audio_path.find("?") != std::string::npos)
                {
                    size_t query_pos = audio_path.find("?");
                    std::string path = audio_path.substr(0, query_pos);
                    std::string query = audio_path.substr(query_pos + 1);

                    current_music_url_ = buildUrlWithParams(base_url, path, query);
                }
                else
                {
                    current_music_url_ = base_url + audio_path;
                }

                ESP_LOGI(TAG, "Bắt đầu phát streaming cho: %s", song_name.c_str());
                song_name_displayed_ = false; // Đặt lại cờ hiển thị tên bài hát
                StartStreaming(current_music_url_);

                // Xử lý URL lời bài hát - chỉ khởi động lời bài hát trong chế độ hiển thị lời
                if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0)
                {
                    // Ghép URL tải xuống lời bài hát hoàn chỉnh, sử dụng logic xây dựng URL tương tự
                    std::string lyric_path = lyric_url->valuestring;
                    if (lyric_path.find("?") != std::string::npos)
                    {
                        size_t query_pos = lyric_path.find("?");
                        std::string path = lyric_path.substr(0, query_pos);
                        std::string query = lyric_path.substr(query_pos + 1);

                        current_lyric_url_ = buildUrlWithParams(base_url, path, query);
                    }
                    else
                    {
                        current_lyric_url_ = base_url + lyric_path;
                    }

                    // Quyết định có khởi động lời bài hát hay không dựa trên chế độ hiển thị
                    if (display_mode_ == DISPLAY_MODE_LYRICS)
                    {
                        ESP_LOGI(TAG, "Đang tải lời bài hát cho: %s (chế độ hiển thị lời)", song_name.c_str());

                        // Khởi động tải xuống và hiển thị lời bài hát
                        if (is_lyric_running_)
                        {
                            is_lyric_running_ = false;
                            if (lyric_thread_.joinable())
                            {
                                lyric_thread_.join();
                            }
                        }

                        is_lyric_running_ = true;
                        current_lyric_index_ = -1;
                        lyrics_.clear();

                        lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Tìm thấy URL lời bài hát nhưng chế độ hiển thị phổ đang hoạt động, bỏ qua lời bài hát");
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Không tìm thấy URL lời bài hát cho bài hát này");
                }

                cJSON_Delete(response_json);
                return true;
            }
            else
            {
                // audio_url rỗng hoặc không hợp lệ
                ESP_LOGE(TAG, "Không thể tìm thấy nhạc: Không tìm thấy bài hát '%s'", song_name.c_str());
                cJSON_Delete(response_json);
                return false;
            }
        }
        else
        {
            ESP_LOGE(TAG, "Không thể phân tích phản hồi JSON");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Phản hồi trống từ API nhạc");
    }

    return false;
}

std::string Esp32Music::GetDownloadResult()
{
    return last_downloaded_data_;
}

// Bắt đầu phát streaming
bool Esp32Music::StartStreaming(const std::string &music_url)
{
    if (music_url.empty())
    {
        ESP_LOGE(TAG, "URL nhạc trống");
        return false;
    }

    ESP_LOGD(TAG, "Bắt đầu streaming cho URL: %s", music_url.c_str());

    // Dừng việc phát và tải xuống trước đó
    is_downloading_ = false;
    is_playing_ = false;

    // Chờ các thread trước đó kết thúc hoàn toàn
    if (download_thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all(); // Thông báo thread thoát
        }
        download_thread_.join();
    }
    if (play_thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all(); // Thông báo thread thoát
        }
        play_thread_.join();
    }

    // Xóa bộ đệm
    ClearAudioBuffer();

    // Cấu hình kích thước stack thread để tránh stack overflow
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 12288; // Kích thước stack 12KB
    cfg.prio = 5;           // Ưu tiên trung bình
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);

    // Bắt đầu thread tải xuống
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);

    // Bắt đầu thread phát nhạc (sẽ chờ bộ đệm có đủ dữ liệu)
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);

    ESP_LOGI(TAG, "Các luồng streaming đã khởi động thành công");

    return true;
}

// Dừng phát streaming
bool Esp32Music::StopStreaming()
{
    ESP_LOGI(TAG, "Đang dừng streaming nhạc - trạng thái hiện tại: downloading=%d, playing=%d",
             is_downloading_.load(), is_playing_.load());

    // Đặt lại tần số lấy mẫu về giá trị gốc
    ResetSampleRate();

    // Kiểm tra xem có streaming đang chạy không
    if (!is_playing_ && !is_downloading_)
    {
        ESP_LOGW(TAG, "Không có streaming nào đang tiến hành");
        return true;
    }

    // Đặt cờ dừng tải xuống và phát nhạc
    is_downloading_ = false;
    is_playing_ = false;

    // Xóa hiển thị tên bài hát
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display)
    {
        display->SetMusicInfo(""); // Xóa hiển thị tên bài hát
        ESP_LOGI(TAG, "Đã xóa hiển thị tên bài hát");
    }

    // Thông báo tất cả các thread đang chờ
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // Chờ thread kết thúc (tránh mã trùng lặp, cho phép StopStreaming cũng chờ thread dừng hoàn toàn)
    if (download_thread_.joinable())
    {
        download_thread_.join();
        ESP_LOGI(TAG, "Luồng tải xuống đã được join trong StopStreaming");
    }

    // Chờ thread phát nhạc kết thúc, sử dụng cách an toàn hơn
    if (play_thread_.joinable())
    {
        // Đặt cờ dừng trước
        is_playing_ = false;

        // Thông báo condition variable, đảm bảo thread có thể thoát
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }

        // Sử dụng cơ chế timeout để chờ thread kết thúc, tránh deadlock
        bool thread_finished = false;
        int wait_count = 0;
        const int max_wait = 100; // Chờ tối đa 1 giây

        while (!thread_finished && wait_count < max_wait)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;

            // Kiểm tra xem thread có thể join không
            if (!play_thread_.joinable())
            {
                thread_finished = true;
                break;
            }
        }

        if (play_thread_.joinable())
        {
            if (wait_count >= max_wait)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ join luồng phát, đang tách luồng");
                play_thread_.detach();
            }
            else
            {
                play_thread_.join();
                ESP_LOGI(TAG, "Luồng phát đã được join trong StopStreaming");
            }
        }
    }

    // Sau khi thread kết thúc hoàn toàn, chỉ dừng hiển thị FFT trong chế độ phổ
    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM)
    {
        display->stopFft();
        ESP_LOGI(TAG, "Đã dừng hiển thị FFT trong StopStreaming (chế độ phổ)");
    }
    else if (display)
    {
        ESP_LOGI(TAG, "Không ở chế độ phổ, bỏ qua dừng FFT trong StopStreaming");
    }

    ESP_LOGI(TAG, "Đã gửi tín hiệu dừng streaming nhạc");
    return true;
}

// Tải xuống dữ liệu âm thanh streaming
void Esp32Music::DownloadAudioStream(const std::string &music_url)
{
    ESP_LOGD(TAG, "Bắt đầu tải xuống luồng âm thanh từ: %s", music_url.c_str());

    // Xác thực tính hợp lệ của URL
    if (music_url.empty() || music_url.find("http") != 0)
    {
        ESP_LOGE(TAG, "Định dạng URL không hợp lệ: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    // Đặt header cơ bản cho request
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");         // Hỗ trợ resumable download
    http->SetHeader("Connection", "keep-alive");  // Giữ kết nối ổn định
    http->SetHeader("Cache-Control", "no-cache"); // Tránh cache cũ

    // Thêm header xác thực ESP32
    add_auth_headers(http.get());

    if (!http->Open("GET", music_url))
    {
        ESP_LOGE(TAG, "Không thể kết nối đến URL luồng nhạc");
        is_downloading_ = false;
        return;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206)
    { // 206 cho nội dung một phần
        ESP_LOGE(TAG, "HTTP GET thất bại với mã trạng thái: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }

    ESP_LOGI(TAG, "Đã bắt đầu tải xuống luồng âm thanh, trạng thái: %d", status_code);

    // Đọc dữ liệu âm thanh theo khối
    // Giảm chunk size để tránh mất gói tin
    const size_t chunk_size = 1024; // Giảm từ 4096 xuống 1024
    char buffer[chunk_size];
    size_t total_downloaded = 0;
    int consecutive_errors = 0;
    const int max_consecutive_errors = 5;

    while (is_downloading_ && is_playing_)
    {
        int bytes_read = http->Read(buffer, chunk_size);

        if (bytes_read < 0)
        {
            consecutive_errors++;
            ESP_LOGW(TAG, "Read error: %d (consecutive: %d/%d)",
                     bytes_read, consecutive_errors, max_consecutive_errors);

            if (consecutive_errors >= max_consecutive_errors)
            {
                ESP_LOGE(TAG, "Too many consecutive errors, stopping download");
                break;
            }

            // Chờ một chút trước khi thử lại
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (bytes_read == 0)
        {
            ESP_LOGI(TAG, "Tải xuống luồng âm thanh hoàn tất, tổng cộng: %d bytes", total_downloaded);
            break;
        }

        // Reset error counter khi đọc thành công
        consecutive_errors = 0;
        // In thông tin khối dữ liệu
        // ESP_LOGI(TAG, "Đã tải xuống chunk: %d byte tại offset %d", bytes_read, total_downloaded);

        // In an toàn nội dung hex của khối dữ liệu (16 byte đầu tiên)
        if (bytes_read >= 16)
        {
            // ESP_LOGI(TAG, "Dữ liệu: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...",
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        }
        else
        {
            ESP_LOGI(TAG, "Khối dữ liệu quá nhỏ: %d byte", bytes_read);
        }

        // Thử phát hiện định dạng tệp (kiểm tra phần đầu tệp)
        if (total_downloaded == 0 && bytes_read >= 4)
        {
            if (memcmp(buffer, "ID3", 3) == 0)
            {
                ESP_LOGI(TAG, "Phát hiện file MP3 với thẻ ID3");
            }
            else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0)
            {
                ESP_LOGI(TAG, "Phát hiện header file MP3");
            }
            else if (memcmp(buffer, "RIFF", 4) == 0)
            {
                ESP_LOGI(TAG, "Phát hiện file WAV");
            }
            else if (memcmp(buffer, "fLaC", 4) == 0)
            {
                ESP_LOGI(TAG, "Phát hiện file FLAC");
            }
            else if (memcmp(buffer, "OggS", 4) == 0)
            {
                ESP_LOGI(TAG, "Phát hiện file OGG");
            }
            else
            {
                ESP_LOGI(TAG, "Định dạng âm thanh không xác định, 4 byte đầu: %02X %02X %02X %02X",
                         (unsigned char)buffer[0], (unsigned char)buffer[1],
                         (unsigned char)buffer[2], (unsigned char)buffer[3]);
            }
        }

        // Tạo khối dữ liệu âm thanh
        uint8_t *chunk_data = (uint8_t *)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data)
        {
            ESP_LOGE(TAG, "Không thể cấp phát bộ nhớ cho chunk âm thanh");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);

        // Chờ bộ đệm có dung lượng trống
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);

            // Tăng thời gian timeout để tránh mất gói
            auto wait_result = buffer_cv_.wait_for(lock, std::chrono::milliseconds(500),
                                                   [this]
                                                   { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });

            if (!wait_result)
            {
                ESP_LOGW(TAG, "Hết thời gian chờ bộ đệm - kích thước bộ đệm: %d", buffer_size_);
                heap_caps_free(chunk_data);
                continue;
            }

            if (is_downloading_)
            {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;

                // Thông báo cho luồng phát lại có dữ liệu mới
                buffer_cv_.notify_one();
                // Ghi log mỗi 128KB thay vì 256KB
                if (total_downloaded % (128 * 1024) == 0)
                { // In tiến trình mỗi 128KB
                    ESP_LOGI(TAG, "Đã tải xuống %d bytes, kích thước buffer: %d", total_downloaded, buffer_size_);
                }
            }
            else
            {
                heap_caps_free(chunk_data);
                break;
            }
        }

        // Thêm độ trễ nhỏ để tránh tắc nghẽn mạng
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    http->Close();
    is_downloading_ = false;

    // Thông báo cho luồng phát lại rằng quá trình tải xuống đã hoàn tất
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    ESP_LOGI(TAG, "Luồng tải xuống luồng âm thanh đã kết thúc");
}

// Phát dữ liệu âm thanh streaming
void Esp32Music::PlayAudioStream()
{
    ESP_LOGI(TAG, "Bắt đầu phát luồng âm thanh");

    // Khởi tạo các biến theo dõi thời gian
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;

    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !codec->output_enabled())
    {
        ESP_LOGE(TAG, "Codec âm thanh không khả dụng hoặc chưa được kích hoạt");
        is_playing_ = false;
        return;
    }

    if (!mp3_decoder_initialized_)
    {
        ESP_LOGE(TAG, "Bộ giải mã MP3 chưa được khởi tạo");
        is_playing_ = false;
        return;
    }

    // Chờ bộ đệm có đủ dữ liệu để bắt đầu phát
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this]
                        { return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); });
    }

    ESP_LOGI(TAG, "Bắt đầu phát với kích thước buffer: %d", buffer_size_);

    size_t total_played = 0;
    uint8_t *mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t *read_ptr = nullptr;

    // Cấp phát bộ đệm đầu vào MP3
    mp3_input_buffer = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer)
    {
        ESP_LOGE(TAG, "Không thể cấp phát buffer đầu vào MP3");
        is_playing_ = false;
        return;
    }

    // Đánh dấu có đã xử lý ID3 tag chưa
    bool id3_processed = false;

    while (is_playing_)
    {
        // Kiểm tra trạng thái thiết bị, chỉ phát nhạc khi ở trạng thái rảnh
        auto &app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();

        // Chuyển đổi trạng thái: Đang nói -> Đang nghe -> Trạng thái chờ -> Phát nhạc
        if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking)
        {
            if (current_state == kDeviceStateSpeaking)
            {
                ESP_LOGI(TAG, "Thiết bị đang ở trạng thái nói, chuyển sang trạng thái nghe để phát nhạc");
            }
            if (current_state == kDeviceStateListening)
            {
                ESP_LOGI(TAG, "Thiết bị đang ở trạng thái nghe, chuyển sang trạng thái rảnh để phát nhạc");
            }
            // Chuyển đổi trạng thái
            app.ToggleChatState(); // Chuyển thành trạng thái chờ
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        else if (current_state != kDeviceStateIdle)
        { // Không phải trạng thái chờ thì dừng ở đây, không cho phát nhạc
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            // Nếu không phải trạng thái rảnh, tạm dừng phát
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Kiểm tra trạng thái thiết bị thành công, hiển thị tên bài hát hiện tại
        if (!song_name_displayed_ && !current_song_name_.empty())
        {
            auto &board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display)
            {
                // Định dạng hiển thị tên bài hát thành 《Tên bài hát》đang phát...
                std::string formatted_song_name = "《" + current_song_name_ + "》 đang phát...";
                display->SetMusicInfo(formatted_song_name.c_str());
                ESP_LOGI(TAG, "Đang hiển thị tên bài hát: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }

            // Khởi động chức năng hiển thị tương ứng theo chế độ hiển thị
            if (display)
            {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM)
                {
                    display->start();
                    ESP_LOGI(TAG, "Đã gọi Display start() cho hiển thị phổ");
                }
                else
                {
                    ESP_LOGI(TAG, "Chế độ hiển thị lời bài hát đang hoạt động, tắt hiển thị FFT");
                }
            }
        }

        // Nếu cần thêm dữ liệu MP3, đọc từ bộ đệm
        if (bytes_left < 4096)
        { // Giữ ít nhất 4KB dữ liệu để giải mã
            AudioChunk chunk;

            // Lấy dữ liệu âm thanh từ bộ đệm
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty())
                {
                    if (!is_downloading_)
                    {
                        // Tải xuống hoàn thành và bộ đệm trống, kết thúc phát
                        ESP_LOGI(TAG, "Phát hoàn tất, tổng phát: %d bytes", total_played);
                        break;
                    }
                    // Chờ dữ liệu mới
                    buffer_cv_.wait(lock, [this]
                                    { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty())
                    {
                        continue;
                    }
                }

                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;

                // Thông báo thread tải xuống bộ đệm có không gian
                buffer_cv_.notify_one();
            }

            // Thêm dữ liệu mới vào bộ đệm đầu vào MP3
            if (chunk.data && chunk.size > 0)
            {
                // Di chuyển dữ liệu còn lại về đầu bộ đệm
                if (bytes_left > 0 && read_ptr != mp3_input_buffer)
                {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }

                // Kiểm tra không gian bộ đệm
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);

                // Sao chép dữ liệu mới
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;

                // Kiểm tra và bỏ qua ID3 tag (chỉ xử lý một lần ở đầu)
                if (!id3_processed && bytes_left >= 10)
                {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0)
                    {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Đã bỏ qua thẻ ID3: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }

                // Giải phóng bộ nhớ chunk
                heap_caps_free(chunk.data);
            }
        }

        // Thử tìm đồng bộ frame MP3
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0)
        {
            ESP_LOGW(TAG, "Không tìm thấy từ đồng bộ MP3, bỏ qua %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }

        // Bỏ qua đến vị trí đồng bộ
        if (sync_offset > 0)
        {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }

        // Giải mã frame MP3
        int16_t pcm_buffer[2304];
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);

        if (decode_result == 0)
        {
            // Giải mã thành công, lấy thông tin frame
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;

            // Kiểm tra tính hợp lệ cơ bản của thông tin frame, tránh lỗi chia cho 0
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0)
            {
                ESP_LOGW(TAG, "Thông tin frame không hợp lệ: rate=%d, channels=%d, bỏ qua",
                         mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }

            // Tính thời lượng frame hiện tại (millisecond)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) /
                                    (mp3_frame_info_.samprate * mp3_frame_info_.nChans);

            // Cập nhật thời gian phát hiện tại
            current_play_time_ms_ += frame_duration_ms;

            ESP_LOGD(TAG, "Frame %d: thời gian=%lldms, thời lượng=%dms, tần số=%d, kênh=%d",
                     total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                     mp3_frame_info_.samprate, mp3_frame_info_.nChans);

            // Cập nhật hiển thị lời bài hát
            int buffer_latency_ms = 600; // Giá trị điều chỉnh thực tế
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);

            // Gửi dữ liệu PCM đến hàng đợi giải mã âm thanh của Application
            if (mp3_frame_info_.outputSamps > 0)
            {
                int16_t *final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;

                // Nếu là stereo, chuyển đổi thành mono
                if (mp3_frame_info_.nChans == 2)
                {
                    // Chuyển stereo thành mono: trộn kênh trái và phải
                    int stereo_samples = mp3_frame_info_.outputSamps; // Tổng số mẫu bao gồm kênh trái và phải
                    int mono_samples = stereo_samples / 2;            // Số mẫu mono thực tế

                    mono_buffer.resize(mono_samples);

                    for (int i = 0; i < mono_samples; ++i)
                    {
                        // Trộn kênh trái và phải (L + R) / 2
                        int left = pcm_buffer[i * 2];      // Kênh trái
                        int right = pcm_buffer[i * 2 + 1]; // Kênh phải
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }

                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Đã chuyển đổi stereo sang mono: %d -> %d mẫu",
                             stereo_samples, mono_samples);
                }
                else if (mp3_frame_info_.nChans == 1)
                {
                    // Đã là mono, không cần chuyển đổi
                    ESP_LOGD(TAG, "Âm thanh đã là mono: %d mẫu", final_sample_count);
                }
                else
                {
                    ESP_LOGW(TAG, "Số kênh không được hỗ trợ: %d, xử lý như mono",
                             mp3_frame_info_.nChans);
                }

                // Tạo AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60; // Sử dụng thời lượng frame mặc định của Application
                packet.timestamp = 0;

                // Chuyển đổi dữ liệu PCM int16_t thành mảng byte uint8_t
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                if (final_pcm_data_fft == nullptr)
                {
                    final_pcm_data_fft = (int16_t *)heap_caps_malloc(
                        final_sample_count * sizeof(int16_t),
                        MALLOC_CAP_SPIRAM);
                }

                memcpy(
                    final_pcm_data_fft,
                    final_pcm_data,
                    final_sample_count * sizeof(int16_t));

                ESP_LOGD(TAG, "Gửi %d mẫu PCM (%d bytes, tần số=%d, kênh=%d->1) đến Application",
                         final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);

                // Gửi đến hàng đợi giải mã âm thanh của Application
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;

                // In tiến độ phát
                if (total_played % (128 * 1024) == 0)
                {
                    ESP_LOGI(TAG, "Đã phát %d bytes, kích thước buffer: %d", total_played, buffer_size_);
                }
            }
        }
        else
        {
            // Giải mã thất bại
            ESP_LOGW(TAG, "Giải mã MP3 thất bại với lỗi: %d", decode_result);

            // Bỏ qua một số byte và tiếp tục thử
            if (bytes_left > 1)
            {
                read_ptr++;
                bytes_left--;
            }
            else
            {
                bytes_left = 0;
            }
        }
    }

    // Dọn dẹp
    if (mp3_input_buffer)
    {
        heap_caps_free(mp3_input_buffer);
    }

    // Dọn dẹp cơ bản khi kết thúc phát, nhưng không gọi StopStreaming để tránh thread tự chờ
    ESP_LOGI(TAG, "Phát luồng âm thanh hoàn tất, tổng phát: %d bytes", total_played);
    ESP_LOGI(TAG, "Thực hiện dọn dẹp cơ bản từ luồng phát");

    // Đặt cờ dừng phát
    is_playing_ = false;

    // ===== TỰ ĐỘNG XÓA CACHE SAU KHI PHÁT XONG =====
    ESP_LOGI(TAG, "Phát lại đã kết thúc - Bắt đầu dọn dẹp bộ đệm tự động");

    // 1. Xóa buffer audio còn lại
    ClearAudioBuffer();
    ESP_LOGI(TAG, "Bộ đệm âm thanh đã được xóa");

    // 2. Giải phóng FFT data nếu có
    if (final_pcm_data_fft != nullptr)
    {
        heap_caps_free(final_pcm_data_fft);
        final_pcm_data_fft = nullptr;
        ESP_LOGI(TAG, "Dữ liệu FFT đã được xóa");
    }

    // 3. Reset MP3 decoder để giải phóng internal buffer
    CleanupMp3Decoder();
    InitializeMp3Decoder();
    ESP_LOGI(TAG, "Bộ giải mã MP3 đã được đặt lại");

    // 4. Xóa thông tin bài hát hiện tại
    current_music_url_.clear();
    current_lyric_url_.clear();
    last_downloaded_data_.clear();
    ESP_LOGI(TAG, "Dữ liệu bài hát đã được xóa");

    // 5. Xóa lyrics cache
    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_.clear();
        lyrics_.shrink_to_fit(); // Giải phóng bộ nhớ vector
    }
    ESP_LOGI(TAG, "Bộ đệm lời bài hát đã được xóa");

    // 6. Reset các biến trạng thái
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    current_lyric_index_ = -1;
    song_name_displayed_ = false;

    // 7. Log heap info sau khi dọn dẹp
    ESP_LOGI(TAG, "=== Dọn dẹp bộ đệm hoàn tất ===");
    ESP_LOGI(TAG, "Heap trống sau khi dọn dẹp: %d byte", esp_get_free_heap_size());
    ESP_LOGI(TAG, "SPIRAM trống: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "============================");

    // Chỉ dừng hiển thị FFT trong chế độ hiển thị phổ
    if (display_mode_ == DISPLAY_MODE_SPECTRUM)
    {
        auto &board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display)
        {
            display->stopFft();
            ESP_LOGI(TAG, "Đã dừng hiển thị FFT từ luồng phát (chế độ phổ)");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Không ở chế độ phổ, bỏ qua dừng FFT");
    }
}

// Xóa bộ đệm âm thanh
void Esp32Music::ClearAudioBuffer()
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    while (!audio_buffer_.empty())
    {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data)
        {
            heap_caps_free(chunk.data);
        }
    }

    buffer_size_ = 0;
    ESP_LOGI(TAG, "Đã xóa buffer âm thanh");
}

// Khởi tạo bộ giải mã MP3
bool Esp32Music::InitializeMp3Decoder()
{
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr)
    {
        ESP_LOGE(TAG, "Không thể khởi tạo bộ giải mã MP3");
        mp3_decoder_initialized_ = false;
        return false;
    }

    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "Khởi tạo bộ giải mã MP3 thành công");
    return true;
}

// Dọn dẹp bộ giải mã MP3
void Esp32Music::CleanupMp3Decoder()
{
    if (mp3_decoder_ != nullptr)
    {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "Đã dọn dẹp bộ giải mã MP3");
}

// Đặt lại tần số lấy mẫu về giá trị gốc
void Esp32Music::ResetSampleRate()
{
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 &&
        codec->output_sample_rate() != codec->original_output_sample_rate())
    {
        ESP_LOGI(TAG, "Đặt lại tần số lấy mẫu: từ %d Hz về giá trị gốc %d Hz",
                 codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1))
        { // -1 có nghĩa là đặt lại về giá trị ban đầu
            ESP_LOGI(TAG, "Đã đặt lại thành công tần số lấy mẫu về giá trị gốc: %d Hz", codec->output_sample_rate());
        }
        else
        {
            ESP_LOGW(TAG, "Không thể đặt lại tần số lấy mẫu về giá trị gốc");
        }
    }
}

// Bỏ qua thẻ ID3 ở đầu file MP3
size_t Esp32Music::SkipId3Tag(uint8_t *data, size_t size)
{
    if (!data || size < 10)
    {
        return 0;
    }

    // Kiểm tra header tag ID3v2 "ID3"
    if (memcmp(data, "ID3", 3) != 0)
    {
        return 0;
    }

    // Tính toán kích thước tag (định dạng số nguyên synchsafe)
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7) |
                        ((uint32_t)(data[9] & 0x7F));

    // Header ID3v2 (10 byte) + nội dung tag
    size_t total_skip = 10 + tag_size;

    // Đảm bảo không vượt quá kích thước dữ liệu có sẵn
    if (total_skip > size)
    {
        total_skip = size;
    }

    ESP_LOGI(TAG, "Tìm thấy thẻ ID3v2, bỏ qua %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// Tải lời bài hát
bool Esp32Music::DownloadLyrics(const std::string &lyric_url)
{
    ESP_LOGI(TAG, "Đang tải lời bài hát từ: %s", lyric_url.c_str());

    // Kiểm tra xem URL có trống không
    if (lyric_url.empty())
    {
        ESP_LOGE(TAG, "URL lời bài hát trống!");
        return false;
    }

    // Thêm logic thử lại
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5; // Cho phép tối đa 5 lần chuyển hướng

    while (retry_count < max_retries && !success && redirect_count < max_redirects)
    {
        if (retry_count > 0)
        {
            ESP_LOGI(TAG, "Thử lại tải lời bài hát (lần thử %d/%d)", retry_count + 1, max_retries);
            // Tạm dừng một chút trước khi thử lại
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Sử dụng HTTP client do Board cung cấp
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (!http)
        {
            ESP_LOGE(TAG, "Không thể tạo HTTP client để tải lời bài hát");
            retry_count++;
            continue;
        }

        // Đặt header request cơ bản
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");

        // Thêm header xác thực ESP32
        add_auth_headers(http.get());

        // Mở kết nối GET
        if (!http->Open("GET", current_url))
        {
            ESP_LOGE(TAG, "Không thể mở kết nối HTTP cho lời bài hát");
            // Xóa delete http; vì unique_ptr sẽ tự động quản lý bộ nhớ
            retry_count++;
            continue;
        }

        // Kiểm tra mã trạng thái HTTP
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Mã trạng thái HTTP tải lời bài hát: %d", status_code);

        // Xử lý chuyển hướng - Vì lớp Http không có phương thức GetHeader, chúng ta chỉ có thể phán đoán dựa trên mã trạng thái
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308)
        {
            // Vì không thể lấy header Location, chỉ có thể báo cáo chuyển hướng nhưng không thể tiếp tục
            ESP_LOGW(TAG, "Nhận được trạng thái chuyển hướng %d nhưng không thể theo chuyển hướng (không có phương thức GetHeader)", status_code);
            http->Close();
            retry_count++;
            continue;
        }

        // Mã trạng thái không thuộc series 200 được coi là lỗi
        if (status_code < 200 || status_code >= 300)
        {
            ESP_LOGE(TAG, "HTTP GET thất bại với mã trạng thái: %d", status_code);
            http->Close();
            retry_count++;
            continue;
        }

        // Đọc phản hồi
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;

        // Vì không thể lấy header Content-Length và Content-Type, chúng ta không biết kích thước và loại nội dung dự kiến
        ESP_LOGD(TAG, "Bắt đầu đọc nội dung lời bài hát");

        while (true)
        {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // Chú thích để giảm output log

            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;

                // In tiến độ tải xuống định kỳ - đổi sang cấp DEBUG để giảm output
                if (total_read % 4096 == 0)
                {
                    ESP_LOGD(TAG, "Đã tải xuống %d bytes", total_read);
                }
            }
            else if (bytes_read == 0)
            {
                // Kết thúc bình thường, không còn dữ liệu
                ESP_LOGD(TAG, "Tải lời bài hát hoàn tất, tổng bytes: %d", total_read);
                success = true;
                break;
            }
            else
            {
                // bytes_read < 0, có thể là một vấn đề đã biết của ESP-IDF
                // Nếu đã đọc được một số dữ liệu, thì coi như tải xuống thành công
                if (!lyric_content.empty())
                {
                    ESP_LOGW(TAG, "HTTP read trả về %d, nhưng chúng ta có dữ liệu (%d bytes), tiếp tục", bytes_read, lyric_content.length());
                    success = true;
                    break;
                }
                else
                {
                    ESP_LOGE(TAG, "Không thể đọc dữ liệu lời bài hát: mã lỗi %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }

        http->Close();

        if (read_error)
        {
            retry_count++;
            continue;
        }

        // Nếu đọc dữ liệu thành công, thoát khỏi vòng lặp thử lại
        if (success)
        {
            break;
        }
    }

    // Kiểm tra xem đã vượt quá số lần thử lại tối đa chưa
    if (retry_count >= max_retries)
    {
        ESP_LOGE(TAG, "Không thể tải lời bài hát sau %d lần thử", max_retries);
        return false;
    }

    // Ghi lại vài byte dữ liệu đầu tiên để giúp gỡ lỗi
    if (!lyric_content.empty())
    {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Xem trước nội dung lời bài hát (%d bytes): %s", lyric_content.length(), preview.c_str());
    }
    else
    {
        ESP_LOGE(TAG, "Tải lời bài hát thất bại hoặc lời bài hát trống");
        return false;
    }

    ESP_LOGI(TAG, "Tải lời bài hát thành công, kích thước: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// Phân tích lời bài hát
bool Esp32Music::ParseLyrics(const std::string &lyric_content)
{
    ESP_LOGI(TAG, "Đang phân tích nội dung lời bài hát");

    // Sử dụng khóa để bảo vệ quyền truy cập mảng lyrics_
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    lyrics_.clear();

    // Chia nhỏ nội dung lời bài hát theo dòng
    std::istringstream stream(lyric_content);
    std::string line;

    while (std::getline(stream, line))
    {
        // Xóa ký tự xuống dòng ở cuối dòng
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // Bỏ qua các dòng trống
        if (line.empty())
        {
            continue;
        }

        // Phân tích định dạng LRC: [mm:ss.xx]văn bản lời bài hát
        if (line.length() > 10 && line[0] == '[')
        {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos)
            {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);

                // Kiểm tra xem đó là thẻ siêu dữ liệu hay dấu thời gian
                // Thẻ siêu dữ liệu thường là [ti:Tiêu đề], [ar:Nghệ sĩ], [al:Album], v.v.
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos)
                {
                    std::string left_part = tag_or_time.substr(0, colon_pos);

                    // Kiểm tra xem bên trái dấu hai chấm có phải là thời gian (số) không
                    bool is_time_format = true;
                    for (char c : left_part)
                    {
                        if (!isdigit(c))
                        {
                            is_time_format = false;
                            break;
                        }
                    }

                    // Nếu không phải định dạng thời gian, bỏ qua dòng này (thẻ siêu dữ liệu)
                    if (!is_time_format)
                    {
                        // Ở đây có thể xử lý siêu dữ liệu, ví dụ như trích xuất thông tin tiêu đề, nghệ sĩ, v.v.
                        ESP_LOGD(TAG, "Bỏ qua thẻ siêu dữ liệu: [%s]", tag_or_time.c_str());
                        continue;
                    }

                    // Là định dạng thời gian, phân tích dấu thời gian
                    try
                    {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);

                        // Xử lý văn bản lời bài hát một cách an toàn, đảm bảo mã hóa UTF-8 chính xác
                        std::string safe_lyric_text;
                        if (!content.empty())
                        {
                            // Tạo bản sao an toàn và xác thực chuỗi
                            safe_lyric_text = content;
                            // Đảm bảo chuỗi kết thúc bằng null
                            safe_lyric_text.shrink_to_fit();
                        }

                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));

                        if (!safe_lyric_text.empty())
                        {
                            // Hạn chế độ dài output log, tránh vấn đề cắt ngắn ký tự tiếng Trung
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Lời bài hát đã phân tích: [%d ms] %s", timestamp_ms, log_text.c_str());
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Lời bài hát đã phân tích: [%d ms] (trống)", timestamp_ms);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        ESP_LOGW(TAG, "Phân tích thời gian thất bại: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }

    // Sắp xếp theo dấu thời gian
    std::sort(lyrics_.begin(), lyrics_.end());

    ESP_LOGI(TAG, "Đã phân tích %d dòng lời bài hát", lyrics_.size());
    return !lyrics_.empty();
}

// Thread hiển thị lời bài hát
void Esp32Music::LyricDisplayThread()
{
    ESP_LOGI(TAG, "Đã bắt đầu thread hiển thị lời bài hát");

    if (!DownloadLyrics(current_lyric_url_))
    {
        ESP_LOGE(TAG, "Không thể tải hoặc phân tích lời bài hát");
        is_lyric_running_ = false;
        return;
    }

    // Kiểm tra định kỳ có cần cập nhật hiển thị (tần suất có thể giảm)
    while (is_lyric_running_ && is_playing_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ESP_LOGI(TAG, "Thread hiển thị lời bài hát đã kết thúc");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms)
{
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    if (lyrics_.empty())
    {
        return;
    }

    // Tìm lời bài hát hiện tại cần hiển thị
    int new_lyric_index = -1;

    // Bắt đầu tìm kiếm từ chỉ mục lời bài hát hiện tại để nâng cao hiệu quả
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;

    // Tìm kiếm xuôi: tìm lời bài hát cuối cùng có dấu thời gian nhỏ hơn hoặc bằng thời gian hiện tại
    for (int i = start_index; i < (int)lyrics_.size(); i++)
    {
        if (lyrics_[i].first <= current_time_ms)
        {
            new_lyric_index = i;
        }
        else
        {
            break; // Dấu thời gian đã vượt quá thời gian hiện tại
        }
    }

    // Nếu không tìm thấy (có thể thời gian hiện tại sớm hơn cả câu lời đầu tiên), hiển thị trống
    if (new_lyric_index == -1)
    {
        new_lyric_index = -1;
    }

    // Nếu chỉ mục lời bài hát thay đổi, cập nhật hiển thị
    if (new_lyric_index != current_lyric_index_)
    {
        current_lyric_index_ = new_lyric_index;

        auto &board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display)
        {
            std::string lyric_text;

            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size())
            {
                lyric_text = lyrics_[current_lyric_index_].second;
            }

            // Hiển thị lời bài hát
            display->SetChatMessage("lyric", lyric_text.c_str());

            ESP_LOGD(TAG, "Cập nhật lời bài hát lúc %lldms: %s",
                     current_time_ms,
                     lyric_text.empty() ? "(không có lời)" : lyric_text.c_str());
        }
    }
}

// Xóa phương thức khởi tạo xác thực phức tạp, sử dụng hàm tĩnh đơn giản

// Xóa phương pháp lớp phức tạp, sử dụng hàm tĩnh đơn giản

/**
 * @brief Thêm header xác thực vào HTTP request
 * @param http_client Con trỏ HTTP client
 *
 * Các header xác thực được thêm vào bao gồm:
 * - X-MAC-Address: Địa chỉ MAC của thiết bị
 * - X-Chip-ID: ID chip của thiết bị
 * - X-Timestamp: Dấu thời gian hiện tại
 * - X-Dynamic-Key: Khóa được tạo động
 */
// Xóa phương thức AddAuthHeaders phức tạp, sử dụng hàm tĩnh đơn giản

// Xóa các phương thức xác minh và cấu hình xác thực phức tạp, sử dụng các hàm tĩnh đơn giản

// 显示模式控制方法实现
void Esp32Music::SetDisplayMode(DisplayMode mode)
{
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;

    ESP_LOGI(TAG, "Chế độ hiển thị đã chuyển từ %s sang %s",
             (old_mode == DISPLAY_MODE_SPECTRUM) ? "PHỔ" : "LỜI BÀI HÁT",
             (mode == DISPLAY_MODE_SPECTRUM) ? "PHỔ" : "LỜI BÀI HÁT");
}