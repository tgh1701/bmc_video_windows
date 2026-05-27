# bmc_video_windows

Flutter FFI plugin cung cấp khả năng **truy cập camera trên Windows** sử dụng Media Foundation (capture) và WIC (JPEG encoding).

## Tính năng

- 📷 Liệt kê tất cả camera devices trên Windows
- 🎥 Capture video ở resolution tùy chỉnh (mặc định 640x480)
- 🖼️ Trả về frame dạng **JPEG** qua FFI (không cần decode phức tạp)
- ⚡ Capture thread riêng biệt, không block UI
- 🔧 JPEG quality có thể điều chỉnh (0-100)

## Yêu cầu

- Windows 10 trở lên
- Visual Studio 2019+ (với C/C++ workload)
- Flutter 3.x trở lên

## Cài đặt

Thêm vào `pubspec.yaml`:

```yaml
dependencies:
  bmc_video_windows:
    git:
      url: https://github.com/tgh1701/bmc_video_windows.git
      ref: main
```

## Quick Start

```dart
import 'package:bmc_video_windows/bmc_video_windows.dart' as bmc_video;

// 1. Liệt kê camera
final devices = bmc_video.listCameraDevices();
print('Tìm thấy ${devices.length} camera');
for (final d in devices) {
  print('  ${d.index}: ${d.name}');
}

// 2. Bắt đầu capture
int result = bmc_video.startCapture(
  deviceIndex: 0,
  width: 640,
  height: 480,
  fps: 15,
  jpegQuality: 60,
);
print('startCapture: $result (0 = thành công)');

// 3. Lấy frame (gọi trong Timer.periodic)
final frame = bmc_video.getLatestJpegFrame();
if (frame != null) {
  print('Got JPEG frame: ${frame.length} bytes');
  // Hiển thị: Image.memory(frame, gaplessPlayback: true)
}

// 4. Dừng capture
bmc_video.stopCapture();

// 5. Giải phóng buffer
bmc_video.disposeFrameBuffer();
```

## API Reference

### Device Enumeration

| Hàm | Mô tả | Return |
|-----|--------|--------|
| `getCameraDeviceCount()` | Số lượng camera | `int` |
| `getCameraDeviceName(index)` | Tên camera theo index | `String` |
| `getCameraDeviceId(index)` | ID (symbolic link) camera | `String` |
| `listCameraDevices()` | Danh sách tất cả camera | `List<BmcVideoDevice>` |

### Capture Control

| Hàm | Mô tả | Return |
|-----|--------|--------|
| `startCapture({deviceIndex, width, height, fps, jpegQuality})` | Bắt đầu capture | `int` (0=OK, -1=fail) |
| `stopCapture()` | Dừng capture | `void` |
| `isCapturing()` | Kiểm tra trạng thái | `bool` |

### Frame Retrieval

| Hàm | Mô tả | Return |
|-----|--------|--------|
| `getLatestJpegFrame()` | Lấy frame JPEG mới nhất | `Uint8List?` |
| `getLatestRawFrame()` | Lấy frame raw BGRA mới nhất (không nén) | `Uint8List?` |
| `getFrameWidth()` | Width thực tế | `int` |
| `getFrameHeight()` | Height thực tế | `int` |
| `disposeFrameBuffer()` | Giải phóng toàn bộ native buffer (cả local và remote) | `void` |

### H.265/H.264 Video Encoder

Các hàm hỗ trợ nén frame hình từ camera sang luồng H.265 hoặc H.264 bằng phần cứng (Media Foundation) để gửi qua UDP.

| Hàm | Mô tả | Return |
|-----|--------|--------|
| `probeVideoCodecSupport()` | Kiểm tra phần cứng hỗ trợ nén Codec nào mà không cần mở camera (`1 = H265, 2 = H264, 0 = none`) | `int` |
| `getLatestH265Frame()` | Lấy frame đã được nén dạng H.265/H.264 mới nhất | `Uint8List?` |
| `isH265KeyFrame()` | Kiểm tra xem frame nén mới nhất có phải Key Frame (I-frame) không | `bool` |
| `getH265CodecType()` | Lấy loại codec đang hoạt động (`1 = H265, 2 = H264, 0 = none`) | `int` |
| `forceH265KeyFrame()` | Yêu cầu encoder bắt buộc sinh ra I-frame ở frame tiếp theo (khi mất gói UDP) | `void` |
| `setVideoQuality(quality)` | Cấu hình chất lượng mã hóa từ `10 - 100` | `void` |
| `getVideoQuality()` | Lấy chất lượng mã hóa hiện tại | `int` |

### H.265/H.264 Video Decoder

Các hàm hỗ trợ giải mã luồng video nén nhận được từ phía đối phương (Android/Windows khác) thành raw BGRA để hiển thị trực tiếp lên Flutter UI.

