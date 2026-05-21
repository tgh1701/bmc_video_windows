#ifndef BMC_VIDEO_WINDOWS_H
#define BMC_VIDEO_WINDOWS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

// ============================================================================
// Camera Device Enumeration
// ============================================================================

/// Get number of available camera (video capture) devices.
FFI_PLUGIN_EXPORT
int getCameraDeviceCount(void);

/// Get the friendly name of a camera device by index.
/// Returns pointer to internal static buffer (valid until next call).
FFI_PLUGIN_EXPORT
const char* getCameraDeviceName(int index);

/// Get the symbolic link (ID) of a camera device by index.
/// Returns pointer to internal static buffer (valid until next call).
FFI_PLUGIN_EXPORT
const char* getCameraDeviceId(int index);

// ============================================================================
// Camera Capture Control
// ============================================================================

/// Start camera capture on a background thread.
/// deviceIndex: 0-based index from getCameraDeviceCount(). -1 = first available.
/// width, height: requested resolution (e.g. 640, 480).
/// fps: requested frame rate (e.g. 15).
/// jpegQuality: JPEG compression quality 0-100 (e.g. 60).
/// Returns 0 on success, -1 on failure.
FFI_PLUGIN_EXPORT
int startCameraCapture(int deviceIndex, int width, int height, int fps, int jpegQuality);

/// Stop camera capture and release resources.
FFI_PLUGIN_EXPORT
void stopCameraCapture(void);

/// Check if camera is currently capturing.
/// Returns 1 if capturing, 0 otherwise.
FFI_PLUGIN_EXPORT
int isCameraCapturing(void);

// ============================================================================
// Frame Retrieval
// ============================================================================

/// Get the latest captured frame as JPEG-encoded bytes.
/// buffer: output buffer to receive JPEG data.
/// bufferSize: size of the output buffer in bytes.
/// Returns: number of bytes written to buffer, 0 if no new frame available.
FFI_PLUGIN_EXPORT
int getLatestFrame(uint8_t* buffer, int bufferSize);

/// Get the actual width of captured frames.
FFI_PLUGIN_EXPORT
int getFrameWidth(void);

/// Get the actual height of captured frames.
FFI_PLUGIN_EXPORT
int getFrameHeight(void);

/// Get path to the native debug log file.
FFI_PLUGIN_EXPORT
const char* getLogFilePath(void);

// ============================================================================
// Resolution Enumeration
// ============================================================================

/// Get number of unique supported resolutions for a camera device.
/// This enumerates all native media types, de-duplicates, and sorts by
/// resolution descending (best first). Results are cached internally.
/// Returns number of unique resolutions found.
FFI_PLUGIN_EXPORT
int getCameraResolutionCount(int deviceIndex);

/// Get resolution info as string "WIDTHxHEIGHT@FPS" at given index.
/// Must call getCameraResolutionCount() first to populate the list.
/// Index 0 = best (highest) resolution.
FFI_PLUGIN_EXPORT
const char* getCameraResolution(int resIndex);

#endif // BMC_VIDEO_WINDOWS_H
