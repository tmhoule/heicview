#include "frame_decode.h"
#include "com_counter.h"
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HeicFrameDecode::HeicFrameDecode() {
    InitializeCriticalSection(&m_cs);
    COMCounter::ObjectCreated();
}

HeicFrameDecode::~HeicFrameDecode() {
    if (m_metadataReader) {
        m_metadataReader->Release();
        m_metadataReader = nullptr;
    }
    DeleteCriticalSection(&m_cs);
    COMCounter::ObjectDestroyed();
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown ||
            riid == IID_IWICBitmapSource ||
            riid == IID_IWICBitmapFrameDecode) {
            *ppv = static_cast<IWICBitmapFrameDecode*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IWICMetadataBlockReader && m_metadataReader) {
            *ppv = static_cast<IWICMetadataBlockReader*>(m_metadataReader);
            m_metadataReader->AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP_(ULONG) HeicFrameDecode::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) HeicFrameDecode::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

// ---------------------------------------------------------------------------
// Initialize — takes ownership of the heif_image_handle
// ---------------------------------------------------------------------------

HRESULT HeicFrameDecode::Initialize(heif_image_handle* handle) {
    try {
        if (!handle) return E_INVALIDARG;
        return DecodeImage(handle);
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// DecodeImage — core: decode HEIC to BGRA32 pixels
// ---------------------------------------------------------------------------

HRESULT HeicFrameDecode::DecodeImage(heif_image_handle* handle) {
    // 1. Decode to RGBA interleaved
    //    libheif applies EXIF orientation by default (ignore_transformations=false),
    //    so output is already correctly oriented.
    heif_image* img = nullptr;
    heif_error err = heif_decode_image(handle, &img,
                                        heif_colorspace_RGB,
                                        heif_chroma_interleaved_RGBA,
                                        nullptr);
    if (err.code != heif_error_Ok) {
        heif_image_handle_release(handle);
        return WINCODEC_ERR_BADIMAGE;
    }

    // 2. Get pixel data
    int srcStride = 0;
    const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &srcStride);
    if (!data) {
        heif_image_release(img);
        heif_image_handle_release(handle);
        return WINCODEC_ERR_BADIMAGE;
    }

    // 3. Get post-orientation dimensions from decoded image
    m_width  = static_cast<UINT>(heif_image_get_width(img, heif_channel_interleaved));
    m_height = static_cast<UINT>(heif_image_get_height(img, heif_channel_interleaved));

    // 4. Convert RGBA to BGRA (swap R and B channels)
    m_pixels.resize(static_cast<size_t>(m_width) * m_height * 4);

    for (UINT y = 0; y < m_height; y++) {
        const uint8_t* src = data + y * srcStride;
        uint8_t* dst = m_pixels.data() + y * m_width * 4;
        for (UINT x = 0; x < m_width; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2]; // B <- R
            dst[x * 4 + 1] = src[x * 4 + 1]; // G <- G
            dst[x * 4 + 2] = src[x * 4 + 0]; // R <- B
            dst[x * 4 + 3] = src[x * 4 + 3]; // A <- A
        }
    }

    // 5. Free heif_image
    heif_image_release(img);

    // 6. Extract EXIF metadata BEFORE freeing the handle
    m_metadataReader = new(std::nothrow) HeicMetadataBlockReader();
    if (m_metadataReader) {
        m_metadataReader->Initialize(handle);
    }

    // 7. Free heif_image_handle (we took ownership)
    heif_image_handle_release(handle);

    return S_OK;
}

// ---------------------------------------------------------------------------
// IWICBitmapSource — GetSize
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetSize(UINT* pWidth, UINT* pHeight) {
    try {
        if (!pWidth || !pHeight) return E_INVALIDARG;
        *pWidth  = m_width;
        *pHeight = m_height;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapSource — GetPixelFormat
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetPixelFormat(WICPixelFormatGUID* pPixelFormat) {
    try {
        if (!pPixelFormat) return E_INVALIDARG;
        *pPixelFormat = GUID_WICPixelFormat32bppBGRA;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapSource — GetResolution
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetResolution(double* pDpiX, double* pDpiY) {
    try {
        if (!pDpiX || !pDpiY) return E_INVALIDARG;
        *pDpiX = 96.0;
        *pDpiY = 96.0;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapSource — CopyPalette
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::CopyPalette(IWICPalette* /*pIPalette*/) {
    try {
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapSource — CopyPixels
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::CopyPixels(const WICRect* prc, UINT cbStride,
                                          UINT cbBufferSize, BYTE* pbBuffer) {
    try {
        if (!pbBuffer) return E_INVALIDARG;

        WICRect rect = {0, 0, static_cast<INT>(m_width), static_cast<INT>(m_height)};
        if (prc) rect = *prc;

        if (rect.X < 0 || rect.Y < 0 ||
            rect.X + rect.Width > static_cast<INT>(m_width) ||
            rect.Y + rect.Height > static_cast<INT>(m_height))
            return E_INVALIDARG;

        const UINT bytesPerPixel = 4;

        // Validate caller's buffer is large enough
        if (cbBufferSize < static_cast<UINT>(rect.Height - 1) * cbStride +
                           static_cast<UINT>(rect.Width) * bytesPerPixel)
            return E_INVALIDARG;

        const UINT srcStride = m_width * bytesPerPixel;

        for (INT y = 0; y < rect.Height; y++) {
            const BYTE* srcRow = m_pixels.data() +
                                 (rect.Y + y) * srcStride +
                                 rect.X * bytesPerPixel;
            BYTE* dstRow = pbBuffer + y * cbStride;
            memcpy(dstRow, srcRow, rect.Width * bytesPerPixel);
        }
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapFrameDecode — GetMetadataQueryReader
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetMetadataQueryReader(
    IWICMetadataQueryReader** ppIMetadataQueryReader) {
    try {
        if (ppIMetadataQueryReader) *ppIMetadataQueryReader = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapFrameDecode — GetColorContexts
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetColorContexts(UINT /*cCount*/,
                                                IWICColorContext** /*ppIColorContexts*/,
                                                UINT* pcActualCount) {
    try {
        if (pcActualCount) *pcActualCount = 0;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapFrameDecode — GetThumbnail
// ---------------------------------------------------------------------------

STDMETHODIMP HeicFrameDecode::GetThumbnail(IWICBitmapSource** ppIThumbnail) {
    try {
        if (ppIThumbnail) *ppIThumbnail = nullptr;
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}
