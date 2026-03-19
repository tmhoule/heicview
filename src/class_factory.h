#pragma once
#include "common.h"

class ClassFactory : public IClassFactory {
public:
    ClassFactory();
    ~ClassFactory();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    volatile LONG m_refCount = 1;
};
