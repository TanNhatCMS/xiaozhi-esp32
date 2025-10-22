"""
Bộ giải điều chế AFSK thời gian thực - dựa trên thuật toán Goertzel
"""

import numpy as np
from collections import deque


class TraceGoertzel:
    """Triển khai thuật toán Goertzel thời gian thực"""
    
    def __init__(self, freq: float, n: int):
        """
        Khởi tạo thuật toán Goertzel

        Args:
            freq: Tần số chuẩn hóa (tần số mục tiêu/tần số lấy mẫu)
            n: Kích thước cửa sổ
        """
        self.freq = freq
        self.n = n

        # Tính toán hệ số trước - nhất quán với mã tham khảo
        self.k = int(freq * n)
        self.w = 2.0 * np.pi * freq
        self.cw = np.cos(self.w)
        self.sw = np.sin(self.w)
        self.c = 2.0 * self.cw

        # Khởi tạo biến trạng thái - sử dụng deque để lưu trữ hai giá trị gần nhất
        self.zs = deque([0.0, 0.0], maxlen=2)
    
    def reset(self):
        """Đặt lại trạng thái thuật toán"""
        self.zs.clear()
        self.zs.extend([0.0, 0.0])
    
    def __call__(self, xs):
        """
        Xử lý một tập hợp các điểm mẫu - giao diện nhất quán với mã tham khảo
        
        Args:
            xs: Chuỗi điểm mẫu

        Returns:
            Amplitude được tính toán
        """
        self.reset()
        for x in xs:
            z1, z2 = self.zs[-1], self.zs[-2]  # Z[-1], Z[-2]
            z0 = x + self.c * z1 - z2  # S[n] = x[n] + C * S[n-1] - S[n-2]
            self.zs.append(float(z0))  # Cập nhật chuỗi
        return self.amp
    
    @property
    def amp(self) -> float:
        """Tính toán biên độ hiện tại - nhất quán với mã tham khảo"""
        z1, z2 = self.zs[-1], self.zs[-2]
        ip = self.cw * z1 - z2
        qp = self.sw * z1
        return np.sqrt(ip**2 + qp**2) / (self.n / 2.0)


