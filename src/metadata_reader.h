#pragma once
#include "common.h"
#include <libheif/heif.h>
#include <vector>

class HeicMetadataBlockReader : public IWICMetadataBlockReader {
public:
    HeicMetadataBlockReader();
    ~HeicMetadataBlockReader();

    HRESULT Initialize(heif_image_handle* handle);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IWICMetadataBlockReader
    STDMETHODIMP GetContainerFormat(GUID* pguidContainerFormat) override;
    STDMETHODIMP GetCount(UINT* pcCount) override;
    STDMETHODIMP GetReaderByIndex(UINT nIndex, IWICMetadataReader** ppIMetadataReader) override;
    STDMETHODIMP GetEnumerator(IEnumUnknown** ppIEnumMetadata) override;

private:
    volatile LONG m_refCount = 1;
    std::vector<std::vector<uint8_t>> m_metadataBlocks;
};
