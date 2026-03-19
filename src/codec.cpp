#include "codec.h"
#include "frame_decode.h"
#include "com_counter.h"
#include <stdexcept>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HeicDecoder::HeicDecoder() {
    InitializeCriticalSection(&m_cs);
    COMCounter::ObjectCreated();
}

HeicDecoder::~HeicDecoder() {
    if (m_ctx) {
        heif_context_free(m_ctx);
        m_ctx = nullptr;
    }
    DeleteCriticalSection(&m_cs);
    COMCounter::ObjectDestroyed();
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_IWICBitmapDecoder) {
            *ppv = static_cast<IWICBitmapDecoder*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP_(ULONG) HeicDecoder::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) HeicDecoder::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — Initialize
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::Initialize(IStream* pIStream, WICDecodeOptions /*cacheOptions*/) {
    try {
        if (!pIStream) return E_INVALIDARG;
        EnterCriticalSection(&m_cs);

        STATSTG stat = {};
        HRESULT hr = pIStream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) { LeaveCriticalSection(&m_cs); return hr; }

        m_data.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        ULONG bytesRead = 0;
        hr = pIStream->Read(m_data.data(), static_cast<ULONG>(m_data.size()), &bytesRead);
        if (FAILED(hr)) { LeaveCriticalSection(&m_cs); return hr; }

        m_ctx = heif_context_alloc();
        heif_error err = heif_context_read_from_memory_without_copy(
            m_ctx, m_data.data(), m_data.size(), nullptr);
        if (err.code != heif_error_Ok) {
            heif_context_free(m_ctx);
            m_ctx = nullptr;
            LeaveCriticalSection(&m_cs);
            return (err.code == heif_error_Invalid_input) ? WINCODEC_ERR_BADIMAGE
                                                          : WINCODEC_ERR_GENERIC_ERROR;
        }

        m_initialized = true;
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — QueryCapability
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::QueryCapability(IStream* /*pIStream*/, DWORD* pCapability) {
    try {
        if (!pCapability) return E_INVALIDARG;
        *pCapability = WICBitmapDecoderCapabilityCanDecodeAllImages;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — GetContainerFormat
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::GetContainerFormat(GUID* pguidContainerFormat) {
    try {
        if (!pguidContainerFormat) return E_INVALIDARG;
        *pguidContainerFormat = GUID_ContainerFormatHeic;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — GetDecoderInfo
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::GetDecoderInfo(IWICBitmapDecoderInfo** ppIDecoderInfo) {
    try {
        if (!ppIDecoderInfo) return E_INVALIDARG;

        IWICImagingFactory* pFactory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_IWICImagingFactory,
                                      reinterpret_cast<void**>(&pFactory));
        if (FAILED(hr)) return WINCODEC_ERR_UNSUPPORTEDOPERATION;

        IWICComponentInfo* pCompInfo = nullptr;
        hr = pFactory->CreateComponentInfo(CLSID_HeicDecoder, &pCompInfo);
        pFactory->Release();
        if (FAILED(hr)) return hr;

        hr = pCompInfo->QueryInterface(IID_IWICBitmapDecoderInfo,
                                       reinterpret_cast<void**>(ppIDecoderInfo));
        pCompInfo->Release();
        return hr;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — GetFrameCount
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::GetFrameCount(UINT* pCount) {
    try {
        if (!pCount) return E_INVALIDARG;
        if (!m_initialized) return WINCODEC_ERR_NOTINITIALIZED;

        EnterCriticalSection(&m_cs);
        int count = heif_context_get_number_of_top_level_images(m_ctx);
        LeaveCriticalSection(&m_cs);

        *pCount = static_cast<UINT>(count);
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICBitmapDecoder — GetFrame
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::GetFrame(UINT index, IWICBitmapFrameDecode** ppIBitmapFrame) {
    try {
        if (!ppIBitmapFrame) return E_INVALIDARG;
        if (!m_initialized) return WINCODEC_ERR_NOTINITIALIZED;

        EnterCriticalSection(&m_cs);

        heif_image_handle* handle = nullptr;
        heif_error err;

        if (index == 0) {
            err = heif_context_get_primary_image_handle(m_ctx, &handle);
        } else {
            int total = heif_context_get_number_of_top_level_images(m_ctx);
            if (index >= static_cast<UINT>(total)) {
                LeaveCriticalSection(&m_cs);
                return WINCODEC_ERR_FRAMEMISSING;
            }
            std::vector<heif_item_id> ids(static_cast<size_t>(total));
            heif_context_get_list_of_top_level_image_IDs(m_ctx, ids.data(), total);
            err = heif_context_get_image_handle(m_ctx, ids[index], &handle);
        }

        LeaveCriticalSection(&m_cs);

        if (err.code != heif_error_Ok || !handle) return WINCODEC_ERR_BADIMAGE;

        // Create frame decoder — it takes ownership of the handle
        HeicFrameDecode* frame = new (std::nothrow) HeicFrameDecode();
        if (!frame) {
            heif_image_handle_release(handle);
            return E_OUTOFMEMORY;
        }

        HRESULT hr = frame->Initialize(handle);  // handle ownership transferred
        if (FAILED(hr)) {
            frame->Release();
            return hr;
        }

        hr = frame->QueryInterface(IID_IWICBitmapFrameDecode,
                                    reinterpret_cast<void**>(ppIBitmapFrame));
        frame->Release();
        return hr;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// Stubs
// ---------------------------------------------------------------------------

STDMETHODIMP HeicDecoder::CopyPalette(IWICPalette* /*pIPalette*/) {
    try {
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP HeicDecoder::GetMetadataQueryReader(IWICMetadataQueryReader** ppIMetadataQueryReader) {
    try {
        if (ppIMetadataQueryReader) *ppIMetadataQueryReader = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP HeicDecoder::GetPreview(IWICBitmapSource** ppIBitmapSource) {
    try {
        if (ppIBitmapSource) *ppIBitmapSource = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP HeicDecoder::GetColorContexts(UINT /*cCount*/,
                                            IWICColorContext** /*ppIColorContexts*/,
                                            UINT* pcActualCount) {
    try {
        if (pcActualCount) *pcActualCount = 0;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP HeicDecoder::GetThumbnail(IWICBitmapSource** ppIThumbnail) {
    try {
        if (ppIThumbnail) *ppIThumbnail = nullptr;
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}
