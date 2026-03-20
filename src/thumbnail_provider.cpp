#include "thumbnail_provider.h"
#include "com_counter.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HeicThumbnailProvider::HeicThumbnailProvider() {
    COMCounter::ObjectCreated();
}

HeicThumbnailProvider::~HeicThumbnailProvider() {
    COMCounter::ObjectDestroyed();
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP HeicThumbnailProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown) {
        *ppv = static_cast<IThumbnailProvider*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IThumbnailProvider) {
        *ppv = static_cast<IThumbnailProvider*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IInitializeWithStream) {
        *ppv = static_cast<IInitializeWithStream*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) HeicThumbnailProvider::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) HeicThumbnailProvider::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

// ---------------------------------------------------------------------------
// IInitializeWithStream
// ---------------------------------------------------------------------------

STDMETHODIMP HeicThumbnailProvider::Initialize(IStream* pstream, DWORD /*grfMode*/) {
    try {
        if (!pstream) return E_INVALIDARG;
        if (m_initialized) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

        STATSTG stat = {};
        HRESULT hr = pstream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) return hr;

        m_data.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        ULONG bytesRead = 0;
        hr = pstream->Read(m_data.data(), static_cast<ULONG>(m_data.size()), &bytesRead);
        if (FAILED(hr)) return hr;

        m_initialized = true;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// ---------------------------------------------------------------------------
// IThumbnailProvider
// ---------------------------------------------------------------------------

STDMETHODIMP HeicThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    try {
        if (!phbmp || !pdwAlpha) return E_INVALIDARG;
        if (!m_initialized) return E_FAIL;

        *phbmp = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;

        // 1. Create heif context and parse
        heif_context* ctx = heif_context_alloc();
        if (!ctx) return E_OUTOFMEMORY;

        heif_error err = heif_context_read_from_memory_without_copy(
            ctx, m_data.data(), m_data.size(), nullptr);
        if (err.code != heif_error_Ok) {
            heif_context_free(ctx);
            return E_FAIL;
        }

        // 2. Get primary image handle
        heif_image_handle* handle = nullptr;
        err = heif_context_get_primary_image_handle(ctx, &handle);
        if (err.code != heif_error_Ok || !handle) {
            heif_context_free(ctx);
            return E_FAIL;
        }

        // 3. Try embedded thumbnail first (much faster for large images)
        heif_image_handle* decodeHandle = handle;
        heif_image_handle* thumbHandle = nullptr;
        int numThumbs = heif_image_handle_get_number_of_thumbnails(handle);
        if (numThumbs > 0) {
            heif_item_id thumbId;
            heif_image_handle_get_list_of_thumbnail_IDs(handle, &thumbId, 1);
            err = heif_image_handle_get_thumbnail(handle, thumbId, &thumbHandle);
            if (err.code == heif_error_Ok && thumbHandle) {
                int tw = heif_image_handle_get_width(thumbHandle);
                int th = heif_image_handle_get_height(thumbHandle);
                // Use thumbnail if it's large enough for the requested size
                if (static_cast<UINT>(tw) >= cx || static_cast<UINT>(th) >= cx) {
                    decodeHandle = thumbHandle;
                } else {
                    heif_image_handle_release(thumbHandle);
                    thumbHandle = nullptr;
                }
            }
        }

        // 4. Decode to RGBA
        heif_image* img = nullptr;
        err = heif_decode_image(decodeHandle, &img,
                                heif_colorspace_RGB,
                                heif_chroma_interleaved_RGBA,
                                nullptr);
        if (thumbHandle) heif_image_handle_release(thumbHandle);
        heif_image_handle_release(handle);
        heif_context_free(ctx);

        if (err.code != heif_error_Ok || !img) return E_FAIL;

        int srcStride = 0;
        const uint8_t* srcData = heif_image_get_plane_readonly(img, heif_channel_interleaved, &srcStride);
        if (!srcData) {
            heif_image_release(img);
            return E_FAIL;
        }

        UINT srcW = static_cast<UINT>(heif_image_get_width(img, heif_channel_interleaved));
        UINT srcH = static_cast<UINT>(heif_image_get_height(img, heif_channel_interleaved));

        // 5. Compute thumbnail dimensions (fit within cx x cx, preserve aspect ratio)
        UINT dstW, dstH;
        if (srcW <= cx && srcH <= cx) {
            dstW = srcW;
            dstH = srcH;
        } else if (srcW >= srcH) {
            dstW = cx;
            dstH = (srcH * cx) / srcW;
            if (dstH == 0) dstH = 1;
        } else {
            dstH = cx;
            dstW = (srcW * cx) / srcH;
            if (dstW == 0) dstW = 1;
        }

        // 6. Create HBITMAP via CreateDIBSection (32bpp BGRA, top-down)
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(dstW);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(dstH); // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbmp || !bits) {
            heif_image_release(img);
            return E_FAIL;
        }

        // 7. Scale and convert RGBA -> BGRA into the bitmap
        uint8_t* dstBits = static_cast<uint8_t*>(bits);
        UINT dstStride = dstW * 4;

        for (UINT y = 0; y < dstH; y++) {
            UINT srcY = (y * srcH) / dstH;
            if (srcY >= srcH) srcY = srcH - 1;
            const uint8_t* srcRow = srcData + srcY * srcStride;
            uint8_t* dstRow = dstBits + y * dstStride;

            for (UINT x = 0; x < dstW; x++) {
                UINT srcX = (x * srcW) / dstW;
                if (srcX >= srcW) srcX = srcW - 1;
                const uint8_t* sp = srcRow + srcX * 4;

                dstRow[x * 4 + 0] = sp[2]; // B <- R
                dstRow[x * 4 + 1] = sp[1]; // G <- G
                dstRow[x * 4 + 2] = sp[0]; // R <- B
                dstRow[x * 4 + 3] = sp[3]; // A <- A
            }
        }

        heif_image_release(img);

        *phbmp = hbmp;
        *pdwAlpha = WTSAT_ARGB;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
