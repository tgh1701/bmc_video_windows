import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';

import 'bmc_video_windows_bindings_generated.dart';

// ============================================================================
// Device Info
// ============================================================================

/// Camera device info returned by enumeration.
class BmcVideoDevice {
  final int index;
  final String id;
  final String name;

  BmcVideoDevice({
    required this.index,
    required this.id,
    required this.name,
  });

  @override
  String toString() =>
      'BmcVideoDevice(index: $index, name: "$name")';
}

// ============================================================================
// Camera Device Enumeration
// ============================================================================

/// Get number of available camera devices.
int getCameraDeviceCount() => _bindings.getCameraDeviceCount();

/// Get the friendly name of a camera device by index.
String getCameraDeviceName(int index) {
  final ptr = _bindings.getCameraDeviceName(index);
  return ptr.cast<Utf8>().toDartString();
}

/// Get the symbolic link (ID) of a camera device by index.
String getCameraDeviceId(int index) {
  final ptr = _bindings.getCameraDeviceId(index);
  return ptr.cast<Utf8>().toDartString();
}

/// List all available camera devices.
List<BmcVideoDevice> listCameraDevices() {
  final count = getCameraDeviceCount();
  final devices = <BmcVideoDevice>[];
  for (int i = 0; i < count; i++) {
    devices.add(BmcVideoDevice(
      index: i,
      id: getCameraDeviceId(i),
      name: getCameraDeviceName(i),
    ));
  }
  return devices;
}

// ============================================================================
// Resolution Enumeration
// ============================================================================

/// Camera resolution info.
class CameraResolution {
  final int width;
  final int height;
  final int fps;

  CameraResolution({
    required this.width,
    required this.height,
    required this.fps,
  });

  /// Display label like "1920x1080 (30fps)"
  String get label => '${width}x$height (${fps}fps)';

  /// Total pixels for comparison
  int get pixels => width * height;

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is CameraResolution &&
          width == other.width &&
          height == other.height &&
          fps == other.fps;

  @override
  int get hashCode => width.hashCode ^ height.hashCode ^ fps.hashCode;

  @override
  String toString() => label;
}

// Native function typedefs for resolution enumeration
typedef _GetCameraResolutionCountC = Int32 Function(Int32 deviceIndex);
typedef _GetCameraResolutionCountDart = int Function(int deviceIndex);
typedef _GetCameraResolutionC = Pointer<Utf8> Function(Int32 resIndex);
typedef _GetCameraResolutionDart = Pointer<Utf8> Function(int resIndex);

/// List all supported resolutions for a camera device.
/// Returns sorted list (best/highest resolution first).
List<CameraResolution> listCameraResolutions(int deviceIndex) {
  final getCount = _dylib.lookupFunction<_GetCameraResolutionCountC, _GetCameraResolutionCountDart>('getCameraResolutionCount');
  final getRes = _dylib.lookupFunction<_GetCameraResolutionC, _GetCameraResolutionDart>('getCameraResolution');

  final count = getCount(deviceIndex);
  final resolutions = <CameraResolution>[];

  for (int i = 0; i < count; i++) {
    final ptr = getRes(i);
    final str = ptr.toDartString(); // "1920x1080@30"
    if (str.isEmpty) continue;

    // Parse "WIDTHxHEIGHT@FPS"
    final parts = str.split('@');
    if (parts.length != 2) continue;
    final wh = parts[0].split('x');
    if (wh.length != 2) continue;

    final w = int.tryParse(wh[0]) ?? 0;
    final h = int.tryParse(wh[1]) ?? 0;
    final fps = int.tryParse(parts[1]) ?? 0;
    if (w > 0 && h > 0) {
      resolutions.add(CameraResolution(width: w, height: h, fps: fps));
    }
  }

  return resolutions;
}

// ============================================================================
// Camera Capture Control
// ============================================================================

/// Start camera capture.
/// [deviceIndex]: 0-based index from listCameraDevices(). -1 = first available.
/// [width], [height]: requested resolution (default 640x480).
/// [fps]: requested frame rate (default 15).
/// [jpegQuality]: JPEG compression quality 0-100 (default 60).
/// Returns 0 on success, -1 on failure.
int startCapture({
  int deviceIndex = 0,
  int width = 640,
  int height = 480,
  int fps = 15,
  int jpegQuality = 60,
}) {
  return _bindings.startCameraCapture(deviceIndex, width, height, fps, jpegQuality);
}

/// Stop camera capture and release resources.
void stopCapture() => _bindings.stopCameraCapture();

/// Check if camera is currently capturing.
bool isCapturing() => _bindings.isCameraCapturing() != 0;

// ============================================================================
// Frame Retrieval
// ============================================================================