| Hàm | Mô tả | Return |
|-----|--------|--------|
| `detectCodecType(data)` | Tự động phân tích NAL headers để nhận diện loại Codec (`1 = H265, 2 = H264, 0 = unknown`) | `int` |
| `initVideoDecoder(width, height, codecType)` | Khởi tạo hoặc thay đổi codec phần cứng giải mã | `int` (`0 = OK`, `-1 = fail`) |
| `decodeVideoFrame(compressedData)` | Nạp dữ liệu nén UDP nhận được để đưa vào decoder giải mã | `int` (`1 = OK`, `0 = need more`, `-1 = error`) |
| `getLatestDecodedFrame()` | Lấy raw BGRA pixel bytes đã được giải mã thành công | `Uint8List?` |
| `cleanupVideoDecoder()` | Giải phóng bộ nhớ và tài nguyên của decoder | `void` |

### BmcVideoDevice

```dart
class BmcVideoDevice {
  final int index;    // 0-based index
  final String id;    // Symbolic link (unique ID)
  final String name;  // Tên hiển thị (e.g., "Logitech Webcam C930e")
}
```

## Kiến trúc

```
┌──────────────────┐
│   Flutter (Dart)  │
│  bmc_video_win..  │
│  dart:ffi calls   │
└────────┬─────────┘
         │ FFI
┌────────▼─────────┐
│  bmc_video_win.. │  ← Native C DLL
│  .dll            │
│                  │
│  ┌─────────────┐ │
│  │ Capture     │ │  ← Thread riêng (_beginthreadex)
│  │ Thread      │ │
│  │             │ │
│  │ MF Source   │ │  ← Media Foundation: camera → RGB24
│  │ Reader      │ │
│  │     ↓       │ │
│  │ WIC JPEG    │ │  ← WIC: RGB24 → JPEG
│  │ Encoder     │ │
│  │     ↓       │ │
│  │ Shared Buf  │ │  ← Mutex-protected shared buffer
│  └─────────────┘ │
└──────────────────┘
```

**Flow:**
1. `startCapture()` → tạo capture thread
2. Thread: `CoInitialize` → `MFStartup` → `MFEnumDeviceSources` → `ActivateObject` → `MFCreateSourceReaderFromMediaSource`
3. Loop: `ReadSample()` → `encode_rgb_to_jpeg()` (WIC) → copy vào shared buffer (mutex)
4. Dart side: `Timer.periodic(66ms)` → `getLatestJpegFrame()` → `Image.memory()`
5. `stopCapture()` → set `running=0` → wait thread exit → cleanup

## Ví dụ: Camera Preview Widget

```dart
import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:bmc_video_windows/bmc_video_windows.dart' as bmc_video;

class CameraPreview extends StatefulWidget {
  final int deviceIndex;
  const CameraPreview({super.key, this.deviceIndex = 0});

  @override
  State<CameraPreview> createState() => _CameraPreviewState();
}

class _CameraPreviewState extends State<CameraPreview> {
  Uint8List? _frame;
  Timer? _timer;
  bool _capturing = false;

  @override
  void initState() {
    super.initState();
    _start();
  }

  @override
  void dispose() {
    _stop();
    super.dispose();
  }

  Future<void> _start() async {
    final result = bmc_video.startCapture(
      deviceIndex: widget.deviceIndex,
      width: 640,
      height: 480,
      fps: 15,
      jpegQuality: 60,
    );
    if (result != 0) return;

    await Future.delayed(const Duration(milliseconds: 800));
    _capturing = true;

    _timer = Timer.periodic(const Duration(milliseconds: 66), (_) {
      final frame = bmc_video.getLatestJpegFrame();
      if (frame != null && mounted) {
        setState(() => _frame = frame);
      }
    });
  }

  void _stop() {
    _timer?.cancel();
    if (_capturing) {
      bmc_video.stopCapture();
      bmc_video.disposeFrameBuffer();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Colors.black,
      child: _frame != null
          ? Image.memory(_frame!, gaplessPlayback: true, fit: BoxFit.contain)
          : const Center(child: CircularProgressIndicator()),
    );
  }
}
```

## Lưu ý

- **Chỉ hỗ trợ Windows**. Các platform khác sẽ throw `UnsupportedError`.
- **Một camera tại một thời điểm**: Plugin sử dụng global state, chỉ capture 1 camera cùng lúc.
- **JPEG encoding trên CPU**: Mỗi frame được encode bằng WIC trên capture thread. Với 640x480@15fps, CPU usage thường < 5%.
- **Thread safety**: Shared buffer được bảo vệ bởi Windows Mutex. `getLatestJpegFrame()` an toàn để gọi từ UI thread.
- **Memory**: Gọi `disposeFrameBuffer()` khi không cần camera nữa để giải phóng native buffer.

## License

Private - BMC Internal Use
