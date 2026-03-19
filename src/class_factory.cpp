#include "class_factory.h"
#include "com_counter.h"

ClassFactory::ClassFactory() { COMCounter::ObjectCreated(); }
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

    // TODO: Task 3 will create HeicDecoder here
    return E_NOINTERFACE;
}

STDMETHODIMP ClassFactory::LockServer(BOOL lock) {
    if (lock) COMCounter::LockServer();
    else      COMCounter::UnlockServer();
    return S_OK;
}
