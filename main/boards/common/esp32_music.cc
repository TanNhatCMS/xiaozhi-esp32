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

        ESP_LOGI(TAG, "Added auth headers - MAC: %s, ChipID: %s, Timestamp: %lld",
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
    ESP_LOGI(TAG, "Music player initialized with default spectrum display mode");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music()
{
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");

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
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
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
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
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
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }

        if (download_thread_.joinable())
        {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }

    // Chờ thread phát nhạc kết thúc, đặt timeout 3 giây
    if (play_thread_.joinable())
    {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
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
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
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
        ESP_LOGI(TAG, "Playback thread finished");
    }

    // Chờ thread lời bài hát kết thúc
    if (lyric_thread_.joinable())
    {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }

    // Dọn dẹp bộ đệm và bộ giải mã MP3
    ClearAudioBuffer();
    CleanupMp3Decoder();

    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32Music::Download(const std::string &song_name, const std::string &artist_name)
{
    ESP_LOGI(TAG, "Starting to get music details for: %s", song_name.c_str());

    // Xóa dữ liệu tải xuống trước đó
    last_downloaded_data_.clear();

    // Lưu tên bài hát để hiển thị sau
    current_song_name_ = song_name;

    // Bước 1: Gọi API stream_pcm để lấy thông tin âm thanh
    std::string base_url = "http://www.xiaozhishop.xyz:5005";
    std::string full_url = base_url + "/stream_pcm?song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);

    ESP_LOGI(TAG, "Request URL: %s", full_url.c_str());

    // Sử dụng HTTP client do Board cung cấp
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    // Đặt header cơ bản cho request
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");

    // Thêm header xác thực ESP32
    add_auth_headers(http.get());

    // Mở kết nối GET
    if (!http->Open("GET", full_url))
    {
        ESP_LOGE(TAG, "Failed to connect to music API");
        return false;
    }

    // Kiểm tra mã trạng thái phản hồi
    int status_code = http->GetStatusCode();
    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        return false;
    }

    // Đọc dữ liệu phản hồi
    last_downloaded_data_ = http->ReadAll();
    http->Close();

    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Complete music details response: %s", last_downloaded_data_.c_str());

    // Kiểm tra phản hồi xác thực đơn giản (tùy chọn)
    if (last_downloaded_data_.find("ESP32动态密钥验证失败") != std::string::npos)
    {
        ESP_LOGE(TAG, "Authentication failed for song: %s", song_name.c_str());
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

            if (cJSON_IsString(artist))
            {
                ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
            }
            if (cJSON_IsString(title))
            {
                ESP_LOGI(TAG, "Title: %s", title->valuestring);
            }

            // Kiểm tra xem audio_url có hợp lệ không
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0)
            {
                ESP_LOGI(TAG, "Audio URL path: %s", audio_url->valuestring);

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

                ESP_LOGI(TAG, "Starting streaming playback for: %s", song_name.c_str());
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
                        ESP_LOGI(TAG, "Loading lyrics for: %s (lyrics display mode)", song_name.c_str());

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
                        ESP_LOGI(TAG, "Lyric URL found but spectrum display mode is active, skipping lyrics");
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
                ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
                ESP_LOGE(TAG, "Failed to find music: Không tìm thấy bài hát '%s'", song_name.c_str());
                cJSON_Delete(response_json);
                return false;
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to parse JSON response");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Empty response from music API");
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
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }

    ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());

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
    cfg.stack_size = 8192; // Kích thước stack 8KB
    cfg.prio = 5;          // Ưu tiên trung bình
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);

    // Bắt đầu thread tải xuống
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);

    // Bắt đầu thread phát nhạc (sẽ chờ bộ đệm có đủ dữ liệu)
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);

    ESP_LOGI(TAG, "Streaming threads started successfully");

    return true;
}

// Dừng phát streaming
bool Esp32Music::StopStreaming()
{
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d",
             is_downloading_.load(), is_playing_.load());

    // Đặt lại tần số lấy mẫu về giá trị gốc
    ResetSampleRate();

    // Kiểm tra xem có streaming đang chạy không
    if (!is_playing_ && !is_downloading_)
    {
        ESP_LOGW(TAG, "No streaming in progress");
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
        ESP_LOGI(TAG, "Cleared song name display");
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
        ESP_LOGI(TAG, "Download thread joined in StopStreaming");
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
                ESP_LOGW(TAG, "Play thread join timeout, detaching thread");
                play_thread_.detach();
            }
            else
            {
                play_thread_.join();
                ESP_LOGI(TAG, "Play thread joined in StopStreaming");
            }
        }
    }

    // Sau khi thread kết thúc hoàn toàn, chỉ dừng hiển thị FFT trong chế độ phổ
    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM)
    {
        display->stopFft();
        ESP_LOGI(TAG, "Stopped FFT display in StopStreaming (spectrum mode)");
    }
    else if (display)
    {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop in StopStreaming");
    }

    ESP_LOGI(TAG, "Music streaming stop signal sent");
    return true;
}