class PairGoertzel:
    """Bộ giải điều chế Goertzel kép"""
    
    def __init__(self, f_sample: int, f_space: int, f_mark: int, 
                 bit_rate: int, win_size: int):
        """
        Khởi tạo bộ giải điều chế kép
        
        Args:
            f_sample: Tần số lấy mẫu
            f_space: Tần số Space (thường tương ứng với 0)
            f_mark: Tần số Mark (thường tương ứng với 1)
            bit_rate: Tốc độ bit
            win_size: Kích thước cửa sổ Goertzel
        """
        assert f_sample % bit_rate == 0, "Tần số lấy mẫu phải là bội số của tốc độ bit"

        self.Fs = f_sample
        self.F0 = f_space
        self.F1 = f_mark
        self.bit_rate = bit_rate
        self.n_per_bit = int(f_sample // bit_rate)  # Số điểm mẫu mỗi bit

        # Tính toán tần số chuẩn hóa
        f1 = f_mark / f_sample
        f0 = f_space / f_sample

        # Khởi tạo thuật toán Goertzel
        self.g0 = TraceGoertzel(freq=f0, n=win_size)
        self.g1 = TraceGoertzel(freq=f1, n=win_size)

        # Bộ đệm đầu vào
        self.in_buffer = deque(maxlen=win_size)
        self.out_count = 0
        
        print(f"PairGoertzel initialized: f0={f0:.6f}, f1={f1:.6f}, win_size={win_size}, n_per_bit={self.n_per_bit}")
    
    def __call__(self, s: float):
        """
        Xử lý một điểm mẫu - giao diện nhất quán với mã tham khảo
        
        Args:
            s: Giá trị điểm mẫu

        Returns:
            (amp0, amp1, p1_prob) - Biên độ tần số không gian, biên độ tần số đánh dấu, xác suất đánh dấu
        """
        self.in_buffer.append(s)
        self.out_count += 1
        
        amp0, amp1, p1_prob = 0, 0, None

        # Mỗi chu kỳ bit xuất ra một lần kết quả
        if self.out_count >= self.n_per_bit:
            amp0 = self.g0(self.in_buffer)  # Tính toán biên độ tần số không gian
            amp1 = self.g1(self.in_buffer)  # Tính toán biên độ tần số đánh dấu
            p1_prob = amp1 / (amp0 + amp1 + 1e-8)  # Tính toán xác suất đánh dấu
            self.out_count = 0
            
        return amp0, amp1, p1_prob


class RealTimeAFSKDecoder:
    """Bộ giải điều chế AFSK thời gian thực - Dựa trên khung bắt đầu"""

    def __init__(self, f_sample: int = 16000, mark_freq: int = 1800,
                 space_freq: int = 1500, bitrate: int = 100,
                 s_goertzel: int = 9, threshold: float = 0.5):
        """
        Khởi tạo bộ giải điều chế AFSK thời gian thực

        Args:
            f_sample: Tần số lấy mẫu
            mark_freq: Tần số Mark
            space_freq: Tần số Space
            bitrate: Tốc độ bit
            s_goertzel: Hệ số kích thước cửa sổ Goertzel (win_size = f_sample // mark_freq * s_goertzel)
            threshold: Ngưỡng quyết định
        """
        self.f_sample = f_sample
        self.mark_freq = mark_freq
        self.space_freq = space_freq
        self.bitrate = bitrate
        self.threshold = threshold

        # Tính toán kích thước cửa sổ - Với mã tham khảo
        win_size = int(f_sample / mark_freq * s_goertzel)

        # Khởi tạo bộ giải điều chế
        self.demodulator = PairGoertzel(f_sample, space_freq, mark_freq,
                                       bitrate, win_size)

        # Định nghĩa khung - Với mã tham khảo
        self.start_bytes = b'\x01\x02'
        self.end_bytes = b'\x03\x04'
        self.start_bits = "".join(format(int(x), '08b') for x in self.start_bytes)
        self.end_bits = "".join(format(int(x), '08b') for x in self.end_bytes)

        # Bộ máy trạng thái
        self.state = "idle" # idle / entering

        # Lưu trữ kết quả giải điều chế
        self.buffer_prelude: deque = deque(maxlen=len(self.start_bits))  # dùng để kiểm tra khung bắt đầu
        self.indicators = []  # lưu dãy xác suất
        self.signal_bits = ""  # lưu chuỗi bit
        self.text_cache = ""

        # Kết quả giải mã
        self.decoded_messages = []
        self.total_bits_received = 0

        print(f"Decoder initialized: win_size={win_size}")
        print(f"Start frame: {self.start_bits} (from {self.start_bytes.hex()})")
        print(f"End frame: {self.end_bits} (from {self.end_bytes.hex()})")
    
    def process_audio(self, samples: np.array) -> str:
        """
        Xử lý dữ liệu âm thanh và trả về văn bản đã giải mã

        Args:
            audio_data: Dữ liệu âm thanh (16-bit PCM)
            
        Returns:
            Văn bản mới được giải mã
        """       
        new_text = "" 
        # Xử lý từng mẫu
        for sample in samples:
            _, _, p1_prob = self.demodulator(sample)
            # Nếu có xác suất đầu ra, ghi nhận và quyết định bit
            if p1_prob is not None:
                bit = '1' if p1_prob > self.threshold else '0'
                match self.state:
                    case "idle":
                        self.buffer_prelude.append(bit)
                        pass
                    case "entering":
                        self.buffer_prelude.append(bit)
                        self.signal_bits += bit
                        self.total_bits_received += 1
                    case _:
                        pass
                self.indicators.append(p1_prob)

                # Kiểm tra trạng thái máy
                if self.state == "idle" and "".join(self.buffer_prelude) == self.start_bits:
                    self.state = "entering"
                    self.text_cache = ""
                    self.signal_bits = ""  # Xóa chuỗi bit
                    self.buffer_prelude.clear()
                elif self.state == "entering" and ("".join(self.buffer_prelude) == self.end_bits or len(self.signal_bits) >= 256):
                    self.state = "idle"
                    self.buffer_prelude.clear()

                # Sau khi thu thập đủ số bit nhất định, cố gắng giải mã
                if len(self.signal_bits) >= 8:
                    text = self._decode_bits_to_text(self.signal_bits)
                    if len(text) > len(self.text_cache):
                        new_text = text[len(self.text_cache) - len(text):]
                        self.text_cache = text
        return new_text
    
    def _decode_bits_to_text(self, bits: str) -> str:
        """
        Giải mã chuỗi bit sang văn bản

        Args:
            bits: Chuỗi bit
            
        Returns:
            Văn bản đã giải mã
        """
        if len(bits) < 8:
            return ""
        
        decoded_text = ""
        byte_count = len(bits) // 8
        
        for i in range(byte_count):
            # Trích 8 bit
            byte_bits = bits[i*8:(i+1)*8]
            
            # Chuyển bit thành byte
            byte_val = int(byte_bits, 2)
            
            # Thử giải mã thành ký tự ASCII
            if 32 <= byte_val <= 126:  # Ký tự ASCII có thể in được
                decoded_text += chr(byte_val)
            elif byte_val == 0:  # Ký tự NULL, bỏ qua
                continue
            else:
                # Ký tự không thể in: bỏ qua (hoặc hiển thị ở dạng hex nếu cần)
                pass
                # decoded_text += f"\\x{byte_val:02X}"
        
        return decoded_text
    
    def clear(self):
        """Xóa trạng thái bộ giải"""
        self.indicators = []
        self.signal_bits = ""
        self.decoded_messages = []
        self.total_bits_received = 0
        print("Trạng thái bộ giải đã được xóa")
    
    def get_stats(self) -> dict:
        """Lấy thông tin thống kê giải mã"""
        return {
            'prelude_bits': "".join(self.buffer_prelude),
            "state": self.state,
            'total_chars': sum(len(msg) for msg in self.text_cache),
            'buffer_bits': len(self.signal_bits),
            'mark_freq': self.mark_freq,
            'space_freq': self.space_freq,
            'bitrate': self.bitrate,
            'threshold': self.threshold,
        }
