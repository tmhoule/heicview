#pragma once
#include "common.h"
#include <libheif/heif.h>
#include <vector>

class HeicDecoder : public IWICBitmapDecoder {
public:
    HeicDecoder();
    ~HeicDecoder();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IWICBitmapDecoder
    STDMETHODIMP QueryCapability(IStream* pIStream, DWORD* pCapability) override;
    STDMETHODIMP Initialize(IStream* pIStream, WICDecodeOptions cacheOptions) override;
    STDMETHODIMP GetContainerFormat(GUID* pguidContainerFormat) override;
    STDMETHODIMP GetDecoderInfo(IWICBitmapDecoderInfo** ppIDecoderInfo) override;
    STDMETHODIMP CopyPalette(IWICPalette* pIPalette) override;
    STDMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader** ppIMetadataQueryReader) override;
    STDMETHODIMP GetPreview(IWICBitmapSource** ppIBitmapSource) override;
    STDMETHODIMP GetColorContexts(UINT cCount, IWICColorContext** ppIColorContexts, UINT* pcActualCount) override;
    STDMETHODIMP GetThumbnail(IWICBitmapSource** ppIThumbnail) override;
    STDMETHODIMP GetFrameCount(UINT* pCount) override;
    STDMETHODIMP GetFrame(UINT index, IWICBitmapFrameDecode** ppIBitmapFrame) override;

private:
    volatile LONG m_refCount = 1;
    heif_context* m_ctx = nullptr;
    std::vector<uint8_t> m_data;
    bool m_initialized = false;
    CRITICAL_SECTION m_cs;
};
