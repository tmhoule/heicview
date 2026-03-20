#include "class_factory.h"
#include "com_counter.h"
#include "codec.h"
#include "thumbnail_provider.h"

ClassFactory::ClassFactory(REFCLSID clsid) : m_clsid(clsid) { COMCounter::ObjectCreated(); }
ClassFactory::~ClassFactory() {}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        COMCounter::ObjectDestroyed();
        delete this;
    }
    return count;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) {
    if (pOuter) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;

    IUnknown* obj = nullptr;
    if (m_clsid == CLSID_HeicDecoder) {
        obj = static_cast<IWICBitmapDecoder*>(new(std::nothrow) HeicDecoder());
    } else if (m_clsid == CLSID_HeicThumbnailProvider) {
        obj = static_cast<IThumbnailProvider*>(new(std::nothrow) HeicThumbnailProvider());
    }
    if (!obj) return E_OUTOFMEMORY;
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
    if (lock) COMCounter::LockServer();
    else      COMCounter::UnlockServer();
    return S_OK;
}
