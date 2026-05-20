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

/// Maximum JPEG frame buffer size (512 KB - sufficient for 640x480 JPEG).
const int _maxJpegBufferSize = 512 * 1024;

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
