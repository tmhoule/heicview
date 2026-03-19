#include <initguid.h>  // Must come before guiddef.h to define GUID storage
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdlib>

// Define our decoder CLSID — must match src/guids.h exactly
DEFINE_GUID(CLSID_HeicDecoder,
    0xF0A8883C, 0xC9D2, 0x4307, 0xBD, 0xB2, 0xD8, 0x8D, 0xC7, 0x06, 0xEE, 0xC2);

typedef HRESULT(WINAPI* DllGetClassObjectFunc)(REFCLSID, REFIID, LPVOID*);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: test_decode <path-to-heic>\n");
        return 1;
    }

    CoInitialize(nullptr);

    // Load our DLL directly
    HMODULE hDll = LoadLibraryA("heic_wic.dll");
    if (!hDll) {
        fprintf(stderr, "FAIL: Could not load heic_wic.dll (error %lu)\n", GetLastError());
        return 1;
    }

    // Get class factory via DllGetClassObject — no registry needed
    auto getClassObject = (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    if (!getClassObject) {
        fprintf(stderr, "FAIL: DllGetClassObject not found\n");
        return 1;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = getClassObject(CLSID_HeicDecoder, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: DllGetClassObject hr=0x%08lx\n", hr);
        return 1;
    }

    // Create decoder via factory
    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IWICBitmapDecoder, (void**)&pDecoder);
    pFactory->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CreateInstance hr=0x%08lx\n", hr);
        return 1;
    }

    // Create IStream from test file
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath, MAX_PATH);
    IStream* pStream = nullptr;
    hr = SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: SHCreateStreamOnFile hr=0x%08lx\n", hr);
        return 1;
    }

    // Initialize decoder with stream
    hr = pDecoder->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
    pStream->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: Initialize hr=0x%08lx\n", hr);
        return 1;
    }

    UINT frameCount = 0;
    pDecoder->GetFrameCount(&frameCount);
    printf("Frame count: %u\n", frameCount);
    if (frameCount == 0) {
        fprintf(stderr, "FAIL: no frames\n");
        return 1;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: GetFrame hr=0x%08lx\n", hr);
        return 1;
    }

    UINT w = 0, h = 0;
    pFrame->GetSize(&w, &h);
    printf("Image size: %u x %u\n", w, h);
    if (w == 0 || h == 0) {
        fprintf(stderr, "FAIL: zero dimensions\n");
        return 1;
    }

    // Read pixels
    UINT stride = w * 4;
    UINT bufSize = stride * h;
    BYTE* pixels = (BYTE*)malloc(bufSize);
    hr = pFrame->CopyPixels(nullptr, stride, bufSize, pixels);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CopyPixels hr=0x%08lx\n", hr);
        return 1;
    }

    // Check pixel values — print first few pixels and some stats
    UINT nonZero = 0;
    UINT nonWhite = 0;
    for (UINT i = 0; i < bufSize; i++) {
        if (pixels[i] != 0) nonZero++;
        if (pixels[i] != 0xFF) nonWhite++;
    }
    printf("Pixel stats: %u/%u bytes non-zero, %u/%u bytes non-0xFF\n",
           nonZero, bufSize, nonWhite, bufSize);

    // Print first 4 pixels (BGRA)
    UINT pixelsToPrint = (w * h < 4) ? w * h : 4;
    for (UINT i = 0; i < pixelsToPrint; i++) {
        printf("  pixel[%u] = B:%u G:%u R:%u A:%u\n", i,
               pixels[i*4+0], pixels[i*4+1], pixels[i*4+2], pixels[i*4+3]);
    }

    printf("PASS: decoded %u x %u HEIC image (%u bytes)\n", w, h, bufSize);

    free(pixels);
    pFrame->Release();
    pDecoder->Release();
    FreeLibrary(hDll);
    CoUninitialize();
    return 0;
}
