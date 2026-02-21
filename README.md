# TFT Bot - Auto Purchase

Bot tự động mua tướng trong Teamfight Tactics sử dụng OCR (Tesseract)

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![DirectX 11](https://img.shields.io/badge/Rendering-DirectX%2011-green)

## Tính năng

- **OCR nhận diện tướng** - Dùng Tesseract OCR quét vùng shop, nhận diện tên tướng tự động
- **2 chế độ quét**
  - **Full Scan** - Quét toàn bộ shop cùng lúc (ổn định)
  - **Slot Scan** - Quét 5 slot song song bằng 5 thread (nhanh hơn)
- **GUI trực quan** - Giao diện ImGui với 4 tab: Control, Units, Preview, Settings
- **Chọn tướng linh hoạt** - Click chọn tướng muốn mua, hỗ trợ mục tiêu 2⭐ / 3⭐ / vô hạn
- **Hotkey** - F2 bắt đầu, F1 dừng
- **Tùy chỉnh vị trí slot** - Calibrate vùng shop qua slider trong tab Settings
- **Preview trực tiếp** - Xem vùng shop đang được capture

## Yêu cầu hệ thống

- Windows 10/11
- Độ phân giải **1920x1080** (Windowed Fullscreen)
- DirectX 11
- [MSYS2](https://www.msys2.org/) với môi trường `clang64` (để build)

## Cài đặt dependencies (MSYS2 clang64)

Mở terminal MSYS2 Clang64 và chạy:

```bash
pacman -S mingw-w64-clang-x86_64-clang \
          mingw-w64-clang-x86_64-tesseract-ocr \
          mingw-w64-clang-x86_64-leptonica \
          mingw-w64-clang-x86_64-pkg-config
```

## Build

```bash
git clone https://github.com/dkhoasobad/Auto-Roll-TFT
cd Auto-Roll-TFT
./build.sh
```

Script `build.sh` sẽ:

1. Compile mã nguồn C++17 với `clang++`
2. Tạo thư mục `TFT_Bot_Release/`
3. Copy executable + tất cả DLL cần thiết vào đó
4. Copy thư mục `tessdata/` (dữ liệu ngôn ngữ cho OCR)

Sau khi build xong, chạy:

```bash
cd TFT_Bot_Release
./TFT_Bot.exe
```

## Cách sử dụng

### 1. Khởi động

- Mở game TFT ở chế độ **Windowed Fullscreen 1920x1080**
- Chạy `TFT_Bot.exe`

### 2. Tab CONTROL

| Thành phần | Mô tả |
|---|---|
| **Scan** (slider) | Tốc độ quét (giây). Giá trị nhỏ = nhanh hơn, tốn CPU hơn |
| **Mode** | Chọn Full Scan hoặc Slot Scan |
| **Debug OCR** | Bật/tắt log chi tiết kết quả OCR |
| **START [F2]** | Bắt đầu bot |
| **STOP [F1]** | Dừng bot |
| **Reset All** | Reset số lượng tướng đã mua về 0 |

### 3. Tab UNITS

- Hiển thị tất cả tướng theo giá (1-5 gold)
- **Click 1 lần** vào tướng → mục tiêu 2⭐ (cần 3 con)
- **Click lần 2** → mục tiêu 3⭐ (cần 9 con)
- **Click lần 3** → mua vô hạn
- **Click lần 4** → tắt (không mua nữa)
- Ô tìm kiếm để lọc tướng theo tên

### 4. Tab PREVIEW

- Hiển thị ảnh chụp vùng shop đang được quét theo thời gian thực

### 5. Tab SETTINGS

- Chỉnh vị trí X/Y và kích thước W/H của 5 slot shop (% màn hình)
- Nút **Capture** để xem preview từng slot, kiểm tra vùng OCR có chính xác không
- Tùy chọn **Đồng bộ Y** và **Đồng bộ Size** để chỉnh nhanh cho tất cả slot
- Nút **Reset Default** để quay về giá trị mặc định

## Cấu trúc project

```
├── main.cpp              # Mã nguồn chính
├── config.h              # Hằng số cấu hình
├── hero.h                # Danh sách tướng theo cost
├── build.sh              # Script build
├── icon.ico              # Icon ứng dụng
├── resource.rc           # Windows resource file
├── imgui/                # Thư viện ImGui (GUI)
│   └── backends/         # Backend DirectX 11 + Win32
├── tessdata/             # Dữ liệu ngôn ngữ Tesseract OCR
│   ├── vie.traineddata   # Tiếng Việt
│   └── eng.traineddata   # Tiếng Anh
└── TFT_Bot_Release/      # Thư mục output sau khi build
```

## Tech stack

| Thành phần | Công nghệ |
|---|---|
| Ngôn ngữ | C++17 |
| GUI | [Dear ImGui](https://github.com/ocornut/imgui) |
| Rendering | DirectX 11 |
| OCR | [Tesseract 5.x](https://github.com/tesseract-ocr/tesseract) |
| Xử lý ảnh | [Leptonica](http://www.leptonica.org/) |
| Compiler | Clang++ (MSYS2) |

## Lưu ý

- Bot chỉ hoạt động ở độ phân giải **1920x1080 Windowed Fullscreen**
- Nếu OCR nhận sai, thử bật **Debug OCR** để kiểm tra và điều chỉnh vị trí slot trong tab Settings
- Cần có thư mục `tessdata/` chứa file `.traineddata` cùng thư mục với exe

## License

MIT
