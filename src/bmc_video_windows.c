#include "bmc_video_windows.h"

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

// WIC (Windows Imaging Component) for JPEG encoding
#include <wincodec.h>

// COM / misc
#include <stdio.h>
#include <process.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

// ============================================================================
// Logging
// ============================================================================

static char s_logFilePath[MAX_PATH] = {0};

static void init_log_file(void) {
    if (s_logFilePath[0] == '\0') {
        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        sprintf_s(s_logFilePath, MAX_PATH, "%sbmc_video_debug.log", tempDir);
    }
}

static void log_to_file(const char* msg) {
    init_log_file();
    FILE* f = fopen(s_logFilePath, "a");
    if (f) {
        // Timestamp
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fflush(f);
        fclose(f);
    }
}

#define LOG(fmt, ...) { \
    char buf[512]; \
    sprintf_s(buf, sizeof(buf), "[bmc_video_windows] " fmt, __VA_ARGS__); \
    OutputDebugStringA(buf); \
    log_to_file(buf); \
}

// ============================================================================
// GUIDs (manually defined to avoid linker issues with Flutter's build system)
// ============================================================================

// MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE = {C60AC5FE-252A-478F-A0EF-BC8FA5F7CAD3}
static const GUID MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE = {
    0xc60ac5fe, 0x252a, 0x478f,
    {0xa0, 0xef, 0xbc, 0x8f, 0xa5, 0xf7, 0xca, 0xd3}
};

// MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID = {8AC3587A-4AE7-42D8-99E0-0A6013EEF90F}
static const GUID MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID = {
    0x8ac3587a, 0x4ae7, 0x42d8,
    {0x99, 0xe0, 0x0a, 0x60, 0x13, 0xee, 0xf9, 0x0f}
};

// MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME = {60D0E559-52F8-4FA2-BBCE-ACDB34A8EC01}
static const GUID MY_MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME = {
    0x60d0e559, 0x52f8, 0x4fa2,
    {0xbb, 0xce, 0xac, 0xdb, 0x34, 0xa8, 0xec, 0x01}
};

// MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK = {58F0AAD8-22BF-4F8A-BB3D-D2C4978C6E2F}
static const GUID MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK = {
    0x58f0aad8, 0x22bf, 0x4f8a,
    {0xbb, 0x3d, 0xd2, 0xc4, 0x97, 0x8c, 0x6e, 0x2f}
};

// MF_READWRITE_DISABLE_CONVERTERS = {98D5B065-1374-4847-8D5D-31520FEE7156}
// We'll NOT use this - we want converters enabled for format conversion

// MF_SOURCE_READER_FIRST_VIDEO_STREAM = 0xFFFFFFFC
#define MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM 0xFFFFFFFC