/// Maximum JPEG frame buffer size (2 MB - sufficient for up to 4K JPEG).
const int _maxJpegBufferSize = 2 * 1024 * 1024;

/// Reusable native buffer to avoid repeated allocation.
Pointer<Uint8>? _frameBuffer;

/// Get the latest captured frame as JPEG-encoded bytes.
/// Returns null if no new frame is available.
Uint8List? getLatestJpegFrame() {
  _frameBuffer ??= calloc<Uint8>(_maxJpegBufferSize);

  final int bytesWritten = _bindings.getLatestFrame(_frameBuffer!, _maxJpegBufferSize);
  if (bytesWritten <= 0) return null;

  // Copy from native buffer to Dart Uint8List
  return Uint8List.fromList(_frameBuffer!.asTypedList(bytesWritten));
}

/// Get the actual width of captured frames.
int getFrameWidth() => _bindings.getFrameWidth();

/// Get the actual height of captured frames.
int getFrameHeight() => _bindings.getFrameHeight();

/// Release the reusable native buffer.
/// Call this when you're done with camera operations.
void disposeFrameBuffer() {
  if (_frameBuffer != null) {
    calloc.free(_frameBuffer!);
    _frameBuffer = null;
  }
}

// ============================================================================
// H.265/H.264 Video Encoder
// ============================================================================

// Native function typedefs for H.265 encoder
typedef _GetLatestH265FrameC = Int32 Function(Pointer<Uint8> buffer, Int32 bufferSize);
typedef _GetLatestH265FrameDart = int Function(Pointer<Uint8> buffer, int bufferSize);
typedef _IsH265KeyFrameC = Int32 Function();
typedef _IsH265KeyFrameDart = int Function();
typedef _GetH265CodecTypeC = Int32 Function();
typedef _GetH265CodecTypeDart = int Function();
typedef _ForceH265KeyFrameC = Void Function();
typedef _ForceH265KeyFrameDart = void Function();

/// H.265 frame buffer (512KB for compressed output)
const int _maxH265BufferSize = 512 * 1024;
Pointer<Uint8>? _h265Buffer;

/// Get the latest H.265/H.264 encoded frame.
/// Returns null if no new frame available.
Uint8List? getLatestH265Frame() {
  _h265Buffer ??= calloc<Uint8>(_maxH265BufferSize);
  final getFrame = _dylib.lookupFunction<_GetLatestH265FrameC, _GetLatestH265FrameDart>('getLatestH265Frame');
  final size = getFrame(_h265Buffer!, _maxH265BufferSize);
  if (size <= 0) return null;
  return Uint8List.fromList(_h265Buffer!.asTypedList(size));
}

/// Check if the latest H.265 frame is a keyframe (I-frame).
bool isH265KeyFrame() {
  final fn = _dylib.lookupFunction<_IsH265KeyFrameC, _IsH265KeyFrameDart>('isH265KeyFrame');
  return fn() != 0;
}

/// Get active video codec type: 0=none, 1=H.265/HEVC, 2=H.264/AVC.
int getH265CodecType() {
  final fn = _dylib.lookupFunction<_GetH265CodecTypeC, _GetH265CodecTypeDart>('getH265CodecType');
  return fn();
}

/// Force the next frame to be encoded as a keyframe.
void forceH265KeyFrame() {
  final fn = _dylib.lookupFunction<_ForceH265KeyFrameC, _ForceH265KeyFrameDart>('forceH265KeyFrame');
  fn();
}

// ============================================================================
// Debug Logging
// ============================================================================

/// Get path to native debug log file.
String getLogFilePath() {
  final ptr = _dylib.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>('getLogFilePath')();
  return ptr.toDartString();
}

/// Read native debug log contents.
/// Returns the full log file contents as a string.
String getNativeLog() {
  try {
    final path = getLogFilePath();
    final file = File(path);
    if (file.existsSync()) {
      return file.readAsStringSync();
    }
    return '(log file not found: $path)';
  } catch (e) {
    return '(error reading log: $e)';
  }
}

/// Print native debug log to stdout (for Flutter console).
void printNativeLog() {
  final log = getNativeLog();
  print('=== bmc_video_windows native log ===');
  print(log);
  print('=== end native log ===');
}

// ============================================================================
// Native Library Loading
// ============================================================================

const String _libName = 'bmc_video_windows';

/// The dynamic library in which the symbols for [BmcVideoWindowsBindings] can be found.
final DynamicLibrary _dylib = () {
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$_libName.framework/$_libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unknown platform: ${Platform.operatingSystem}');
}();

/// The bindings to the native functions in [_dylib].
final BmcVideoWindowsBindings _bindings = BmcVideoWindowsBindings(_dylib);
