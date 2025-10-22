import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image
import os
import tempfile
import sys
from LVGLImage import LVGLImage, ColorFormat, CompressMethod

HELP_TEXT = """Hướng dẫn sử dụng công cụ chuyển đổi ảnh LVGL:

1. Thêm tệp: nhấn nút "Thêm tệp" để chọn ảnh cần chuyển đổi, hỗ trợ chọn nhiều tệp.

2. Gỡ tệp: tick vào hộp kiểm "[ ]" trước tên tệp (khi chọn sẽ thành "[√]") rồi nhấn "Gỡ tệp đã chọn".

3. Thiết lập độ phân giải: chọn kích thước mong muốn, ví dụ 128x128.
   Nên chọn phù hợp với độ phân giải màn hình thiết bị, quá lớn hoặc nhỏ sẽ ảnh hưởng hiển thị.

4. Định dạng màu: chọn "Tự nhận dạng" để dựa theo độ trong suốt, hoặc đặt thủ công.
   Trừ khi bạn hiểu rõ, nên để chế độ tự động để tránh các vấn đề ngoài ý muốn.

5. Phương thức nén: chọn NONE hoặc RLE.
   Nếu không chắc chắn, hãy giữ mặc định NONE (không nén).

6. Thư mục đầu ra: chọn nơi lưu tệp sau khi chuyển đổi.
   Mặc định là thư mục output cùng nơi đặt chương trình.

7. Chuyển đổi: nhấn "Chuyển toàn bộ" hoặc "Chuyển tệp đã chọn" để bắt đầu.
"""

class ImageConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Công cụ chuyển đổi ảnh LVGL")
        self.root.geometry("750x650")

        # Khởi tạo biến
        self.output_dir = tk.StringVar(value=os.path.abspath("output"))
        self.resolution = tk.StringVar(value="128x128")
        self.color_format = tk.StringVar(value="Tự nhận dạng")
        self.compress_method = tk.StringVar(value="NONE")

        # Tạo các thành phần giao diện
        self.create_widgets()
        self.redirect_output()

    def create_widgets(self):
        # Khung thiết lập tham số
        settings_frame = ttk.LabelFrame(self.root, text="Thiết lập chuyển đổi")
        settings_frame.grid(row=0, column=0, padx=10, pady=5, sticky="ew")

        # Thiết lập độ phân giải
        ttk.Label(settings_frame, text="Độ phân giải:").grid(row=0, column=0, padx=2)
        ttk.Combobox(settings_frame, textvariable=self.resolution, 
                    values=["512x512", "256x256", "128x128", "64x64", "32x32"], width=8).grid(row=0, column=1, padx=2)

        # Định dạng màu
        ttk.Label(settings_frame, text="Định dạng màu:").grid(row=0, column=2, padx=2)
        ttk.Combobox(settings_frame, textvariable=self.color_format,
                    values=["Tự nhận dạng", "RGB565", "RGB565A8"], width=10).grid(row=0, column=3, padx=2)

        # Phương thức nén
        ttk.Label(settings_frame, text="Phương thức nén:").grid(row=0, column=4, padx=2)
        ttk.Combobox(settings_frame, textvariable=self.compress_method,
                    values=["NONE", "RLE"], width=8).grid(row=0, column=5, padx=2)

        # Khung chọn tệp
        file_frame = ttk.LabelFrame(self.root, text="Chọn tệp")
        file_frame.grid(row=1, column=0, padx=10, pady=5, sticky="nsew")

        # Khung nút thao tác tệp
        btn_frame = ttk.Frame(file_frame)
        btn_frame.pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame, text="Thêm tệp", command=self.select_files).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame, text="Gỡ tệp đã chọn", command=self.remove_selected).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame, text="Xóa danh sách", command=self.clear_files).pack(side=tk.LEFT, padx=2)

        # Danh sách tệp (Treeview)
        self.tree = ttk.Treeview(file_frame, columns=("selected", "filename"), 
                               show="headings", height=10)
        self.tree.heading("selected", text="Chọn", anchor=tk.W)
        self.tree.heading("filename", text="Tên tệp", anchor=tk.W)
        self.tree.column("selected", width=60, anchor=tk.W)
        self.tree.column("filename", width=600, anchor=tk.W)
        self.tree.pack(fill=tk.BOTH, expand=True)
        self.tree.bind("<ButtonRelease-1>", self.on_tree_click)

        # Thư mục đầu ra
        output_frame = ttk.LabelFrame(self.root, text="Thư mục đầu ra")
        output_frame.grid(row=2, column=0, padx=10, pady=5, sticky="ew")
        ttk.Entry(output_frame, textvariable=self.output_dir, width=60).pack(side=tk.LEFT, padx=5)
        ttk.Button(output_frame, text="Duyệt", command=self.select_output_dir).pack(side=tk.RIGHT, padx=5)

        # Nút chuyển đổi và nút trợ giúp
        convert_frame = ttk.Frame(self.root)
        convert_frame.grid(row=3, column=0, padx=10, pady=10)
        ttk.Button(convert_frame, text="Chuyển đổi tất cả tệp", command=lambda: self.start_conversion(True)).pack(side=tk.LEFT, padx=5)
        ttk.Button(convert_frame, text="Chuyển đổi tệp đã chọn", command=lambda: self.start_conversion(False)).pack(side=tk.LEFT, padx=5)
        ttk.Button(convert_frame, text="Trợ giúp", command=self.show_help).pack(side=tk.RIGHT, padx=5)

        # Khu vực nhật ký (thêm nút xóa)
        log_frame = ttk.LabelFrame(self.root, text="Nhật ký")
        log_frame.grid(row=4, column=0, padx=10, pady=5, sticky="nsew")

        # Khung nút cho nhật ký
        log_btn_frame = ttk.Frame(log_frame)
        log_btn_frame.pack(fill=tk.X, side=tk.BOTTOM)

        # Nút xóa nhật ký
        ttk.Button(log_btn_frame, text="Xóa nhật ký", command=self.clear_log).pack(side=tk.RIGHT, padx=5, pady=2)
        
        self.log_text = tk.Text(log_frame, height=15)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Bố cục
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)
        self.root.rowconfigure(4, weight=1)

    def clear_log(self):
        """Xóa nội dung nhật ký"""
        self.log_text.delete(1.0, tk.END)

    def show_help(self):
        messagebox.showinfo("Trợ giúp", HELP_TEXT)

    def redirect_output(self):
        class StdoutRedirector:
            def __init__(self, text_widget):
                self.text_widget = text_widget
                self.original_stdout = sys.stdout

            def write(self, message):
                self.text_widget.insert(tk.END, message)
                self.text_widget.see(tk.END)
                self.original_stdout.write(message)

            def flush(self):
                self.original_stdout.flush()

        sys.stdout = StdoutRedirector(self.log_text)

    def on_tree_click(self, event):
        region = self.tree.identify("region", event.x, event.y)
        if region == "cell":
            col = self.tree.identify_column(event.x)
            item = self.tree.identify_row(event.y)
            if col == "#1":  # Nhấn vào cột chọn
                current_val = self.tree.item(item, "values")[0]
                new_val = "[√]" if current_val == "[ ]" else "[ ]"
                self.tree.item(item, values=(new_val, self.tree.item(item, "values")[1]))

    def select_output_dir(self):
        path = filedialog.askdirectory()
        if path:
            self.output_dir.set(path)

    def select_files(self):
        files = filedialog.askopenfilenames(filetypes=[("Hình ảnh", "*.png;*.jpg;*.jpeg;*.bmp;*.gif")])
        for f in files:
            self.tree.insert("", tk.END, values=("[ ]", os.path.basename(f)), tags=(f,))

    def remove_selected(self):
        to_remove = []
        for item in self.tree.get_children():
            if self.tree.item(item, "values")[0] == "[√]":
                to_remove.append(item)
        for item in reversed(to_remove):
            self.tree.delete(item)

    def clear_files(self):
        for item in self.tree.get_children():
            self.tree.delete(item)

    def start_conversion(self, convert_all):
        input_files = [
            self.tree.item(item, "tags")[0]
            for item in self.tree.get_children()
            if convert_all or self.tree.item(item, "values")[0] == "[√]"
        ]
        
        if not input_files:
            msg = "Không tìm thấy tệp nào để chuyển đổi" if convert_all else "Không có tệp nào được chọn"
            messagebox.showwarning("Cảnh báo", msg)
            return
        
        os.makedirs(self.output_dir.get(), exist_ok=True)

        # Phân tích tham số chuyển đổi
        width, height = map(int, self.resolution.get().split('x'))
        compress = CompressMethod.RLE if self.compress_method.get() == "RLE" else CompressMethod.NONE

        # Thực hiện chuyển đổi
        self.convert_images(input_files, width, height, compress)

    def convert_images(self, input_files, width, height, compress):
        success_count = 0
        total_files = len(input_files)
        
        for idx, file_path in enumerate(input_files):
            try:
                print(f"正在处理: {os.path.basename(file_path)}")
                
                with Image.open(file_path) as img:
                    # Điều chỉnh kích thước hình ảnh
                    img = img.resize((width, height), Image.Resampling.LANCZOS)

                    # Xử lý định dạng màu
                    color_format_str = self.color_format.get()
                    if color_format_str == "Tự động nhận diện":
                        # Kiểm tra kênh alpha
                        has_alpha = img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info)
                        if has_alpha:
                            img = img.convert('RGBA')
                            cf = ColorFormat.RGB565A8
                        else:
                            img = img.convert('RGB')
                            cf = ColorFormat.RGB565
                    else:
                        if color_format_str == "RGB565A8":
                            img = img.convert('RGBA')
                            cf = ColorFormat.RGB565A8
                        else:
                            img = img.convert('RGB')
                            cf = ColorFormat.RGB565

                    # Lưu hình ảnh đã điều chỉnh
                    base_name = os.path.splitext(os.path.basename(file_path))[0]
                    output_image_path = os.path.join(self.output_dir.get(), f"{base_name}_{width}x{height}.png")
                    img.save(output_image_path, 'PNG')

                    # Tạo tệp tạm thời
                    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmpfile:
                        temp_path = tmpfile.name
                        img.save(temp_path, 'PNG')

                    # Chuyển đổi thành mảng C LVGL
                    lvgl_img = LVGLImage().from_png(temp_path, cf=cf)
                    output_c_path = os.path.join(self.output_dir.get(), f"{base_name}.c")
                    lvgl_img.to_c_array(output_c_path, compress=compress)

                    success_count += 1
                    os.unlink(temp_path)
                    print(f"Chuyển đổi thành công: {base_name}.c\n")

            except Exception as e:
                print(f"Chuyển đổi thất bại: {str(e)}\n")

        print(f"Chuyển đổi hoàn tất! Thành công {success_count}/{total_files} tệp\n")

if __name__ == "__main__":
    root = tk.Tk()
    app = ImageConverterApp(root)
    root.mainloop()
