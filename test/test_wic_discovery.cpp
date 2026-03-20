#include <initguid.h>
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <cstdio>

// Test WIC codec discovery — does WIC find our decoder via pattern matching?
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: test_wic_discovery <path-to-heic>\n");
        return 1;
    }

    CoInitialize(nullptr);

    // Create WIC factory
    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CoCreateInstance WICImagingFactory hr=0x%08lx\n", hr);
        return 1;
    }
    printf("WIC factory created OK\n");

    // Try CreateDecoderFromFilename — this uses pattern matching
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath, MAX_PATH);

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        wpath, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &pDecoder);

    if (FAILED(hr)) {
        fprintf(stderr, "CreateDecoderFromFilename FAILED: hr=0x%08lx\n", hr);

        // Try CreateDecoderFromStream as fallback
        printf("Trying CreateDecoderFromStream...\n");
        IStream* pStream = nullptr;
        hr = SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream);
        if (FAILED(hr)) {
            fprintf(stderr, "FAIL: SHCreateStreamOnFile hr=0x%08lx\n", hr);
            return 1;
        }

        hr = pFactory->CreateDecoderFromStream(
            pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
        pStream->Release();

        if (FAILED(hr)) {
            fprintf(stderr, "CreateDecoderFromStream FAILED: hr=0x%08lx\n", hr);

            // Enumerate all available decoders
            printf("\nEnumerating registered WIC decoders:\n");
            IEnumUnknown* pEnum = nullptr;
            IWICImagingFactory2* pFactory2 = nullptr;
            // Use component enumeration
            IWICComponentInfo* pInfo = nullptr;
            CLSID decoderClsid = {0xF0A8883C, 0xC9D2, 0x4307,
                {0xBD, 0xB2, 0xD8, 0x8D, 0xC7, 0x06, 0xEE, 0xC2}};
            hr = pFactory->CreateComponentInfo(decoderClsid, &pInfo);
            if (SUCCEEDED(hr)) {
                printf("CreateComponentInfo for our CLSID: SUCCESS\n");
                IWICBitmapDecoderInfo* pDecoderInfo = nullptr;
                hr = pInfo->QueryInterface(IID_PPV_ARGS(&pDecoderInfo));
                if (SUCCEEDED(hr)) {
                    printf("QueryInterface for IWICBitmapDecoderInfo: SUCCESS\n");

                    // Check if it matches our file
                    IStream* pStream2 = nullptr;
                    SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream2);
                    if (pStream2) {
                        BOOL matches = FALSE;
                        hr = pDecoderInfo->MatchesPattern(pStream2, &matches);
                        printf("MatchesPattern: hr=0x%08lx, matches=%d\n", hr, matches);
                        pStream2->Release();
                    }

                    WCHAR friendlyName[256] = {};
                    UINT len = 0;
                    pDecoderInfo->GetFriendlyName(256, friendlyName, &len);
                    wprintf(L"FriendlyName: %s\n", friendlyName);

                    pDecoderInfo->Release();
                } else {
                    fprintf(stderr, "QueryInterface for DecoderInfo FAILED: 0x%08lx\n", hr);
                }
                pInfo->Release();
            } else {
                fprintf(stderr, "CreateComponentInfo FAILED: 0x%08lx\n", hr);
            }

            pFactory->Release();
            CoUninitialize();
            return 1;
        }
    }

    printf("WIC discovery: SUCCESS\n");

    // Decode
    UINT frameCount = 0;
    pDecoder->GetFrameCount(&frameCount);
    printf("Frame count: %u\n", frameCount);

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        fprintf(stderr, "GetFrame FAILED: hr=0x%08lx\n", hr);
        return 1;
    }

    UINT w = 0, h = 0;
    pFrame->GetSize(&w, &h);
    printf("Image size: %u x %u\n", w, h);

    printf("PASS: WIC discovery and decode successful\n");

    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    CoUninitialize();
    return 0;
}