// Tải xuống dữ liệu âm thanh streaming
void Esp32Music::DownloadAudioStream(const std::string &music_url)
{
    ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str());

    // Xác thực tính hợp lệ của URL
    if (music_url.empty() || music_url.find("http") != 0)
    {
        ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    // Đặt header cơ bản cho request
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-"); // Hỗ trợ resumable download

    // Thêm header xác thực ESP32
    add_auth_headers(http.get());

    if (!http->Open("GET", music_url))
    {
        ESP_LOGE(TAG, "Failed to connect to music stream URL");
        is_downloading_ = false;
        return;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206)
    { // 206 cho nội dung một phần
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }

    ESP_LOGI(TAG, "Started downloading audio stream, status: %d", status_code);

    // Đọc dữ liệu âm thanh theo khối
    const size_t chunk_size = 4096; // 4KB mỗi khối
    char buffer[chunk_size];
    size_t total_downloaded = 0;

    while (is_downloading_ && is_playing_)
    {
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read < 0)
        {
            ESP_LOGE(TAG, "Failed to read audio data: error code %d", bytes_read);
            break;
        }
        if (bytes_read == 0)
        {
            ESP_LOGI(TAG, "Audio stream download completed, total: %d bytes", total_downloaded);
            break;
        }

        // 打印数据块信息
        // ESP_LOGI(TAG, "Downloaded chunk: %d bytes at offset %d", bytes_read, total_downloaded);

        // 安全地打印数据块的十六进制内容（前16字节）
        if (bytes_read >= 16)
        {
            // ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...",
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        }
        else
        {
            ESP_LOGI(TAG, "Data chunk too small: %d bytes", bytes_read);
        }

        // 尝试检测文件格式（检查文件头）
        if (total_downloaded == 0 && bytes_read >= 4)
        {
            if (memcmp(buffer, "ID3", 3) == 0)
            {
                ESP_LOGI(TAG, "Detected MP3 file with ID3 tag");
            }
            else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0)
            {
                ESP_LOGI(TAG, "Detected MP3 file header");
            }
            else if (memcmp(buffer, "RIFF", 4) == 0)
            {
                ESP_LOGI(TAG, "Detected WAV file");
            }
            else if (memcmp(buffer, "fLaC", 4) == 0)
            {
                ESP_LOGI(TAG, "Detected FLAC file");
            }
            else if (memcmp(buffer, "OggS", 4) == 0)
            {
                ESP_LOGI(TAG, "Detected OGG file");
            }
            else
            {
                ESP_LOGI(TAG, "Unknown audio format, first 4 bytes: %02X %02X %02X %02X",
                         (unsigned char)buffer[0], (unsigned char)buffer[1],
                         (unsigned char)buffer[2], (unsigned char)buffer[3]);
            }
        }

        // 创建音频数据块
        uint8_t *chunk_data = (uint8_t *)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);

        // 等待缓冲区有空间
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this]
                            { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });

            if (is_downloading_)
            {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;

                // 通知播放线程有新数据
                buffer_cv_.notify_one();

                if (total_downloaded % (256 * 1024) == 0)
                { // 每256KB打印一次进度
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            }
            else
            {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }

    http->Close();
    is_downloading_ = false;

    // 通知播放线程下载完成
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// Phát dữ liệu âm thanh streaming
void Esp32Music::PlayAudioStream()
{
    ESP_LOGI(TAG, "Starting audio stream playback");

    // Khởi tạo các biến theo dõi thời gian
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;

    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !codec->output_enabled())
    {
        ESP_LOGE(TAG, "Audio codec not available or not enabled");
        is_playing_ = false;
        return;
    }

    if (!mp3_decoder_initialized_)
    {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        is_playing_ = false;
        return;
    }

    // Chờ bộ đệm có đủ dữ liệu để bắt đầu phát
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this]
                        { return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); });
    }

    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);

    size_t total_played = 0;
    uint8_t *mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t *read_ptr = nullptr;

    // Cấp phát bộ đệm đầu vào MP3
    mp3_input_buffer = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
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
                ESP_LOGI(TAG, "Device is in speaking state, switching to listening state for music playback");
            }
            if (current_state == kDeviceStateListening)
            {
                ESP_LOGI(TAG, "Device is in listening state, switching to idle state for music playback");
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
                std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
                display->SetMusicInfo(formatted_song_name.c_str());
                ESP_LOGI(TAG, "Displaying song name: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }

            // Khởi động chức năng hiển thị tương ứng theo chế độ hiển thị
            if (display)
            {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM)
                {
                    display->start();
                    ESP_LOGI(TAG, "Display start() called for spectrum visualization");
                }
                else
                {
                    ESP_LOGI(TAG, "Lyrics display mode active, FFT visualization disabled");
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
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
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
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
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
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
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
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping",
                         mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }

            // Tính thời lượng frame hiện tại (millisecond)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) /
                                    (mp3_frame_info_.samprate * mp3_frame_info_.nChans);

            // Cập nhật thời gian phát hiện tại
            current_play_time_ms_ += frame_duration_ms;

            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d",
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

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples",
                             stereo_samples, mono_samples);
                }
                else if (mp3_frame_info_.nChans == 1)
                {
                    // Đã là mono, không cần chuyển đổi
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                }
                else
                {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono",
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

                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application",
                         final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);

                // Gửi đến hàng đợi giải mã âm thanh của Application
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;

                // In tiến độ phát
                if (total_played % (128 * 1024) == 0)
                {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
        }
        else
        {
            // Giải mã thất bại
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);

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
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played);
    ESP_LOGI(TAG, "Performing basic cleanup from play thread");

    // Đặt cờ dừng phát
    is_playing_ = false;

    // Chỉ dừng hiển thị FFT trong chế độ hiển thị phổ
    if (display_mode_ == DISPLAY_MODE_SPECTRUM)
    {
        auto &board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display)
        {
            display->stopFft();
            ESP_LOGI(TAG, "Stopped FFT display from play thread (spectrum mode)");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop");
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
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// Khởi tạo bộ giải mã MP3
bool Esp32Music::InitializeMp3Decoder()
{
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }

    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
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
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// Đặt lại tần số lấy mẫu về giá trị gốc
void Esp32Music::ResetSampleRate()
{
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 &&
        codec->output_sample_rate() != codec->original_output_sample_rate())
    {
        ESP_LOGI(TAG, "重置采样率：从 %d Hz 重置到原始值 %d Hz",
                 codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1))
        { // -1 表示重置到原始值
            ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
        }
        else
        {
            ESP_LOGW(TAG, "无法重置采样率到原始值");
        }
    }
}