// MFVideoFormat_RGB24 = {00000014-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_RGB24 = {
    0x00000014, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MFVideoFormat_RGB32 = {00000016-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_RGB32 = {
    0x00000016, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MF_MT_MAJOR_TYPE = {48EBA18E-F8C9-4687-BF11-0A74C9F96A8F}
static const GUID MY_MF_MT_MAJOR_TYPE = {
    0x48eba18e, 0xf8c9, 0x4687,
    {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}
};

// MFMediaType_Video = {73646976-0000-0010-8000-00AA00389B71}
static const GUID MY_MFMediaType_Video = {
    0x73646976, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MF_MT_SUBTYPE = {F7E34C9A-42E8-4714-B74B-CB29D72C35E5}
static const GUID MY_MF_MT_SUBTYPE = {
    0xf7e34c9a, 0x42e8, 0x4714,
    {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}
};

// MF_MT_FRAME_SIZE = {1652C33D-D6B2-4012-B834-72030849A37D}
static const GUID MY_MF_MT_FRAME_SIZE = {
    0x1652c33d, 0xd6b2, 0x4012,
    {0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d}
};

// MF_MT_FRAME_RATE = {C459A2E8-3D2C-4E44-B132-FEE5156C7BB0}
static const GUID MY_MF_MT_FRAME_RATE = {
    0xc459a2e8, 0x3d2c, 0x4e44,
    {0xb1, 0x32, 0xfe, 0xe5, 0x15, 0x6c, 0x7b, 0xb0}
};

// CLSID_WICImagingFactory = {CACAF262-9370-4615-A13B-9F5539DA4C0A}
static const GUID MY_CLSID_WICImagingFactory = {
    0xcacaf262, 0x9370, 0x4615,
    {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}
};

// IID_IWICImagingFactory = {EC5EC8A9-C395-4314-9C77-54D7A935FF70}
static const GUID MY_IID_IWICImagingFactory = {
    0xec5ec8a9, 0xc395, 0x4314,
    {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}
};

// GUID_ContainerFormatJpeg = {19E4A5AA-5662-4FC5-A0C0-1758028E1057}
static const GUID MY_GUID_ContainerFormatJpeg = {
    0x19e4a5aa, 0x5662, 0x4fc5,
    {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}
};

// GUID_WICPixelFormat24bppBGR = {6FDDC324-4E03-4BFE-B185-3D77768DC90C}
static const GUID MY_GUID_WICPixelFormat24bppBGR = {
    0x6fddc324, 0x4e03, 0x4bfe,
    {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}
};

// IID_IMFMediaSource = {279A808D-AEC7-40C8-9C6B-A6B492C78A66}
static const GUID MY_IID_IMFMediaSource = {
    0x279a808d, 0xaec7, 0x40c8,
    {0x9c, 0x6b, 0xa6, 0xb4, 0x92, 0xc7, 0x8a, 0x66}
};

// ============================================================================
// Capture State
// ============================================================================

typedef struct CaptureState {
    int width;
    int height;
    int fps;
    int jpegQuality;
    volatile int running;
    HANDLE threadHandle;
    HANDLE frameMutex;

    // Latest JPEG frame buffer (double buffer to avoid blocking)
    uint8_t* jpegBuffer;
    int jpegSize;
    volatile int frameReady;

    // Device index
    int deviceIndex;
} CaptureState;

static CaptureState g_capture = {0};

// ============================================================================
// Device Enumeration
// ============================================================================

static char s_nameBuf[512];
static char s_idBuf[1024];

static HRESULT enumerate_devices(IMFActivate*** pppDevices, UINT32* pCount) {
    IMFAttributes* pAttributes = NULL;
    HRESULT hr;

    hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) return hr;

    hr = pAttributes->lpVtbl->SetGUID(pAttributes,
        &MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        &MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        pAttributes->lpVtbl->Release(pAttributes);
        return hr;
    }

    hr = MFEnumDeviceSources(pAttributes, pppDevices, pCount);
    pAttributes->lpVtbl->Release(pAttributes);
    return hr;
}

static void free_device_list(IMFActivate** ppDevices, UINT32 count) {
    if (ppDevices) {
        for (UINT32 i = 0; i < count; i++) {
            if (ppDevices[i]) {
                ppDevices[i]->lpVtbl->Release(ppDevices[i]);
            }
        }
        CoTaskMemFree(ppDevices);
    }
}

FFI_PLUGIN_EXPORT
int getCameraDeviceCount(void) {
    IMFActivate** ppDevices = NULL;
    UINT32 count = 0;
    BOOL shouldUninit = FALSE;

    LOG("getCameraDeviceCount: enter\n", 0);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    shouldUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG("getCameraDeviceCount: CoInitializeEx failed: 0x%08X\n", hr);
        return 0;
    }
    LOG("getCameraDeviceCount: CoInitializeEx ok (shouldUninit=%d)\n", shouldUninit);

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        LOG("getCameraDeviceCount: MFStartup failed: 0x%08X\n", hr);
        if (shouldUninit) CoUninitialize();
        return 0;
    }
    LOG("getCameraDeviceCount: MFStartup ok\n", 0);

    hr = enumerate_devices(&ppDevices, &count);
    LOG("getCameraDeviceCount: enumerate_devices hr=0x%08X count=%u\n", hr, count);
    free_device_list(ppDevices, count);

    MFShutdown();
    if (shouldUninit) CoUninitialize();

    LOG("getCameraDeviceCount: returning %d\n", SUCCEEDED(hr) ? (int)count : 0);
    return SUCCEEDED(hr) ? (int)count : 0;
}

