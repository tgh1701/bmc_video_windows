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

/// List all supported resolutions for a camera device.
/// Returns sorted list (best/highest resolution first).
List<CameraResolution> listCameraResolutions(int deviceIndex) {
  final count = _bindings.getCameraResolutionCount(deviceIndex);
  final resolutions = <CameraResolution>[];

  for (int i = 0; i < count; i++) {
    final ptr = _bindings.getCameraResolution(i);
    final str = ptr.cast<Utf8>().toDartString(); // "1920x1080@30"
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
/// NOTE: This is BLOCKED for 3 seconds after start (race condition protection).
/// Use forceStopCapture() when actually ending a call.
void stopCapture() => _bindings.stopCameraCapture();

/// Force stop camera capture — bypasses 3-second protection.
/// Use this when ACTUALLY ending a video call (dispose/bye).
void forceStopCapture() {
  try {
    final forceStop = _dylib.lookupFunction<Void Function(), void Function()>('forceStopCameraCapture');
    forceStop();
  } catch (_) {
    // Fallback to regular stop if function not found
    _bindings.stopCameraCapture();
  }
}

/// Check if camera is currently capturing.
bool isCapturing() => _bindings.isCameraCapturing() != 0;

// ============================================================================
// Frame Retrieval
// ============================================================================

/// Maximum raw BGRA frame buffer size (4 MB - supports up to 1024x1024 BGRA).
const int _maxRawFrameBufferSize = 4 * 1024 * 1024;

/// Reusable native buffer to avoid repeated allocation.
Pointer<Uint8>? _frameBuffer;

/// Get the latest captured frame as raw BGRA pixel bytes.
/// Returns a VIEW into native memory (zero-copy).
/// IMPORTANT: Data is only valid until the next call to this function!
/// Safe because decodeImageFromPixels copies pixel data synchronously.
Uint8List? getLatestRawFrame() {
  _frameBuffer ??= calloc<Uint8>(_maxRawFrameBufferSize);

  final int bytesWritten = _bindings.getLatestFrame(_frameBuffer!, _maxRawFrameBufferSize);
  if (bytesWritten <= 0) return null;

  // Zero-copy: return view into native buffer
  return _frameBuffer!.asTypedList(bytesWritten);
}

/// Get the actual width of captured frames.
int getFrameWidth() => _bindings.getFrameWidth();

/// Get the actual height of captured frames.
int getFrameHeight() => _bindings.getFrameHeight();

/// Release ALL reusable native buffers.
/// Call this when you're done with camera operations (end of video call).
void disposeFrameBuffer() {
  if (_frameBuffer != null) {
    calloc.free(_frameBuffer!);
    _frameBuffer = null;
  }
  // FIX: Also free H.265 encoder buffer (was previously leaked)
  if (_h265Buffer != null) {
    calloc.free(_h265Buffer!);
    _h265Buffer = null;
  }
  // FIX: Also free decoder buffers (were previously only freed in cleanupVideoDecoder)
  if (_decoderBuffer != null) {
    calloc.free(_decoderBuffer!);
    _decoderBuffer = null;
  }
  if (_decoderInputBuffer != null) {
    calloc.free(_decoderInputBuffer!);
    _decoderInputBuffer = null;
    _decoderInputBufferSize = 0;
  }
}

// ============================================================================
// H.265/H.264 Video Encoder
// ============================================================================

/// H.265 frame buffer (512KB for compressed output)
const int _maxH265BufferSize = 512 * 1024;
Pointer<Uint8>? _h265Buffer;

/// Get the latest H.265/H.264 encoded frame.
/// Returns a VIEW into native memory (zero-copy).
/// IMPORTANT: Data is only valid until the next call to this function!
Uint8List? getLatestH265Frame() {
  _h265Buffer ??= calloc<Uint8>(_maxH265BufferSize);
  final size = _bindings.getLatestH265Frame(_h265Buffer!, _maxH265BufferSize);
  if (size <= 0) return null;
  // Zero-copy: return view into native buffer
  // Safe because _sendVideoUdp copies data into UDP packet immediately
  return _h265Buffer!.asTypedList(size);
}

/// Check if the latest H.265 frame is a keyframe (I-frame).
bool isH265KeyFrame() => _bindings.isH265KeyFrame() != 0;

/// Get active video codec type: 0=none, 1=H.265/HEVC, 2=H.264/AVC.
int getH265CodecType() => _bindings.getH265CodecType();

/// Probe which video codec encoder is supported WITHOUT starting camera/encoder.
/// Returns: 1=H.265/HEVC, 2=H.264/AVC, 0=none.
int probeVideoCodecSupport() => _bindings.probeVideoCodecSupport();

/// Force the next frame to be encoded as a keyframe.
void forceH265KeyFrame() => _bindings.forceH265KeyFrame();

/// Set encoder quality (10-100). Higher = better quality, larger frames.
void setVideoQuality(int quality) => _bindings.setVideoQuality(quality);

/// Get current encoder quality (10-100).
int getVideoQuality() => _bindings.getVideoQuality();

// ============================================================================
// H.265/H.264 Video Decoder (for remote video)
// ============================================================================

/// Decoder frame buffer (2MB — enough for 640x480x4=1.2MB raw BGRA)
const int _maxDecoderBufferSize = 2 * 1024 * 1024;
Pointer<Uint8>? _decoderBuffer;
Pointer<Uint8>? _decoderInputBuffer;
int _decoderInputBufferSize = 0;

/// Detect codec type from compressed NAL data (auto-detect like Android).
/// Returns: 1=H.265/HEVC, 2=H.264/AVC, 0=unknown.
int detectCodecType(Uint8List data) {
  if (data.isEmpty) return 0;
  final size = data.length;
  // Reuse decoder input buffer for detection
  if (_decoderInputBuffer == null || size > _decoderInputBufferSize) {
    if (_decoderInputBuffer != null) calloc.free(_decoderInputBuffer!);
    _decoderInputBufferSize = size > 512 * 1024 ? size : 512 * 1024;
    _decoderInputBuffer = calloc<Uint8>(_decoderInputBufferSize);
  }
  _decoderInputBuffer!.asTypedList(size).setAll(0, data);
  return _bindings.detectCodecType(_decoderInputBuffer!, size);
}

/// Initialize video decoder.
/// [codecType]: 1=H.265/HEVC, 2=H.264/AVC.
/// If already initialized with different codec, will auto-reinit (like Android).
/// Returns 0 on success, -1 on failure.
int initVideoDecoder(int width, int height, int codecType) {
  return _bindings.initVideoDecoder(width, height, codecType);
}

/// Decode a compressed video frame.
/// Returns 1 (frame decoded), 0 (need more data), -1 (error).
int decodeVideoFrame(Uint8List compressedData) {
  final size = compressedData.length;
  if (size <= 0) return -1;

  // Allocate/reuse input buffer (grow if needed)
  if (_decoderInputBuffer == null || size > _decoderInputBufferSize) {
    if (_decoderInputBuffer != null) calloc.free(_decoderInputBuffer!);
    _decoderInputBufferSize = size > 512 * 1024 ? size : 512 * 1024;
    _decoderInputBuffer = calloc<Uint8>(_decoderInputBufferSize);
  }

  // Copy data to native buffer
  _decoderInputBuffer!.asTypedList(size).setAll(0, compressedData);

  return _bindings.decodeVideoFrame(_decoderInputBuffer!, size);
}

/// Get latest decoded frame as raw BGRA pixel bytes.
/// Returns a VIEW into native memory (zero-copy, saves ~1.2MB copy per frame!).
/// IMPORTANT: Data is only valid until the next call to this function!
/// Safe because decodeImageFromPixels copies pixel data synchronously.
Uint8List? getLatestDecodedFrame() {
  _decoderBuffer ??= calloc<Uint8>(_maxDecoderBufferSize);
  final size = _bindings.getLatestDecodedFrame(_decoderBuffer!, _maxDecoderBufferSize);
  if (size <= 0) return null;
  // Zero-copy: return view into native buffer
  return _decoderBuffer!.asTypedList(size);
}

/// Cleanup decoder and release resources.
void cleanupVideoDecoder() {
  _bindings.cleanupVideoDecoder();
  if (_decoderBuffer != null) {
    calloc.free(_decoderBuffer!);
    _decoderBuffer = null;
  }
  if (_decoderInputBuffer != null) {
    calloc.free(_decoderInputBuffer!);
    _decoderInputBuffer = null;
    _decoderInputBufferSize = 0;
  }
}

// ============================================================================
// Debug Logging
// ============================================================================

/// Get path to native debug log file.
String getLogFilePath() {
  final ptr = _bindings.getLogFilePath();
  return ptr.cast<Utf8>().toDartString();
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
