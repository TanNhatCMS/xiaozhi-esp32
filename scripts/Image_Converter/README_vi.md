# Công cụ chuyển đổi ảnh LVGL

Thư mục này chứa hai script Python dùng để xử lý và chuyển đổi ảnh sang định dạng tương thích với LVGL.

## 1. LVGLImage (LVGLImage.py)

Script gốc được lấy từ [kho chính thức của LVGL](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py) và đảm nhận việc chuyển đổi định dạng ở mức dòng lệnh.

## 2. `lvgl_tools_gui.py`

Giao diện đồ họa gọi `LVGLImage.py` để chuyển đổi hàng loạt ảnh. Công cụ này rất hữu ích khi bạn muốn thay đổi bộ biểu cảm mặc định của Xiaozhi; hướng dẫn chi tiết có thể tham khảo [tại đây](https://www.bilibili.com/video/BV12FQkYeEJ3/).

### Tính năng

- Giao diện trực quan, thao tác nhanh chóng.
- Hỗ trợ xử lý hàng loạt tệp ảnh.
- Tự động nhận dạng định dạng ảnh và chọn cấu hình màu tối ưu.
- Hoạt động với nhiều độ phân giải khác nhau.

### Cách sử dụng

Tạo môi trường ảo:

```bash
python -m venv venv
# Kích hoạt trên macOS/Linux
source venv/bin/activate
# Kích hoạt trên Windows
venv\\Scripts\\activate
```

Cài đặt phụ thuộc:

```bash
pip install -r requirements.txt
```

Chạy công cụ GUI:

```bash
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate      # Windows
# Sau khi kích hoạt môi trường ảo
python lvgl_tools_gui.py
```