FFI_PLUGIN_EXPORT
const char* getCameraDeviceName(int index) {
    IMFActivate** ppDevices = NULL;
    UINT32 count = 0;
    BOOL shouldUninit = FALSE;

    s_nameBuf[0] = '\0';

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    shouldUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return s_nameBuf;

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        if (shouldUninit) CoUninitialize();
        return s_nameBuf;
    }

    hr = enumerate_devices(&ppDevices, &count);
    if (SUCCEEDED(hr) && (UINT32)index < count) {
        WCHAR* wName = NULL;
        UINT32 nameLen = 0;
        hr = ppDevices[index]->lpVtbl->GetAllocatedString(
            ppDevices[index],
            &MY_MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &wName, &nameLen);
        if (SUCCEEDED(hr) && wName) {
            WideCharToMultiByte(CP_UTF8, 0, wName, -1, s_nameBuf, sizeof(s_nameBuf), NULL, NULL);
            CoTaskMemFree(wName);
        }
    }
    free_device_list(ppDevices, count);

    MFShutdown();
    if (shouldUninit) CoUninitialize();
    return s_nameBuf;
}

FFI_PLUGIN_EXPORT
const char* getCameraDeviceId(int index) {
    IMFActivate** ppDevices = NULL;
    UINT32 count = 0;
    BOOL shouldUninit = FALSE;

    s_idBuf[0] = '\0';

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    shouldUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return s_idBuf;

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        if (shouldUninit) CoUninitialize();
        return s_idBuf;
    }

    hr = enumerate_devices(&ppDevices, &count);
    if (SUCCEEDED(hr) && (UINT32)index < count) {
        WCHAR* wId = NULL;
        UINT32 idLen = 0;
        hr = ppDevices[index]->lpVtbl->GetAllocatedString(
            ppDevices[index],
            &MY_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &wId, &idLen);
        if (SUCCEEDED(hr) && wId) {
            WideCharToMultiByte(CP_UTF8, 0, wId, -1, s_idBuf, sizeof(s_idBuf), NULL, NULL);
            CoTaskMemFree(wId);
        }
    }
    free_device_list(ppDevices, count);

    MFShutdown();
    if (shouldUninit) CoUninitialize();
    return s_idBuf;
}

// ============================================================================
// JPEG Encoding using WIC
// ============================================================================

