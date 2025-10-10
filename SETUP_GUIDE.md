# Hướng dẫn cài đặt firmware Xiaozhi ESP32

## Yêu cầu hệ thống

### 1. Cài đặt Python 3.12

#### Trên Windows:
```bash
# Tải Python 3.12 từ https://www.python.org/downloads/
# Hoặc sử dụng Chocolatey
choco install python --version=3.12.0

# Kiểm tra phiên bản
python --version
```

#### Trên macOS:
```bash
# Sử dụng Homebrew
brew install python@3.12

# Kiểm tra phiên bản
python3.12 --version
```

#### Trên Linux (Ubuntu/Debian):
```bash
# Thêm repository và cài đặt
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt update
sudo apt install python3.12 python3.12-venv python3.12-pip

# Kiểm tra phiên bản
python3.12 --version
```

### 2. Cài đặt ESP-IDF 5.5.1

Tham khảo tài liệu chính thức: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html

#### Cài đặt ESP-IDF:

**Bước 1: Tải ESP-IDF**
```bash
# Tạo thư mục cho ESP-IDF
mkdir -p ~/esp
cd ~/esp

# Clone ESP-IDF phiên bản 5.5.1
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.5.1
git submodule update --init --recursive
```

**Bước 2: Cài đặt các công cụ**
```bash
# Chạy script cài đặt
./install.sh esp32s3

# Thiết lập biến môi trường
. ./export.sh
```

**Bước 3: Thêm vào PATH (tùy chọn)**
```bash
# Thêm vào ~/.bashrc hoặc ~/.zshrc
echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.bashrc
source ~/.bashrc
```

## Cài đặt firmware Xiaozhi ESP32

### Chuẩn bị

1. **Kết nối ESP32-S3** với máy tính qua USB
2. **Xác định cổng COM** (Windows) hoặc **device path** (Linux/macOS):
   - Windows: `COM3`, `COM4`, ...
   - Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`, ...
   - macOS: `/dev/cu.usbserial-*`, `/dev/cu.usbmodem-*`

### Các lệnh cài đặt firmware

#### Bước 1: Thiết lập target
```bash
cd /path/to/xiaozhi-esp32-vi
idf.py set-target esp32s3
```

#### Bước 2: Cấu hình (tùy chọn)
```bash
# Nếu sử dụng file sdkconfig mẫu thì bỏ qua bước này
idf.py menuconfig
```

**Lưu ý:** Nếu bạn đã có file `sdkconfig` được cấu hình sẵn, có thể bỏ qua bước 1-2.

#### Bước 3: Build firmware
```bash
idf.py build
```

#### Bước 4: Tạo file firmware tổng hợp
```bash
idf.py merge-bin -o firmware.bin
```

#### Bước 5: Flash firmware vào ESP32-S3
```bash
# Thay thế PORT bằng cổng COM thực tế của bạn
esptool.py --chip esp32s3 -b 115200 write_flash --flash_freq 80m --flash_mode dio 0x0 build/firmware.bin
```

**Ví dụ với cổng cụ thể:**
```bash
# Windows
esptool.py --chip esp32s3 -p COM3 -b 115200 write_flash --flash_freq 80m --flash_mode dio 0x0 build/firmware.bin

# Linux
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 115200 write_flash --flash_freq 80m --flash_mode dio 0x0 build/firmware.bin

# macOS
esptool.py --chip esp32s3 -p /dev/cu.usbserial-0001 -b 115200 write_flash --flash_freq 80m --flash_mode dio 0x0 build/firmware.bin
```

## Kiểm tra kết quả

Sau khi flash thành công, bạn có thể:

1. **Kiểm tra log serial:**
```bash
idf.py monitor
```

2. **Hoặc sử dụng esptool để đọc thông tin:**
```bash
esptool.py --chip esp32s3 -p PORT flash_id
```

## Xử lý sự cố

### Lỗi thường gặp:

1. **"Permission denied" trên Linux/macOS:**
```bash
sudo chmod 666 /dev/ttyUSB0
# Hoặc thêm user vào group dialout
sudo usermod -a -G dialout $USER
```

2. **"Port not found":**
- Kiểm tra kết nối USB
- Cài đặt driver USB-to-Serial (Windows)
- Kiểm tra quyền truy cập cổng

3. **"Build failed":**
- Kiểm tra ESP-IDF đã được cài đặt đúng
- Chạy `get_idf` để thiết lập môi trường
- Kiểm tra Python version

4. **"Flash failed":**
- Nhấn và giữ nút BOOT trên ESP32-S3 khi flash
- Kiểm tra kết nối USB
- Thử giảm baudrate: `-b 460800`

## Lệnh nhanh

Để flash nhanh trong lần sau:
```bash
# Thiết lập môi trường
get_idf

# Vào thư mục project
cd /path/to/xiaozhi-esp32-vi

# Flash trực tiếp
idf.py -p PORT flash monitor
```

## Tài liệu tham khảo

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
