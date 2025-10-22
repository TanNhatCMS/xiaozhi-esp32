## Kiến trúc dịch vụ âm thanh

Dịch vụ âm thanh là thành phần lõi chịu trách nhiệm quản lý toàn bộ chức năng liên quan đến âm thanh, bao gồm thu âm từ micro, xử lý, mã hóa/giải mã và phát lại qua loa. Hệ thống được thiết kế dạng mô-đun, các tác vụ chính chạy trong những task FreeRTOS chuyên biệt để đảm bảo hiệu năng thời gian thực.

## Thành phần chính

- **`AudioService`**: Bộ điều phối trung tâm, khởi tạo và quản lý mọi thành phần âm thanh khác, các task và hàng đợi dữ liệu.
- **`AudioCodec`**: Lớp trừu tượng phần cứng (HAL) cho chip codec âm thanh vật lý, phụ trách giao tiếp I2S thô cho cả thu và phát âm thanh.
- **`AudioProcessor`**: Xử lý âm thanh thời gian thực từ luồng micro, thường bao gồm khử vọng (AEC), khử nhiễu và phát hiện hoạt động giọng nói (VAD). `AfeAudioProcessor` là hiện thực mặc định dựa trên Audio Front-End của ESP-ADF.
- **`WakeWord`**: Nhận diện từ khóa kích hoạt (ví dụ “你好，小智”, “Hi, ESP”) trong luồng âm thanh. Mô-đun này chạy độc lập với bộ xử lý chính cho tới khi phát hiện được từ kích hoạt.
- **`OpusEncoderWrapper` / `OpusDecoderWrapper`**: Quản lý việc mã hóa PCM sang định dạng Opus và giải mã ngược lại. Opus được lựa chọn nhờ khả năng nén cao và độ trễ thấp, phù hợp cho truyền âm thanh.
- **`OpusResampler`**: Tiện ích chuyển đổi luồng âm thanh giữa các tần số lấy mẫu (ví dụ từ tần số gốc của codec về 16 kHz phục vụ xử lý).

## Mô hình đa luồng

Dịch vụ vận hành đồng thời ba task chính để xử lý các giai đoạn khác nhau trong pipeline âm thanh:

1. **`AudioInputTask`**: Chỉ phụ trách đọc dữ liệu PCM thô từ `AudioCodec`, sau đó chuyển tiếp cho `WakeWord` hoặc `AudioProcessor` tùy trạng thái hiện tại.
2. **`AudioOutputTask`**: Chịu trách nhiệm phát lại. Task này lấy dữ liệu PCM đã giải mã từ `audio_playback_queue_` và gửi vào `AudioCodec` để phát ra loa.
3. **`OpusCodecTask`**: Task công nhân xử lý cả mã hóa lẫn giải mã. Nó lấy dữ liệu PCM từ `audio_encode_queue_`, mã hóa thành gói Opus rồi đưa vào `audio_send_queue_`. Đồng thời, task cũng lấy gói Opus từ `audio_decode_queue_`, giải mã thành PCM và đặt kết quả vào `audio_playback_queue_`.

## Luồng dữ liệu

Có hai luồng chính: đầu vào (uplink) và đầu ra (downlink).

### 1. Luồng âm thanh đầu vào (Uplink)

Luồng này thu âm từ micro, xử lý, mã hóa và chuẩn bị gửi tới máy chủ.

- `AudioInputTask` liên tục đọc dữ liệu PCM thô từ `AudioCodec`.
- Dữ liệu được đưa vào `AudioProcessor` để làm sạch (AEC, VAD).
- PCM sau xử lý được đẩy vào `audio_encode_queue_`.
- `OpusCodecTask` lấy dữ liệu PCM, mã hóa thành gói Opus và đẩy vào `audio_send_queue_`.
- Ứng dụng sẽ lấy các gói Opus này để gửi lên mạng.

### 2. Luồng âm thanh đầu ra (Downlink)

Luồng này nhận âm thanh đã mã hóa, giải mã và phát ra loa.

- Ứng dụng nhận gói Opus từ mạng và đưa vào `audio_decode_queue_`.
- `OpusCodecTask` lấy gói, giải mã thành PCM và đưa vào `audio_playback_queue_`.
- `AudioOutputTask` lấy PCM từ hàng đợi và gửi tới `AudioCodec` để phát ra loa.

## Quản lý nguồn

Để tiết kiệm năng lượng, các kênh vào (ADC) và ra (DAC) của codec sẽ tự động tắt sau một khoảng không hoạt động (`AUDIO_POWER_TIMEOUT_MS`). Bộ hẹn giờ (`audio_power_timer_`) định kỳ kiểm tra hoạt động và quản lý trạng thái nguồn. Các kênh sẽ được bật lại khi cần thu hoặc phát âm thanh mới.