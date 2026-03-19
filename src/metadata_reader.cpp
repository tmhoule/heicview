#include "metadata_reader.h"
#include "com_counter.h"
#include <shlwapi.h>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HeicMetadataBlockReader::HeicMetadataBlockReader() {
    COMCounter::ObjectCreated();
}

HeicMetadataBlockReader::~HeicMetadataBlockReader() {
    COMCounter::ObjectDestroyed();
}

// ---------------------------------------------------------------------------
// Initialize — reads EXIF metadata blocks from the handle
// NOTE: Does NOT take ownership of handle; caller frees it.
// ---------------------------------------------------------------------------

HRESULT HeicMetadataBlockReader::Initialize(heif_image_handle* handle) {
    try {
        if (!handle) return E_INVALIDARG;

        // Count EXIF blocks
        int count = heif_image_handle_get_number_of_metadata_blocks(handle, "Exif");
        if (count <= 0) return S_OK;  // No EXIF — not an error

        // Retrieve block IDs
        std::vector<heif_item_id> ids(static_cast<size_t>(count));
        int fetched = heif_image_handle_get_list_of_metadata_block_IDs(
            handle, "Exif", ids.data(), count);

        for (int i = 0; i < fetched; i++) {
            size_t sz = heif_image_handle_get_metadata_size(handle, ids[i]);
            if (sz == 0) continue;

            std::vector<uint8_t> block(sz);
            heif_error err = heif_image_handle_get_metadata(
                handle, ids[i], block.data());
            if (err.code != heif_error_Ok) continue;

            m_metadataBlocks.push_back(std::move(block));
        }
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP HeicMetadataBlockReader::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown ||
            riid == IID_IWICMetadataBlockReader) {
            *ppv = static_cast<IWICMetadataBlockReader*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

STDMETHODIMP_(ULONG) HeicMetadataBlockReader::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) HeicMetadataBlockReader::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

// ---------------------------------------------------------------------------
// IWICMetadataBlockReader — GetContainerFormat
// ---------------------------------------------------------------------------

STDMETHODIMP HeicMetadataBlockReader::GetContainerFormat(GUID* pguidContainerFormat) {
    try {
        if (!pguidContainerFormat) return E_INVALIDARG;
        *pguidContainerFormat = GUID_ContainerFormatHeic;
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICMetadataBlockReader — GetCount
// ---------------------------------------------------------------------------

STDMETHODIMP HeicMetadataBlockReader::GetCount(UINT* pcCount) {
    try {
        if (!pcCount) return E_INVALIDARG;
        *pcCount = static_cast<UINT>(m_metadataBlocks.size());
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICMetadataBlockReader — GetReaderByIndex
// ---------------------------------------------------------------------------

STDMETHODIMP HeicMetadataBlockReader::GetReaderByIndex(UINT nIndex,
                                                        IWICMetadataReader** ppIMetadataReader) {
    try {
        if (nIndex >= m_metadataBlocks.size()) return E_INVALIDARG;
        if (!ppIMetadataReader) return E_INVALIDARG;

        // EXIF data from HEIF has a 4-byte TIFF header offset prefix
        // before the actual EXIF APP1 data. Skip these 4 bytes.
        const auto& block = m_metadataBlocks[nIndex];
        if (block.size() <= 4) return WINCODEC_ERR_BADIMAGE;

        IStream* pStream = SHCreateMemStream(block.data() + 4,
                                             static_cast<UINT>(block.size() - 4));
        if (!pStream) return E_OUTOFMEMORY;

        IWICImagingFactory* pFactory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
        if (SUCCEEDED(hr)) {
            IWICComponentFactory* pComponentFactory = nullptr;
            hr = pFactory->QueryInterface(IID_PPV_ARGS(&pComponentFactory));
            if (SUCCEEDED(hr)) {
                hr = pComponentFactory->CreateMetadataReaderFromContainer(
                    GUID_MetadataFormatExif, nullptr,
                    WICPersistOptionDefault, pStream, ppIMetadataReader);
                pComponentFactory->Release();
            }
            pFactory->Release();
        }
        pStream->Release();
        return hr;
    }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}

// ---------------------------------------------------------------------------
// IWICMetadataBlockReader — GetEnumerator
// ---------------------------------------------------------------------------

STDMETHODIMP HeicMetadataBlockReader::GetEnumerator(IEnumUnknown** ppIEnumMetadata) {
    try {
        if (ppIEnumMetadata) *ppIEnumMetadata = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}
