#include <initguid.h>
#include "common.h"
#include "com_counter.h"
#include "class_factory.h"
#include "registration.h"

HMODULE g_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) {
    if (clsid != CLSID_HeicDecoder) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new(std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return COMCounter::CanUnload() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return RegisterWICDecoder(g_hModule);
}

STDAPI DllUnregisterServer() {
    return UnregisterWICDecoder();
}
