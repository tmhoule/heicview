#pragma once
#include "common.h"
#include <thumbcache.h>
#include <propsys.h>
#include <libheif/heif.h>
#include <vector>

class HeicThumbnailProvider : public IThumbnailProvider, public IInitializeWithStream {
public:
    HeicThumbnailProvider();
    ~HeicThumbnailProvider();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* pstream, DWORD grfMode) override;

    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

private:
    volatile LONG m_refCount = 1;
    std::vector<uint8_t> m_data;
    bool m_initialized = false;
};