/// Encode RGB24 (BGR) pixel data to JPEG.
/// rgbData: pointer to BGR pixel data (bottom-up from Media Foundation).
/// width, height: frame dimensions.
/// quality: JPEG quality 0-100.
/// outJpeg: output buffer for JPEG data (caller-allocated).
/// outJpegSize: size of output buffer.
/// Returns: number of bytes written to outJpeg, 0 on failure.
static int encode_rgb_to_jpeg(
    const uint8_t* rgbData, int width, int height, int quality,
    uint8_t* outJpeg, int outJpegSize)
{
    IWICImagingFactory* pFactory = NULL;
    IWICBitmapEncoder* pEncoder = NULL;
    IWICBitmapFrameEncode* pFrame = NULL;
    IPropertyBag2* pPropertyBag = NULL;
    IStream* pStream = NULL;
    int result = 0;

    HRESULT hr;

    // Create in-memory stream
    pStream = SHCreateMemStream(NULL, 0);
    if (!pStream) {
        LOG("SHCreateMemStream failed\n", 0);
        return 0;
    }

    // Create WIC factory
    hr = CoCreateInstance(
        &MY_CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        &MY_IID_IWICImagingFactory, (void**)&pFactory);
    if (FAILED(hr)) {
        LOG("CoCreateInstance WICImagingFactory failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Create JPEG encoder
    hr = pFactory->lpVtbl->CreateEncoder(pFactory,
        &MY_GUID_ContainerFormatJpeg, NULL, &pEncoder);
    if (FAILED(hr)) {
        LOG("CreateEncoder JPEG failed: 0x%08X\n", hr);
        goto cleanup;
    }

    hr = pEncoder->lpVtbl->Initialize(pEncoder, pStream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        LOG("Encoder Initialize failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Create frame
    hr = pEncoder->lpVtbl->CreateNewFrame(pEncoder, &pFrame, &pPropertyBag);
    if (FAILED(hr)) {
        LOG("CreateNewFrame failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Set JPEG quality via property bag
    if (pPropertyBag) {
        PROPBAG2 option = {0};
        option.pstrName = L"ImageQuality";
        VARIANT varValue;
        VariantInit(&varValue);
        varValue.vt = VT_R4;
        varValue.fltVal = (float)quality / 100.0f;
        pPropertyBag->lpVtbl->Write(pPropertyBag, 1, &option, &varValue);
    }

    hr = pFrame->lpVtbl->Initialize(pFrame, pPropertyBag);
    if (FAILED(hr)) {
        LOG("Frame Initialize failed: 0x%08X\n", hr);
        goto cleanup;
    }

    hr = pFrame->lpVtbl->SetSize(pFrame, (UINT)width, (UINT)height);
    if (FAILED(hr)) {
        LOG("Frame SetSize failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Set pixel format - WIC will convert if needed
    GUID pixelFormat = MY_GUID_WICPixelFormat24bppBGR;
    hr = pFrame->lpVtbl->SetPixelFormat(pFrame, &pixelFormat);
    if (FAILED(hr)) {
        LOG("Frame SetPixelFormat failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Media Foundation RGB24 is bottom-up BGR. We need to flip vertically.
    {
        int stride = width * 3;
        // Align stride to 4 bytes (DWORD) as MF does
        int mfStride = (stride + 3) & ~3;

        // Write rows top-to-bottom (flip the bottom-up MF data)
        uint8_t* flipped = (uint8_t*)malloc(stride * height);
        if (!flipped) goto cleanup;

        for (int y = 0; y < height; y++) {
            const uint8_t* srcRow = rgbData + (height - 1 - y) * mfStride;
            uint8_t* dstRow = flipped + y * stride;
            memcpy(dstRow, srcRow, stride);
        }

        hr = pFrame->lpVtbl->WritePixels(pFrame, (UINT)height, (UINT)stride, (UINT)(stride * height), flipped);
        free(flipped);
    }

    if (FAILED(hr)) {
        LOG("Frame WritePixels failed: 0x%08X\n", hr);
        goto cleanup;
    }

    hr = pFrame->lpVtbl->Commit(pFrame);
    if (FAILED(hr)) {
        LOG("Frame Commit failed: 0x%08X\n", hr);
        goto cleanup;
    }

    hr = pEncoder->lpVtbl->Commit(pEncoder);
    if (FAILED(hr)) {
        LOG("Encoder Commit failed: 0x%08X\n", hr);
        goto cleanup;
    }

    // Read JPEG data from stream
    {
        LARGE_INTEGER liZero = {0};
        ULARGE_INTEGER pos;
        hr = pStream->lpVtbl->Seek(pStream, liZero, STREAM_SEEK_END, &pos);
        if (FAILED(hr)) goto cleanup;

        DWORD jpegLen = (DWORD)pos.QuadPart;
        if (jpegLen > 0 && (int)jpegLen <= outJpegSize) {
            hr = pStream->lpVtbl->Seek(pStream, liZero, STREAM_SEEK_SET, NULL);
            if (FAILED(hr)) goto cleanup;

            ULONG bytesRead = 0;
            hr = pStream->lpVtbl->Read(pStream, outJpeg, jpegLen, &bytesRead);
            if (SUCCEEDED(hr)) {
                result = (int)bytesRead;
            }
        } else if ((int)jpegLen > outJpegSize) {
            LOG("JPEG too large: %u > %d\n", jpegLen, outJpegSize);
        }
    }

cleanup:
    if (pFrame) pFrame->lpVtbl->Release(pFrame);
    if (pPropertyBag) pPropertyBag->lpVtbl->Release(pPropertyBag);
    if (pEncoder) pEncoder->lpVtbl->Release(pEncoder);
    if (pFactory) pFactory->lpVtbl->Release(pFactory);
    if (pStream) pStream->lpVtbl->Release(pStream);

    return result;
}

// ============================================================================
// Camera Capture Thread
// ============================================================================

static unsigned __stdcall camera_capture_thread(void* arg) {
    CaptureState* state = (CaptureState*)arg;

    IMFActivate** ppDevices = NULL;
    UINT32 deviceCount = 0;
    IMFMediaSource* pSource = NULL;
    IMFSourceReader* pReader = NULL;
    IMFMediaType* pOutputType = NULL;
    HRESULT hr;

    int actualWidth = state->width;
    int actualHeight = state->height;

    LOG("capture_thread: ENTER (device=%d, %dx%d@%dfps)\n", state->deviceIndex, state->width, state->height, state->fps);

    // Allocate JPEG output buffer (max ~500KB for 640x480)
    int jpegBufSize = 512 * 1024;
    uint8_t* tempJpeg = (uint8_t*)malloc(jpegBufSize);
    if (!tempJpeg) {
        LOG("capture_thread: Failed to allocate JPEG buffer\n", 0);
        state->running = 0;
        return 1;
    }

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG("capture_thread: CoInitializeEx failed: 0x%08X\n", hr);
        free(tempJpeg);
        state->running = 0;
        return 1;
    }
    LOG("capture_thread: CoInitializeEx ok\n", 0);

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        LOG("capture_thread: MFStartup failed: 0x%08X\n", hr);
        free(tempJpeg);
        CoUninitialize();
        state->running = 0;
        return 1;
    }
    LOG("capture_thread: MFStartup ok\n", 0);

    // Enumerate devices
    hr = enumerate_devices(&ppDevices, &deviceCount);
    if (FAILED(hr) || deviceCount == 0) {
        LOG("capture_thread: No camera devices found (hr=0x%08X, count=%u)\n", hr, deviceCount);
        goto exit;
    }
    LOG("capture_thread: Found %u devices\n", deviceCount);

    int devIdx = state->deviceIndex;
    if (devIdx < 0 || (UINT32)devIdx >= deviceCount) devIdx = 0;

    LOG("capture_thread: Using camera device %d of %u\n", devIdx, deviceCount);

    // Activate the device to get IMFMediaSource
    hr = ppDevices[devIdx]->lpVtbl->ActivateObject(
        ppDevices[devIdx],
        &MY_IID_IMFMediaSource,
        (void**)&pSource);
    if (FAILED(hr)) {
        LOG("capture_thread: ActivateObject failed: 0x%08X\n", hr);
        goto exit;
    }
    LOG("capture_thread: ActivateObject ok, pSource=%p\n", pSource);

    // Create source reader
    hr = MFCreateSourceReaderFromMediaSource(pSource, NULL, &pReader);
    if (FAILED(hr)) {
        LOG("capture_thread: MFCreateSourceReaderFromMediaSource failed: 0x%08X\n", hr);
        goto exit;
    }
    LOG("capture_thread: SourceReader created ok\n", 0);

    // Configure output format: request RGB24 (MF will auto-convert)
    hr = MFCreateMediaType(&pOutputType);
    if (FAILED(hr)) {
        LOG("capture_thread: MFCreateMediaType failed: 0x%08X\n", hr);
        goto exit;
    }

    pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_MAJOR_TYPE, &MY_MFMediaType_Video);
    pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_SUBTYPE, &MY_MFVideoFormat_RGB24);

    // Set requested resolution
    {
        UINT64 frameSize = ((UINT64)state->width << 32) | (UINT64)state->height;
        pOutputType->lpVtbl->SetUINT64(pOutputType, &MY_MF_MT_FRAME_SIZE, frameSize);
    }

    // Set requested frame rate
    {
        UINT64 frameRate = ((UINT64)state->fps << 32) | 1ULL;
        pOutputType->lpVtbl->SetUINT64(pOutputType, &MY_MF_MT_FRAME_RATE, frameRate);
    }

    hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
        MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pOutputType);
    if (FAILED(hr)) {
        LOG("capture_thread: SetCurrentMediaType RGB24 failed: 0x%08X, trying RGB32...\n", hr);

        // Fallback: try RGB32
        pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_SUBTYPE, &MY_MFVideoFormat_RGB32);
        hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
            MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pOutputType);
        if (FAILED(hr)) {
            LOG("capture_thread: SetCurrentMediaType RGB32 also failed: 0x%08X\n", hr);
            goto exit;
        }
        LOG("capture_thread: Using RGB32 format\n", 0);
    } else {
        LOG("capture_thread: Using RGB24 format\n", 0);
    }

    // Read back actual media type to get real resolution
    {
        IMFMediaType* pActualType = NULL;
        hr = pReader->lpVtbl->GetCurrentMediaType(pReader,
            MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pActualType);
        if (SUCCEEDED(hr)) {
            UINT64 frameSize = 0;
            hr = pActualType->lpVtbl->GetUINT64(pActualType, &MY_MF_MT_FRAME_SIZE, &frameSize);
            if (SUCCEEDED(hr)) {
                actualWidth = (int)(frameSize >> 32);
                actualHeight = (int)(frameSize & 0xFFFFFFFF);
            }
            pActualType->lpVtbl->Release(pActualType);
        }
    }

    state->width = actualWidth;
    state->height = actualHeight;

    LOG("capture_thread: CAPTURE LOOP STARTING: %dx%d @ %dfps, quality=%d\n",
        actualWidth, actualHeight, state->fps, state->jpegQuality);

    // Capture loop
    int frameCount = 0;
    while (state->running) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* pSample = NULL;

        hr = pReader->lpVtbl->ReadSample(pReader,
            MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &streamIndex, &flags, &timestamp, &pSample);

        if (FAILED(hr)) {
            LOG("capture_thread: ReadSample failed: 0x%08X\n", hr);
            Sleep(10);
            continue;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            LOG("capture_thread: End of stream\n", 0);
            if (pSample) pSample->lpVtbl->Release(pSample);
            break;
        }

        if (pSample) {
            IMFMediaBuffer* pBuffer = NULL;
            hr = pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer);
            if (SUCCEEDED(hr)) {
                BYTE* pBits = NULL;
                DWORD maxLen = 0, curLen = 0;
                hr = pBuffer->lpVtbl->Lock(pBuffer, &pBits, &maxLen, &curLen);
                if (SUCCEEDED(hr) && pBits && curLen > 0) {
                    // Encode to JPEG
                    int jpegLen = encode_rgb_to_jpeg(
                        pBits, actualWidth, actualHeight,
                        state->jpegQuality, tempJpeg, jpegBufSize);

                    if (jpegLen > 0) {
                        frameCount++;
                        if (frameCount == 1) {
                            LOG("capture_thread: FIRST FRAME! jpegLen=%d\n", jpegLen);
                        }
                        if (frameCount % 30 == 0) {
                            LOG("capture_thread: frame #%d, jpegLen=%d\n", frameCount, jpegLen);
                        }
                        // Copy to shared buffer
                        WaitForSingleObject(state->frameMutex, INFINITE);
                        if (state->jpegBuffer == NULL || jpegLen > jpegBufSize) {
                            // Reallocate if needed
                            free(state->jpegBuffer);
                            state->jpegBuffer = (uint8_t*)malloc(jpegBufSize);
                        }
                        if (state->jpegBuffer) {
                            memcpy(state->jpegBuffer, tempJpeg, jpegLen);
                            state->jpegSize = jpegLen;
                            state->frameReady = 1;
                        }
                        ReleaseMutex(state->frameMutex);
                    } else {
                        if (frameCount == 0) {
                            LOG("capture_thread: encode_rgb_to_jpeg returned 0 (curLen=%u)\n", curLen);
                        }
                    }

                    pBuffer->lpVtbl->Unlock(pBuffer);
                }
                pBuffer->lpVtbl->Release(pBuffer);
            }
            pSample->lpVtbl->Release(pSample);
        }
    }

    LOG("capture_thread: loop ended, total frames=%d\n", frameCount);

exit:
    if (pOutputType) pOutputType->lpVtbl->Release(pOutputType);
    if (pReader) pReader->lpVtbl->Release(pReader);
    if (pSource) {
        pSource->lpVtbl->Shutdown(pSource);
        pSource->lpVtbl->Release(pSource);
    }
    free_device_list(ppDevices, deviceCount);

    free(tempJpeg);
    MFShutdown();
    CoUninitialize();

    state->running = 0;
    LOG("capture_thread: EXIT\n", 0);
    return 0;
}

// ============================================================================
// FFI Exports: Capture Control
// ============================================================================

FFI_PLUGIN_EXPORT
int startCameraCapture(int deviceIndex, int width, int height, int fps, int jpegQuality) {
    if (g_capture.running) {
        LOG("Camera already capturing, stop first\n", 0);
        return -1;
    }

    // Initialize state
    memset(&g_capture, 0, sizeof(CaptureState));
    g_capture.deviceIndex = deviceIndex;
    g_capture.width = width;
    g_capture.height = height;
    g_capture.fps = fps;
    g_capture.jpegQuality = (jpegQuality > 0 && jpegQuality <= 100) ? jpegQuality : 60;
    g_capture.running = 1;
    g_capture.frameReady = 0;
    g_capture.jpegBuffer = (uint8_t*)malloc(512 * 1024);
    g_capture.jpegSize = 0;

    g_capture.frameMutex = CreateMutex(NULL, FALSE, NULL);
    if (!g_capture.frameMutex) {
        LOG("CreateMutex failed\n", 0);
        g_capture.running = 0;
        return -1;
    }

    g_capture.threadHandle = (HANDLE)_beginthreadex(
        NULL, 0, camera_capture_thread, &g_capture, 0, NULL);
    if (!g_capture.threadHandle) {
        LOG("_beginthreadex failed\n", 0);
        CloseHandle(g_capture.frameMutex);
        g_capture.running = 0;
        return -1;
    }

    LOG("startCameraCapture: device=%d, %dx%d@%dfps, quality=%d\n",
        deviceIndex, width, height, fps, jpegQuality);
    return 0;
}

FFI_PLUGIN_EXPORT
void stopCameraCapture(void) {
    if (!g_capture.running) return;

    LOG("Stopping camera capture...\n", 0);
    g_capture.running = 0;

    if (g_capture.threadHandle) {
        WaitForSingleObject(g_capture.threadHandle, 5000);
        CloseHandle(g_capture.threadHandle);
        g_capture.threadHandle = NULL;
    }

    if (g_capture.frameMutex) {
        CloseHandle(g_capture.frameMutex);
        g_capture.frameMutex = NULL;
    }

    if (g_capture.jpegBuffer) {
        free(g_capture.jpegBuffer);
        g_capture.jpegBuffer = NULL;
    }

    g_capture.jpegSize = 0;
    g_capture.frameReady = 0;
    LOG("Camera capture stopped\n", 0);
}

FFI_PLUGIN_EXPORT
int isCameraCapturing(void) {
    return g_capture.running ? 1 : 0;
}

// ============================================================================
// FFI Exports: Frame Retrieval
// ============================================================================

FFI_PLUGIN_EXPORT
int getLatestFrame(uint8_t* buffer, int bufferSize) {
    if (!g_capture.running || !g_capture.frameReady || !buffer || bufferSize <= 0) {
        return 0;
    }

    int copied = 0;
    WaitForSingleObject(g_capture.frameMutex, INFINITE);

    if (g_capture.frameReady && g_capture.jpegSize > 0 && g_capture.jpegSize <= bufferSize) {
        memcpy(buffer, g_capture.jpegBuffer, g_capture.jpegSize);
        copied = g_capture.jpegSize;
        g_capture.frameReady = 0; // Mark as consumed
    }

    ReleaseMutex(g_capture.frameMutex);
    return copied;
}

FFI_PLUGIN_EXPORT
int getFrameWidth(void) {
    return g_capture.width;
}

FFI_PLUGIN_EXPORT
int getFrameHeight(void) {
    return g_capture.height;
}

FFI_PLUGIN_EXPORT
const char* getLogFilePath(void) {
    init_log_file();
    return s_logFilePath;
}
