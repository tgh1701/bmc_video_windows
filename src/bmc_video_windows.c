#include "bmc_video_windows.h"

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>

// WIC (Windows Imaging Component) for JPEG encoding
#include <wincodec.h>

// COM / misc
#include <stdio.h>
#include <process.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
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

// MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = {FB394F3D-CCF0-42B5-B722-435EC513CC9E}
static const GUID MY_MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING = {
    0xfb394f3d, 0xccf0, 0x42b5,
    {0xb7, 0x22, 0x43, 0x5e, 0xc5, 0x13, 0xcc, 0x9e}
};

// === H.265/H.264 Encoder GUIDs ===

// MFVideoFormat_HEVC = {43564548-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_HEVC = {
    0x43564548, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MFVideoFormat_H264 = {34363248-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_H264 = {
    0x34363248, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MFVideoFormat_NV12 = {3231564E-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_NV12 = {
    0x3231564e, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// MFVideoFormat_YUY2 = {32595559-0000-0010-8000-00AA00389B71}
static const GUID MY_MFVideoFormat_YUY2 = {
    0x32595559, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// IID_IMFTransform = {BF94C121-5B05-4E6F-8000-BA598961414D}
static const GUID MY_IID_IMFTransform = {
    0xbf94c121, 0x5b05, 0x4e6f,
    {0x80, 0x00, 0xba, 0x59, 0x89, 0x61, 0x41, 0x4d}
};

// IID_ICodecAPI = {901DB4C7-31CE-41A2-85DC-8FA0BF41B8DA}
static const GUID MY_IID_ICodecAPI = {
    0x901db4c7, 0x31ce, 0x41a2,
    {0x85, 0xdc, 0x8f, 0xa0, 0xbf, 0x41, 0xb8, 0xda}
};

// MF_MT_AVG_BITRATE = {20332624-FB0D-4D9E-BD0D-CBF6786C102E}
static const GUID MY_MF_MT_AVG_BITRATE = {
    0x20332624, 0xfb0d, 0x4d9e,
    {0xbd, 0x0d, 0xcb, 0xf6, 0x78, 0x6c, 0x10, 0x2e}
};

// MF_MT_INTERLACE_MODE = {E2724BB8-E676-4806-B4B2-A8D6EFB44CCD}
static const GUID MY_MF_MT_INTERLACE_MODE = {
    0xe2724bb8, 0xe676, 0x4806,
    {0xb4, 0xb2, 0xa8, 0xd6, 0xef, 0xb4, 0x4c, 0xcd}
};

// CODECAPI_AVEncCommonRateControlMode = {1C0608E9-370C-4710-8A58-CB6181C42423}
static const GUID MY_CODECAPI_AVEncCommonRateControlMode = {
    0x1c0608e9, 0x370c, 0x4710,
    {0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23}
};

// CODECAPI_AVEncMPVGOPSize = {95F31B26-95A4-41AE-9368-6480538D962D}
static const GUID MY_CODECAPI_AVEncMPVGOPSize = {
    0x95f31b26, 0x95a4, 0x41ae,
    {0x93, 0x68, 0x64, 0x80, 0x53, 0x8d, 0x96, 0x2d}
};

// CODECAPI_AVLowLatencyMode = {9C27891A-ED7A-40E1-88E8-B22727A024EE}
static const GUID MY_CODECAPI_AVLowLatencyMode = {
    0x9c27891a, 0xed7a, 0x40e1,
    {0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee}
};

// CODECAPI_AVEncVideoForceKeyFrame = {398C1B98-8353-475A-9EF2-8F265D260345}
static const GUID MY_CODECAPI_AVEncVideoForceKeyFrame = {
    0x398c1b98, 0x8353, 0x475a,
    {0x9e, 0xf2, 0x8f, 0x26, 0x5d, 0x26, 0x03, 0x45}
};

// MFSampleExtension_CleanPoint = {9CDF01D8-A0F0-43BA-B077-EAA06CBD728A}
static const GUID MY_MFSampleExtension_CleanPoint = {
    0x9cdf01d8, 0xa0f0, 0x43ba,
    {0xb0, 0x77, 0xea, 0xa0, 0x6c, 0xbd, 0x72, 0x8a}
};

// MF_TRANSFORM_ASYNC_UNLOCK = {E5666D6B-3422-4EB6-A421-DA7DB1F8E207}
static const GUID MY_MF_TRANSFORM_ASYNC_UNLOCK = {
    0xe5666d6b, 0x3422, 0x4eb6,
    {0xa4, 0x21, 0xda, 0x7d, 0xb1, 0xf8, 0xe2, 0x07}
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

    // Latest JPEG frame buffer
    uint8_t* jpegBuffer;
    int jpegSize;
    volatile int frameReady;

    // Device index
    int deviceIndex;

    // H.265/H.264 encoder state
    IMFTransform* pEncoder;
    ICodecAPI* pCodecAPI;
    uint8_t* h265Buffer;         // Compressed video output buffer
    int h265Size;
    volatile int h265Ready;
    int h265IsKeyFrame;
    int h265Active;              // 1 = encoder running
    int h265CodecType;           // 0=none, 1=H265, 2=H264
    int h265Bitrate;
    GUID h265InputSubtype;       // NV12 or YUY2
    volatile int h265ForceKey;   // 1 = force next frame to be I-frame
    LONGLONG h265FrameIndex;     // Frame counter for timestamps
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
// Resolution Enumeration
// ============================================================================

// Cached resolution list
#define MAX_RESOLUTIONS 64
typedef struct {
    int width;
    int height;
    int fps;
} ResolutionInfo;

static ResolutionInfo s_resolutions[MAX_RESOLUTIONS];
static int s_resolutionCount = 0;
static char s_resBuf[64];  // For returning resolution string

FFI_PLUGIN_EXPORT
int getCameraResolutionCount(int deviceIndex) {
    IMFActivate** ppDevices = NULL;
    UINT32 count = 0;
    BOOL shouldUninit = FALSE;
    s_resolutionCount = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    shouldUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 0;

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { if (shouldUninit) CoUninitialize(); return 0; }

    hr = enumerate_devices(&ppDevices, &count);
    if (FAILED(hr) || (UINT32)deviceIndex >= count) {
        free_device_list(ppDevices, count);
        MFShutdown(); if (shouldUninit) CoUninitialize();
        return 0;
    }

    // Activate device and create source reader
    IMFMediaSource* pSource = NULL;
    hr = ppDevices[deviceIndex]->lpVtbl->ActivateObject(
        ppDevices[deviceIndex], &MY_IID_IMFMediaSource, (void**)&pSource);
    if (FAILED(hr)) {
        free_device_list(ppDevices, count);
        MFShutdown(); if (shouldUninit) CoUninitialize();
        return 0;
    }

    IMFSourceReader* pReader = NULL;
    hr = MFCreateSourceReaderFromMediaSource(pSource, NULL, &pReader);
    if (FAILED(hr)) {
        pSource->lpVtbl->Shutdown(pSource);
        pSource->lpVtbl->Release(pSource);
        free_device_list(ppDevices, count);
        MFShutdown(); if (shouldUninit) CoUninitialize();
        return 0;
    }

    // Enumerate all native media types
    int resCount = 0;
    for (DWORD typeIndex = 0; typeIndex < 200; typeIndex++) {
        IMFMediaType* pType = NULL;
        hr = pReader->lpVtbl->GetNativeMediaType(pReader,
            MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, typeIndex, &pType);
        if (FAILED(hr)) break;  // No more types

        UINT64 frameSize = 0;
        pType->lpVtbl->GetUINT64(pType, &MY_MF_MT_FRAME_SIZE, &frameSize);
        int w = (int)(frameSize >> 32);
        int h = (int)(frameSize & 0xFFFFFFFF);

        UINT64 frameRate = 0;
        pType->lpVtbl->GetUINT64(pType, &MY_MF_MT_FRAME_RATE, &frameRate);
        int fpsNum = (int)(frameRate >> 32);
        int fpsDen = (int)(frameRate & 0xFFFFFFFF);
        int fps = (fpsDen > 0) ? (fpsNum / fpsDen) : 0;

        pType->lpVtbl->Release(pType);

        if (w <= 0 || h <= 0) continue;

        // De-duplicate (same w,h,fps already exists?)
        int found = 0;
        for (int i = 0; i < resCount; i++) {
            if (s_resolutions[i].width == w && s_resolutions[i].height == h && s_resolutions[i].fps == fps) {
                found = 1;
                break;
            }
        }
        if (!found && resCount < MAX_RESOLUTIONS) {
            s_resolutions[resCount].width = w;
            s_resolutions[resCount].height = h;
            s_resolutions[resCount].fps = fps;
            resCount++;
        }
    }

    s_resolutionCount = resCount;

    // Sort by resolution (descending: largest first)
    for (int i = 0; i < resCount - 1; i++) {
        for (int j = i + 1; j < resCount; j++) {
            int pixI = s_resolutions[i].width * s_resolutions[i].height;
            int pixJ = s_resolutions[j].width * s_resolutions[j].height;
            if (pixJ > pixI || (pixJ == pixI && s_resolutions[j].fps > s_resolutions[i].fps)) {
                ResolutionInfo tmp = s_resolutions[i];
                s_resolutions[i] = s_resolutions[j];
                s_resolutions[j] = tmp;
            }
        }
    }

    pReader->lpVtbl->Release(pReader);
    pSource->lpVtbl->Shutdown(pSource);
    pSource->lpVtbl->Release(pSource);
    free_device_list(ppDevices, count);
    MFShutdown();
    if (shouldUninit) CoUninitialize();

    LOG("getCameraResolutionCount: device=%d, found %d resolutions\n", deviceIndex, resCount);
    return resCount;
}

/// Get resolution info as string "WIDTHxHEIGHT@FPS" at given index.
/// Must call getCameraResolutionCount() first to populate the list.
FFI_PLUGIN_EXPORT
const char* getCameraResolution(int resIndex) {
    s_resBuf[0] = '\0';
    if (resIndex < 0 || resIndex >= s_resolutionCount) return s_resBuf;
    sprintf_s(s_resBuf, sizeof(s_resBuf), "%dx%d@%d",
        s_resolutions[resIndex].width,
        s_resolutions[resIndex].height,
        s_resolutions[resIndex].fps);
    return s_resBuf;
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
// H.265/H.264 Encoder (via MFT - Media Foundation Transform)
// ============================================================================

/// Calculate auto bitrate based on resolution
static int auto_bitrate(int width, int height) {
    int pixels = width * height;
    if (pixels >= 1920 * 1080) return 3000000;  // 3 Mbps for 1080p
    if (pixels >= 1280 * 720)  return 1500000;  // 1.5 Mbps for 720p
    return 500000;                               // 500 Kbps for 480p and below
}

/// Try to create and configure MFT encoder (H.265 first, then H.264 fallback)
static int init_video_encoder(CaptureState* state, int width, int height, int fps, GUID inputSubtype, int bitrate) {
    HRESULT hr;
    IMFActivate** ppActivate = NULL;
    UINT32 count = 0;
    const GUID* codecSubtype = &MY_MFVideoFormat_HEVC;
    int codecType = 1; // 1=H265

    if (bitrate <= 0) bitrate = auto_bitrate(width, height);

    LOG("init_video_encoder: %dx%d@%dfps, bitrate=%d, trying H.265...\n", width, height, fps, bitrate);

    // Try H.265 first
    MFT_REGISTER_TYPE_INFO outputInfo = {0};
    memcpy(&outputInfo.guidMajorType, &MY_MFMediaType_Video, sizeof(GUID));
    memcpy(&outputInfo.guidSubtype, &MY_MFVideoFormat_HEVC, sizeof(GUID));

    hr = MFTEnumEx(
        MFT_CATEGORY_VIDEO_ENCODER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        NULL, &outputInfo, &ppActivate, &count);

    if (FAILED(hr) || count == 0) {
        LOG("init_video_encoder: No H.265 encoder, trying H.264...\n", 0);
        if (ppActivate) CoTaskMemFree(ppActivate);
        ppActivate = NULL;
        count = 0;

        // Fallback to H.264
        memcpy(&outputInfo.guidSubtype, &MY_MFVideoFormat_H264, sizeof(GUID));
        codecSubtype = &MY_MFVideoFormat_H264;
        codecType = 2; // 2=H264

        hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            NULL, &outputInfo, &ppActivate, &count);

        if (FAILED(hr) || count == 0) {
            LOG("init_video_encoder: No H.264 encoder either, giving up\n", 0);
            if (ppActivate) CoTaskMemFree(ppActivate);
            return -1;
        }
    }

    LOG("init_video_encoder: Found %u encoders, codec=%s\n", count, codecType == 1 ? "H265" : "H264");

    // Activate the first encoder
    IMFTransform* pEncoder = NULL;
    hr = ppActivate[0]->lpVtbl->ActivateObject(ppActivate[0], &MY_IID_IMFTransform, (void**)&pEncoder);
    for (UINT32 i = 0; i < count; i++) ppActivate[i]->lpVtbl->Release(ppActivate[i]);
    CoTaskMemFree(ppActivate);

    if (FAILED(hr) || !pEncoder) {
        LOG("init_video_encoder: ActivateObject failed: 0x%08X\n", hr);
        return -1;
    }

    // ===== Set OUTPUT type FIRST (required by MFT) =====
    IMFMediaType* pOutputType = NULL;
    MFCreateMediaType(&pOutputType);
    pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_MAJOR_TYPE, &MY_MFMediaType_Video);
    pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_SUBTYPE, codecSubtype);
    MFSetAttributeSize((IMFAttributes*)pOutputType, &MY_MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio((IMFAttributes*)pOutputType, &MY_MF_MT_FRAME_RATE, fps, 1);
    pOutputType->lpVtbl->SetUINT32(pOutputType, &MY_MF_MT_AVG_BITRATE, (UINT32)bitrate);
    pOutputType->lpVtbl->SetUINT32(pOutputType, &MY_MF_MT_INTERLACE_MODE, 2); // MFVideoInterlace_Progressive

    hr = pEncoder->lpVtbl->SetOutputType(pEncoder, 0, pOutputType, 0);
    pOutputType->lpVtbl->Release(pOutputType);
    if (FAILED(hr)) {
        LOG("init_video_encoder: SetOutputType failed: 0x%08X\n", hr);
        pEncoder->lpVtbl->Release(pEncoder);
        return -1;
    }

    // ===== Set INPUT type =====
    IMFMediaType* pInputType = NULL;
    MFCreateMediaType(&pInputType);
    pInputType->lpVtbl->SetGUID(pInputType, &MY_MF_MT_MAJOR_TYPE, &MY_MFMediaType_Video);
    pInputType->lpVtbl->SetGUID(pInputType, &MY_MF_MT_SUBTYPE, &inputSubtype);
    MFSetAttributeSize((IMFAttributes*)pInputType, &MY_MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio((IMFAttributes*)pInputType, &MY_MF_MT_FRAME_RATE, fps, 1);
    pInputType->lpVtbl->SetUINT32(pInputType, &MY_MF_MT_INTERLACE_MODE, 2);

    hr = pEncoder->lpVtbl->SetInputType(pEncoder, 0, pInputType, 0);
    pInputType->lpVtbl->Release(pInputType);
    if (FAILED(hr)) {
        LOG("init_video_encoder: SetInputType failed: 0x%08X (subtype may not be supported)\n", hr);
        // Try NV12 as alternative input
        if (!IsEqualGUID(&inputSubtype, &MY_MFVideoFormat_NV12)) {
            LOG("init_video_encoder: Retrying with NV12 input...\n", 0);
            MFCreateMediaType(&pInputType);
            pInputType->lpVtbl->SetGUID(pInputType, &MY_MF_MT_MAJOR_TYPE, &MY_MFMediaType_Video);
            pInputType->lpVtbl->SetGUID(pInputType, &MY_MF_MT_SUBTYPE, &MY_MFVideoFormat_NV12);
            MFSetAttributeSize((IMFAttributes*)pInputType, &MY_MF_MT_FRAME_SIZE, width, height);
            MFSetAttributeRatio((IMFAttributes*)pInputType, &MY_MF_MT_FRAME_RATE, fps, 1);
            pInputType->lpVtbl->SetUINT32(pInputType, &MY_MF_MT_INTERLACE_MODE, 2);
            hr = pEncoder->lpVtbl->SetInputType(pEncoder, 0, pInputType, 0);
            pInputType->lpVtbl->Release(pInputType);
            if (FAILED(hr)) {
                LOG("init_video_encoder: NV12 input also failed: 0x%08X\n", hr);
                pEncoder->lpVtbl->Release(pEncoder);
                return -1;
            }
            memcpy(&state->h265InputSubtype, &MY_MFVideoFormat_NV12, sizeof(GUID));
        } else {
            pEncoder->lpVtbl->Release(pEncoder);
            return -1;
        }
    } else {
        memcpy(&state->h265InputSubtype, &inputSubtype, sizeof(GUID));
    }

    // ===== Configure via ICodecAPI =====
    ICodecAPI* pCodecAPI = NULL;
    hr = pEncoder->lpVtbl->QueryInterface(pEncoder, &MY_IID_ICodecAPI, (void**)&pCodecAPI);
    if (SUCCEEDED(hr) && pCodecAPI) {
        VARIANT var;
        VariantInit(&var);

        // CBR rate control
        var.vt = VT_UI4;
        var.ulVal = 0; // eAVEncCommonRateControlMode_CBR
        pCodecAPI->lpVtbl->SetValue(pCodecAPI, &MY_CODECAPI_AVEncCommonRateControlMode, &var);

        // GOP size (keyframe interval = 2 seconds)
        var.vt = VT_UI4;
        var.ulVal = fps * 2;
        pCodecAPI->lpVtbl->SetValue(pCodecAPI, &MY_CODECAPI_AVEncMPVGOPSize, &var);

        // Low latency mode (critical for real-time video call)
        var.vt = VT_BOOL;
        var.boolVal = VARIANT_TRUE;
        pCodecAPI->lpVtbl->SetValue(pCodecAPI, &MY_CODECAPI_AVLowLatencyMode, &var);

        LOG("init_video_encoder: ICodecAPI configured (CBR, GOP=%d, low_latency)\n", fps * 2);
    }

    // Notify encoder to start streaming
    hr = pEncoder->lpVtbl->ProcessMessage(pEncoder, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) {
        LOG("init_video_encoder: BEGIN_STREAMING failed: 0x%08X\n", hr);
    }
    hr = pEncoder->lpVtbl->ProcessMessage(pEncoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    // Store in state
    state->pEncoder = pEncoder;
    state->pCodecAPI = pCodecAPI;
    state->h265Active = 1;
    state->h265CodecType = codecType;
    state->h265Bitrate = bitrate;
    state->h265FrameIndex = 0;
    state->h265Buffer = (uint8_t*)malloc(512 * 1024); // 512KB for compressed output
    state->h265Size = 0;
    state->h265Ready = 0;
    state->h265IsKeyFrame = 0;
    state->h265ForceKey = 0;

    LOG("init_video_encoder: SUCCESS! codec=%s, bitrate=%d\n",
        codecType == 1 ? "H.265/HEVC" : "H.264/AVC", bitrate);
    return 0;
}

/// Encode one raw frame via MFT. Outputs to state->h265Buffer.
static int encode_video_frame(CaptureState* state, uint8_t* rawData, DWORD rawSize, int width, int height) {
    if (!state->pEncoder || !state->h265Active) return -1;

    HRESULT hr;

    // Force keyframe if requested
    if (state->h265ForceKey && state->pCodecAPI) {
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_UI4;
        var.ulVal = 1;
        state->pCodecAPI->lpVtbl->SetValue(state->pCodecAPI, &MY_CODECAPI_AVEncVideoForceKeyFrame, &var);
        state->h265ForceKey = 0;
        LOG("encode_video_frame: Forced keyframe\n", 0);
    }

    // Create input buffer
    IMFMediaBuffer* pInBuffer = NULL;
    hr = MFCreateMemoryBuffer(rawSize, &pInBuffer);
    if (FAILED(hr)) return -1;

    BYTE* pData = NULL;
    pInBuffer->lpVtbl->Lock(pInBuffer, &pData, NULL, NULL);
    memcpy(pData, rawData, rawSize);
    pInBuffer->lpVtbl->Unlock(pInBuffer);
    pInBuffer->lpVtbl->SetCurrentLength(pInBuffer, rawSize);

    // Create input sample
    IMFSample* pInSample = NULL;
    MFCreateSample(&pInSample);
    pInSample->lpVtbl->AddBuffer(pInSample, pInBuffer);

    // Set timestamp (100-nanosecond units)
    LONGLONG ts = state->h265FrameIndex * 10000000LL / state->fps;
    LONGLONG dur = 10000000LL / state->fps;
    pInSample->lpVtbl->SetSampleTime(pInSample, ts);
    pInSample->lpVtbl->SetSampleDuration(pInSample, dur);
    state->h265FrameIndex++;

    // Feed to encoder
    hr = state->pEncoder->lpVtbl->ProcessInput(state->pEncoder, 0, pInSample, 0);
    pInSample->lpVtbl->Release(pInSample);
    pInBuffer->lpVtbl->Release(pInBuffer);

    if (FAILED(hr)) {
        if (state->h265FrameIndex <= 2) {
            LOG("encode_video_frame: ProcessInput failed: 0x%08X\n", hr);
        }
        return -1;
    }

    // Try to get output
    MFT_OUTPUT_STREAM_INFO streamInfo = {0};
    state->pEncoder->lpVtbl->GetOutputStreamInfo(state->pEncoder, 0, &streamInfo);

    MFT_OUTPUT_DATA_BUFFER outputData = {0};
    IMFSample* pOutSample = NULL;

    if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        // We need to provide the output sample
        MFCreateSample(&pOutSample);
        IMFMediaBuffer* pOutBuffer = NULL;
        DWORD outBufSize = streamInfo.cbSize > 0 ? streamInfo.cbSize : 512 * 1024;
        MFCreateMemoryBuffer(outBufSize, &pOutBuffer);
        pOutSample->lpVtbl->AddBuffer(pOutSample, pOutBuffer);
        pOutBuffer->lpVtbl->Release(pOutBuffer);
        outputData.pSample = pOutSample;
    }

    DWORD status = 0;
    hr = state->pEncoder->lpVtbl->ProcessOutput(state->pEncoder, 0, 1, &outputData, &status);

    if (hr == ((HRESULT)0xC00D6D72L)) { // MF_E_TRANSFORM_NEED_MORE_INPUT
        // Encoder needs more frames before producing output (normal for first few frames)
        if (pOutSample) pOutSample->lpVtbl->Release(pOutSample);
        return 0;
    }

    if (FAILED(hr)) {
        if (state->h265FrameIndex <= 5) {
            LOG("encode_video_frame: ProcessOutput failed: 0x%08X\n", hr);
        }
        if (pOutSample) pOutSample->lpVtbl->Release(pOutSample);
        return -1;
    }

    // Extract compressed data
    IMFSample* pResultSample = outputData.pSample;
    if (pResultSample) {
        IMFMediaBuffer* pEncodedBuf = NULL;
        hr = pResultSample->lpVtbl->ConvertToContiguousBuffer(pResultSample, &pEncodedBuf);
        if (SUCCEEDED(hr)) {
            BYTE* pEncData = NULL;
            DWORD encLen = 0;
            pEncodedBuf->lpVtbl->Lock(pEncodedBuf, &pEncData, NULL, &encLen);

            if (pEncData && encLen > 0 && state->h265Buffer) {
                int copyLen = (int)encLen;
                if (copyLen > 512 * 1024) copyLen = 512 * 1024;

                WaitForSingleObject(state->frameMutex, INFINITE);
                memcpy(state->h265Buffer, pEncData, copyLen);
                state->h265Size = copyLen;
                state->h265Ready = 1;

                // Check if keyframe
                UINT32 isCleanPoint = 0;
                hr = pResultSample->lpVtbl->GetUINT32(
                    (IMFAttributes*)pResultSample, &MY_MFSampleExtension_CleanPoint, &isCleanPoint);
                state->h265IsKeyFrame = (SUCCEEDED(hr) && isCleanPoint) ? 1 : 0;
                ReleaseMutex(state->frameMutex);

                if (state->h265FrameIndex <= 3 || state->h265FrameIndex % 30 == 0) {
                    LOG("encode_video_frame: frame #%lld, encoded=%d bytes, keyframe=%d\n",
                        state->h265FrameIndex, copyLen, state->h265IsKeyFrame);
                }
            }

            pEncodedBuf->lpVtbl->Unlock(pEncodedBuf);
            pEncodedBuf->lpVtbl->Release(pEncodedBuf);
        }
    }

    if (pOutSample && pOutSample != outputData.pSample) pOutSample->lpVtbl->Release(pOutSample);
    if (outputData.pSample) outputData.pSample->lpVtbl->Release(outputData.pSample);

    return 1;
}

/// Cleanup H.265/H.264 encoder
static void cleanup_video_encoder(CaptureState* state) {
    if (state->pEncoder) {
        state->pEncoder->lpVtbl->ProcessMessage(state->pEncoder, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        state->pEncoder->lpVtbl->ProcessMessage(state->pEncoder, MFT_MESSAGE_COMMAND_DRAIN, 0);
        state->pEncoder->lpVtbl->Release(state->pEncoder);
        state->pEncoder = NULL;
    }
    if (state->pCodecAPI) {
        state->pCodecAPI->lpVtbl->Release(state->pCodecAPI);
        state->pCodecAPI = NULL;
    }
    if (state->h265Buffer) {
        free(state->h265Buffer);
        state->h265Buffer = NULL;
    }
    state->h265Active = 0;
    state->h265CodecType = 0;
    state->h265Size = 0;
    state->h265Ready = 0;
    LOG("cleanup_video_encoder: done\n", 0);
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

    // Allocate JPEG output buffer (2MB for high-res)
    int jpegBufSize = 2 * 1024 * 1024;
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

    // Create source reader with video processing enabled
    // This allows MF to convert from camera native format (NV12/YUY2) to RGB24/RGB32
    IMFAttributes* pReaderAttrs = NULL;
    hr = MFCreateAttributes(&pReaderAttrs, 1);
    if (SUCCEEDED(hr)) {
        pReaderAttrs->lpVtbl->SetUINT32(pReaderAttrs,
            &MY_MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        LOG("capture_thread: Video processing enabled\n", 0);
    }

    hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttrs, &pReader);
    if (pReaderAttrs) pReaderAttrs->lpVtbl->Release(pReaderAttrs);
    if (FAILED(hr)) {
        LOG("capture_thread: MFCreateSourceReaderFromMediaSource failed: 0x%08X\n", hr);
        goto exit;
    }
    LOG("capture_thread: SourceReader created ok (with video processing)\n", 0);

    // ========================================================================
    // Step 1: Get native media type to understand camera's format
    // ========================================================================
    IMFMediaType* pNativeType = NULL;
    hr = pReader->lpVtbl->GetNativeMediaType(pReader,
        MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType);
    if (FAILED(hr)) {
        LOG("capture_thread: GetNativeMediaType failed: 0x%08X\n", hr);
        goto exit;
    }

    // Log native format details
    {
        GUID nativeSubtype = {0};
        pNativeType->lpVtbl->GetGUID(pNativeType, &MY_MF_MT_SUBTYPE, &nativeSubtype);
        LOG("capture_thread: Native subtype: %08X-%04X-%04X\n",
            nativeSubtype.Data1, nativeSubtype.Data2, nativeSubtype.Data3);

        UINT64 nativeFrameSize = 0;
        pNativeType->lpVtbl->GetUINT64(pNativeType, &MY_MF_MT_FRAME_SIZE, &nativeFrameSize);
        int nw = (int)(nativeFrameSize >> 32);
        int nh = (int)(nativeFrameSize & 0xFFFFFFFF);
        LOG("capture_thread: Native resolution: %dx%d\n", nw, nh);
    }

    // ========================================================================
    // Step 2: Try to set output format to RGB32 (copy native type, change subtype)
    // ========================================================================
    int useNativeFormat = 0;  // 0 = RGB output, 1 = native NV12/YUY2

    // Method: Copy all attributes from native type, just change subtype
    hr = MFCreateMediaType(&pOutputType);
    if (FAILED(hr)) {
        LOG("capture_thread: MFCreateMediaType failed: 0x%08X\n", hr);
        goto exit;
    }

    // Copy all attributes from native type
    pNativeType->lpVtbl->CopyAllItems(pNativeType, (IMFAttributes*)pOutputType);

    // Try RGB32 first (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING supports YUV→RGB32)
    pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_SUBTYPE, &MY_MFVideoFormat_RGB32);
    hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
        MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pOutputType);
    if (SUCCEEDED(hr)) {
        LOG("capture_thread: Using RGB32 output (converted from native)\n", 0);
    } else {
        LOG("capture_thread: RGB32 failed: 0x%08X, trying RGB24...\n", hr);

        // Try RGB24
        pOutputType->lpVtbl->SetGUID(pOutputType, &MY_MF_MT_SUBTYPE, &MY_MFVideoFormat_RGB24);
        hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
            MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pOutputType);
        if (SUCCEEDED(hr)) {
            LOG("capture_thread: Using RGB24 output (converted from native)\n", 0);
        } else {
            LOG("capture_thread: RGB24 also failed: 0x%08X, using native format\n", hr);
            useNativeFormat = 1;

            // Reset to native format
            hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
                MY_MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pNativeType);
            if (FAILED(hr)) {
                LOG("capture_thread: Setting native type also failed: 0x%08X\n", hr);
                goto exit;
            }
            LOG("capture_thread: Using native format (will convert manually)\n", 0);
        }
    }

    if (pNativeType) { pNativeType->lpVtbl->Release(pNativeType); pNativeType = NULL; }

    // ========================================================================
    // Step 3: Read actual resolution from current media type
    // ========================================================================
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

            // Get actual subtype for logging
            GUID actualSubtype = {0};
            pActualType->lpVtbl->GetGUID(pActualType, &MY_MF_MT_SUBTYPE, &actualSubtype);
            LOG("capture_thread: Actual subtype: %08X, resolution: %dx%d\n",
                actualSubtype.Data1, actualWidth, actualHeight);

            pActualType->lpVtbl->Release(pActualType);
        }
    }

    state->width = actualWidth;
    state->height = actualHeight;

    // Allocate RGB conversion buffer if using native format
    uint8_t* rgbBuffer = NULL;
    if (useNativeFormat) {
        rgbBuffer = (uint8_t*)malloc(actualWidth * actualHeight * 3);
        if (!rgbBuffer) {
            LOG("capture_thread: Failed to allocate RGB conversion buffer\n", 0);
            goto exit;
        }
    }

    LOG("capture_thread: CAPTURE LOOP STARTING: %dx%d, quality=%d, nativeConvert=%d\n",
        actualWidth, actualHeight, state->jpegQuality, useNativeFormat);

    // ========================================================================
    // Step 3.5: Initialize H.265/H.264 encoder
    // ========================================================================
    {
        // Determine native format for encoder input
        GUID nativeSubtype = MY_MFVideoFormat_NV12; // default
        if (useNativeFormat) {
            DWORD expectedYUY2 = (DWORD)(actualWidth * actualHeight * 2);
            // We'll detect actual format in the first frame, use YUY2 as likely default
            nativeSubtype = MY_MFVideoFormat_YUY2;
        }
        int encResult = init_video_encoder(state, actualWidth, actualHeight, state->fps, nativeSubtype, 0);
        if (encResult != 0) {
            LOG("capture_thread: H.265/H.264 encoder not available, JPEG only\n", 0);
        }
    }

    // ========================================================================
    // Step 4: Capture loop
    // ========================================================================
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
                    // === H.265/H.264 encode (raw YUV data before RGB conversion) ===
                    if (state->h265Active && useNativeFormat) {
                        encode_video_frame(state, pBits, curLen, actualWidth, actualHeight);
                    }

                    BYTE* rgbData = pBits;
                    int rgbWidth = actualWidth;
                    int rgbHeight = actualHeight;

                    // If native format, convert YUY2/NV12 to RGB24 (flipped vertically for WIC)
                    if (useNativeFormat && rgbBuffer) {
                        DWORD expectedYUY2 = (DWORD)(rgbWidth * rgbHeight * 2);
                        DWORD expectedNV12 = (DWORD)(rgbWidth * rgbHeight * 3 / 2);

                        // Check YUY2 first (curLen == w*h*2 = 614400 for 640x480)
                        if (curLen >= expectedYUY2) {
                            // YUY2 to RGB24 conversion (flip vertical)
                            for (int y = 0; y < rgbHeight; y++) {
                                int outY = rgbHeight - 1 - y;  // flip
                                for (int x = 0; x < rgbWidth; x += 2) {
                                    int srcIdx = (y * rgbWidth + x) * 2;
                                    int Y0 = pBits[srcIdx];
                                    int U  = pBits[srcIdx + 1] - 128;
                                    int Y1 = pBits[srcIdx + 2];
                                    int V  = pBits[srcIdx + 3] - 128;

                                    int R = Y0 + ((359 * V) >> 8);
                                    int G = Y0 - ((88 * U + 183 * V) >> 8);
                                    int B = Y0 + ((454 * U) >> 8);
                                    if (R < 0) R = 0; if (R > 255) R = 255;
                                    if (G < 0) G = 0; if (G > 255) G = 255;
                                    if (B < 0) B = 0; if (B > 255) B = 255;

                                    int outIdx = (outY * rgbWidth + x) * 3;
                                    rgbBuffer[outIdx + 0] = (uint8_t)B;
                                    rgbBuffer[outIdx + 1] = (uint8_t)G;
                                    rgbBuffer[outIdx + 2] = (uint8_t)R;

                                    R = Y1 + ((359 * V) >> 8);
                                    G = Y1 - ((88 * U + 183 * V) >> 8);
                                    B = Y1 + ((454 * U) >> 8);
                                    if (R < 0) R = 0; if (R > 255) R = 255;
                                    if (G < 0) G = 0; if (G > 255) G = 255;
                                    if (B < 0) B = 0; if (B > 255) B = 255;

                                    rgbBuffer[outIdx + 3] = (uint8_t)B;
                                    rgbBuffer[outIdx + 4] = (uint8_t)G;
                                    rgbBuffer[outIdx + 5] = (uint8_t)R;
                                }
                            }
                            rgbData = rgbBuffer;
                            if (frameCount == 0) {
                                LOG("capture_thread: YUY2 conversion (curLen=%u)\n", curLen);
                            }
                        } else if (curLen >= expectedNV12) {
                            // NV12 to RGB24 conversion (flip vertical)
                            const uint8_t* yPlane = pBits;
                            const uint8_t* uvPlane = pBits + rgbWidth * rgbHeight;

                            for (int y = 0; y < rgbHeight; y++) {
                                int outY = rgbHeight - 1 - y;  // flip
                                for (int x = 0; x < rgbWidth; x++) {
                                    int yIdx = y * rgbWidth + x;
                                    int uvIdx = (y / 2) * rgbWidth + (x & ~1);

                                    int Y = yPlane[yIdx];
                                    int U = uvPlane[uvIdx] - 128;
                                    int V = uvPlane[uvIdx + 1] - 128;

                                    int R = Y + ((359 * V) >> 8);
                                    int G = Y - ((88 * U + 183 * V) >> 8);
                                    int B = Y + ((454 * U) >> 8);

                                    if (R < 0) R = 0; if (R > 255) R = 255;
                                    if (G < 0) G = 0; if (G > 255) G = 255;
                                    if (B < 0) B = 0; if (B > 255) B = 255;

                                    int outIdx = (outY * rgbWidth + x) * 3;
                                    rgbBuffer[outIdx + 0] = (uint8_t)B;
                                    rgbBuffer[outIdx + 1] = (uint8_t)G;
                                    rgbBuffer[outIdx + 2] = (uint8_t)R;
                                }
                            }
                            rgbData = rgbBuffer;
                            if (frameCount == 0) {
                                LOG("capture_thread: NV12 conversion (curLen=%u)\n", curLen);
                            }
                        } else {
                            if (frameCount == 0) {
                                LOG("capture_thread: Unknown native size: curLen=%u (nv12=%u, yuy2=%u)\n",
                                    curLen, expectedNV12, expectedYUY2);
                            }
                        }
                    }

                    // Encode to JPEG
                    int jpegLen = encode_rgb_to_jpeg(
                        rgbData, rgbWidth, rgbHeight,
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

    free(rgbBuffer);

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
    cleanup_video_encoder(state);
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
    g_capture.jpegBuffer = (uint8_t*)malloc(2 * 1024 * 1024);  // 2MB for high-res JPEG
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

// ============================================================================
// H.265/H.264 Encoder API
// ============================================================================

FFI_PLUGIN_EXPORT
int getLatestH265Frame(uint8_t* buffer, int bufferSize) {
    if (!g_capture.h265Active || !g_capture.h265Ready) return 0;

    int copied = 0;
    WaitForSingleObject(g_capture.frameMutex, 100);
    if (g_capture.h265Ready && g_capture.h265Buffer && g_capture.h265Size > 0) {
        int copyLen = g_capture.h265Size;
        if (copyLen > bufferSize) copyLen = bufferSize;
        memcpy(buffer, g_capture.h265Buffer, copyLen);
        copied = copyLen;
        g_capture.h265Ready = 0;
    }
    ReleaseMutex(g_capture.frameMutex);
    return copied;
}

FFI_PLUGIN_EXPORT
int isH265KeyFrame(void) {
    return g_capture.h265IsKeyFrame;
}

FFI_PLUGIN_EXPORT
int getH265CodecType(void) {
    return g_capture.h265CodecType; // 0=none, 1=H265, 2=H264
}

FFI_PLUGIN_EXPORT
void forceH265KeyFrame(void) {
    g_capture.h265ForceKey = 1;
}