// 跳过MP3文件开头的ID3标签
size_t Esp32Music::SkipId3Tag(uint8_t *data, size_t size)
{
    if (!data || size < 10)
    {
        return 0;
    }

    // 检查ID3v2标签头 "ID3"
    if (memcmp(data, "ID3", 3) != 0)
    {
        return 0;
    }

    // 计算标签大小（synchsafe integer格式）
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7) |
                        ((uint32_t)(data[9] & 0x7F));

    // ID3v2头部(10字节) + 标签内容
    size_t total_skip = 10 + tag_size;

    // 确保不超过可用数据大小
    if (total_skip > size)
    {
        total_skip = size;
    }

    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// 下载歌词
bool Esp32Music::DownloadLyrics(const std::string &lyric_url)
{
    ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str());

    // 检查URL是否为空
    if (lyric_url.empty())
    {
        ESP_LOGE(TAG, "Lyric URL is empty!");
        return false;
    }

    // 添加重试逻辑
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5; // 最多允许5次重定向

    while (retry_count < max_retries && !success && redirect_count < max_redirects)
    {
        if (retry_count > 0)
        {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
            // 重试前暂停一下
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // 使用Board提供的HTTP客户端
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (!http)
        {
            ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }

        // 设置基本请求头
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");

        // 添加ESP32认证头
        add_auth_headers(http.get());

        // 打开GET连接
        ESP_LOGI(TAG, "小智开源音乐固件qq交流群:826072986");
        if (!http->Open("GET", current_url))
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics");
            // 移除delete http; 因为unique_ptr会自动管理内存
            retry_count++;
            continue;
        }

        // 检查HTTP状态码
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);

        // 处理重定向 - 由于Http类没有GetHeader方法，我们只能根据状态码判断
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308)
        {
            // 由于无法获取Location头，只能报告重定向但无法继续
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            retry_count++;
            continue;
        }

        // 非200系列状态码视为错误
        if (status_code < 200 || status_code >= 300)
        {
            ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
            http->Close();
            retry_count++;
            continue;
        }

        // 读取响应
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;

        // 由于无法获取Content-Length和Content-Type头，我们不知道预期大小和内容类型
        ESP_LOGD(TAG, "Starting to read lyric content");

        while (true)
        {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // 注释掉以减少日志输出

            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;

                // 定期打印下载进度 - 改为DEBUG级别减少输出
                if (total_read % 4096 == 0)
                {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            }
            else if (bytes_read == 0)
            {
                // 正常结束，没有更多数据
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            }
            else
            {
                // bytes_read < 0，可能是ESP-IDF的已知问题
                // 如果已经读取到了一些数据，则认为下载成功
                if (!lyric_content.empty())
                {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
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

        // 如果成功读取数据，跳出重试循环
        if (success)
        {
            break;
        }
    }

    // 检查是否超过了最大重试次数
    if (retry_count >= max_retries)
    {
        ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
        return false;
    }

    // 记录前几个字节的数据，帮助调试
    if (!lyric_content.empty())
    {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    }
    else
    {
        ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
        return false;
    }

    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// 解析歌词
bool Esp32Music::ParseLyrics(const std::string &lyric_content)
{
    ESP_LOGI(TAG, "Parsing lyrics content");

    // 使用锁保护lyrics_数组访问
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    lyrics_.clear();

    // 按行分割歌词内容
    std::istringstream stream(lyric_content);
    std::string line;

    while (std::getline(stream, line))
    {
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // 跳过空行
        if (line.empty())
        {
            continue;
        }

        // 解析LRC格式: [mm:ss.xx]歌词文本
        if (line.length() > 10 && line[0] == '[')
        {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos)
            {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);

                // 检查是否是元数据标签而不是时间戳
                // 元数据标签通常是 [ti:标题], [ar:艺术家], [al:专辑] 等
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos)
                {
                    std::string left_part = tag_or_time.substr(0, colon_pos);

                    // 检查冒号左边是否是时间（数字）
                    bool is_time_format = true;
                    for (char c : left_part)
                    {
                        if (!isdigit(c))
                        {
                            is_time_format = false;
                            break;
                        }
                    }

                    // 如果不是时间格式，跳过这一行（元数据标签）
                    if (!is_time_format)
                    {
                        // 可以在这里处理元数据，例如提取标题、艺术家等信息
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }

                    // 是时间格式，解析时间戳
                    try
                    {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);

                        // 安全处理歌词文本，确保UTF-8编码正确
                        std::string safe_lyric_text;
                        if (!content.empty())
                        {
                            // 创建安全副本并验证字符串
                            safe_lyric_text = content;
                            // 确保字符串以null结尾
                            safe_lyric_text.shrink_to_fit();
                        }

                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));

                        if (!safe_lyric_text.empty())
                        {
                            // 限制日志输出长度，避免中文字符截断问题
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }

    // 按时间戳排序
    std::sort(lyrics_.begin(), lyrics_.end());

    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// 歌词显示线程
void Esp32Music::LyricDisplayThread()
{
    ESP_LOGI(TAG, "Lyric display thread started");

    if (!DownloadLyrics(current_lyric_url_))
    {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }

    // 定期检查是否需要更新显示(频率可以降低)
    while (is_lyric_running_ && is_playing_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms)
{
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    if (lyrics_.empty())
    {
        return;
    }

    // 查找当前应该显示的歌词
    int new_lyric_index = -1;

    // 从当前歌词索引开始查找，提高效率
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;

    // 正向查找：找到最后一个时间戳小于等于当前时间的歌词
    for (int i = start_index; i < (int)lyrics_.size(); i++)
    {
        if (lyrics_[i].first <= current_time_ms)
        {
            new_lyric_index = i;
        }
        else
        {
            break; // 时间戳已超过当前时间
        }
    }

    // 如果没有找到(可能当前时间比第一句歌词还早)，显示空
    if (new_lyric_index == -1)
    {
        new_lyric_index = -1;
    }

    // 如果歌词索引发生变化，更新显示
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

            // 显示歌词
            display->SetChatMessage("lyric", lyric_text.c_str());

            ESP_LOGD(TAG, "Lyric update at %lldms: %s",
                     current_time_ms,
                     lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

// 删除复杂的认证初始化方法，使用简单的静态函数

// 删除复杂的类方法，使用简单的静态函数

/**
 * @brief 添加认证头到HTTP请求
 * @param http_client HTTP客户端指针
 *
 * 添加的认证头包括：
 * - X-MAC-Address: 设备MAC地址
 * - X-Chip-ID: 设备芯片ID
 * - X-Timestamp: 当前时间戳
 * - X-Dynamic-Key: 动态生成的密钥
 */
// 删除复杂的AddAuthHeaders方法，使用简单的静态函数

// 删除复杂的认证验证和配置方法，使用简单的静态函数

// 显示模式控制方法实现
void Esp32Music::SetDisplayMode(DisplayMode mode)
{
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;

    ESP_LOGI(TAG, "Display mode changed from %s to %s",
             (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS",
             (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS");
}