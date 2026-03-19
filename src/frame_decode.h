#pragma once
#include "common.h"
#include <libheif/heif.h>
#include <vector>

class HeicFrameDecode : public IWICBitmapFrameDecode {
public:
    HeicFrameDecode();
    ~HeicFrameDecode();

    // Takes ownership of the heif_image_handle (caller should NOT free it)
    HRESULT Initialize(heif_image_handle* handle);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IWICBitmapSource
    STDMETHODIMP GetSize(UINT* pWidth, UINT* pHeight) override;
    STDMETHODIMP GetPixelFormat(WICPixelFormatGUID* pPixelFormat) override;
    STDMETHODIMP GetResolution(double* pDpiX, double* pDpiY) override;
    STDMETHODIMP CopyPalette(IWICPalette* pIPalette) override;
    STDMETHODIMP CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override;

    // IWICBitmapFrameDecode
    STDMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader** ppIMetadataQueryReader) override;
    STDMETHODIMP GetColorContexts(UINT cCount, IWICColorContext** ppIColorContexts, UINT* pcActualCount) override;
    STDMETHODIMP GetThumbnail(IWICBitmapSource** ppIThumbnail) override;

private:
    HRESULT DecodeImage(heif_image_handle* handle);

    volatile LONG m_refCount = 1;
    std::vector<uint8_t> m_pixels;  // BGRA32, post-orientation
    UINT m_width = 0;
    UINT m_height = 0;
    CRITICAL_SECTION m_cs;
};
