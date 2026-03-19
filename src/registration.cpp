#include "registration.h"
#include <wincodec.h>
#include <objbase.h>
#include <cstdio>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Convert a GUID to the standard {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} form.
// buf must be at least 39 wchar_t characters (including null terminator).
static void GuidToString(const GUID& guid, wchar_t* buf, int cch)
{
    StringFromGUID2(guid, buf, cch);
}

// Write a REG_SZ value under an already-open key.
static LONG SetRegistryString(HKEY hKey, const wchar_t* name, const wchar_t* value)
{
    DWORD cbData = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    return RegSetValueExW(hKey, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value), cbData);
}

// Write a REG_DWORD value under an already-open key.
static LONG SetRegistryDword(HKEY hKey, const wchar_t* name, DWORD value)
{
    return RegSetValueExW(hKey, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
}

// Write a REG_BINARY value under an already-open key.
static LONG SetRegistryBinary(HKEY hKey, const wchar_t* name,
                               const BYTE* data, DWORD cbData)
{
    return RegSetValueExW(hKey, name, 0, REG_BINARY, data, cbData);
}

// ---------------------------------------------------------------------------
// RegisterWICDecoder
// ---------------------------------------------------------------------------

HRESULT RegisterWICDecoder(HMODULE hModule)
{
    // Retrieve the path of this DLL.
    wchar_t dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(hModule, dllPath, MAX_PATH) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    // Build GUID strings we'll reuse throughout.
    wchar_t clsidStr[39]       = {};
    wchar_t containerFmtStr[39] = {};
    wchar_t vendorStr[39]      = {};
    wchar_t pixelFmtBGRAStr[39] = {};

    GuidToString(CLSID_HeicDecoder,          clsidStr,          39);
    GuidToString(GUID_ContainerFormatHeic,   containerFmtStr,   39);
    GuidToString(GUID_VendorHEICconvert,     vendorStr,         39);
    GuidToString(GUID_WICPixelFormat32bppBGRA, pixelFmtBGRAStr, 39);

    // -----------------------------------------------------------------------
    // 1.  HKLM\SOFTWARE\Classes\CLSID\{CLSID_HeicDecoder}
    // -----------------------------------------------------------------------
    wchar_t clsidKeyPath[256] = {};
    _snwprintf_s(clsidKeyPath, _countof(clsidKeyPath), _TRUNCATE,
                 L"SOFTWARE\\Classes\\CLSID\\%s", clsidStr);

    HKEY hClsidKey = nullptr;
    LONG lr = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKeyPath,
                               0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_WRITE, nullptr, &hClsidKey, nullptr);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);

    // Values on the CLSID key itself.
    SetRegistryString(hClsidKey, nullptr,           L"HEIC WIC Codec");
    SetRegistryString(hClsidKey, L"Author",         L"HEICconvert");
    SetRegistryString(hClsidKey, L"Description",    L"HEIC WIC Codec");
    SetRegistryString(hClsidKey, L"FileExtensions", L".heic,.heif");
    SetRegistryString(hClsidKey, L"FriendlyName",   L"HEIC WIC Codec");
    SetRegistryString(hClsidKey, L"ContainerFormat",containerFmtStr);
    SetRegistryString(hClsidKey, L"MimeTypes",      L"image/heic,image/heif");
    SetRegistryString(hClsidKey, L"Vendor",         vendorStr);
    SetRegistryString(hClsidKey, L"Version",        L"1.0.0");

    // 1a. InprocServer32
    HKEY hInproc = nullptr;
    lr = RegCreateKeyExW(hClsidKey, L"InprocServer32",
                          0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_WRITE, nullptr, &hInproc, nullptr);
    if (lr == ERROR_SUCCESS)
    {
        SetRegistryString(hInproc, nullptr,           dllPath);
        SetRegistryString(hInproc, L"ThreadingModel", L"Both");
        RegCloseKey(hInproc);
    }

    // 1b. Formats\{GUID_WICPixelFormat32bppBGRA}  — empty subkey, just needs to exist
    {
        wchar_t fmtSubkey[64] = {};
        _snwprintf_s(fmtSubkey, _countof(fmtSubkey), _TRUNCATE,
                     L"Formats\\%s", pixelFmtBGRAStr);
        HKEY hFmt = nullptr;
        lr = RegCreateKeyExW(hClsidKey, fmtSubkey,
                              0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &hFmt, nullptr);
        if (lr == ERROR_SUCCESS) RegCloseKey(hFmt);
    }

    // 1c. Patterns
    // Pattern data: 8-byte signatures at file offset 4.
    // The mask is all 0xFF for each pattern (exact match).
    static const BYTE mask[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    // Pattern 0 — "ftypheic"
    static const BYTE pat0[8] = { 0x66, 0x74, 0x79, 0x70, 0x68, 0x65, 0x69, 0x63 };
    // Pattern 1 — "ftypheix"
    static const BYTE pat1[8] = { 0x66, 0x74, 0x79, 0x70, 0x68, 0x65, 0x69, 0x78 };
    // Pattern 2 — "ftypmif1"
    static const BYTE pat2[8] = { 0x66, 0x74, 0x79, 0x70, 0x6D, 0x69, 0x66, 0x31 };

    const BYTE* patterns[3] = { pat0, pat1, pat2 };
    const wchar_t* patternSubkeys[3] = { L"Patterns\\0", L"Patterns\\1", L"Patterns\\2" };

    for (int i = 0; i < 3; ++i)
    {
        HKEY hPat = nullptr;
        lr = RegCreateKeyExW(hClsidKey, patternSubkeys[i],
                              0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &hPat, nullptr);
        if (lr == ERROR_SUCCESS)
        {
            SetRegistryDword (hPat, L"Position", 4);
            SetRegistryDword (hPat, L"Length",   8);
            SetRegistryBinary(hPat, L"Pattern",  patterns[i], 8);
            SetRegistryBinary(hPat, L"Mask",     mask,        8);
            RegCloseKey(hPat);
        }
    }

    RegCloseKey(hClsidKey);

    // -----------------------------------------------------------------------
    // 2.  HKCR\CLSID\{CATID_WICBitmapDecoders}\Instance\{CLSID_HeicDecoder}
    //     CATID_WICBitmapDecoders = {7ED96837-96F0-4812-B211-F13C24117ED3}
    // -----------------------------------------------------------------------
    wchar_t instanceKeyPath[256] = {};
    _snwprintf_s(instanceKeyPath, _countof(instanceKeyPath), _TRUNCATE,
                 L"CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\%s",
                 clsidStr);

    HKEY hInstanceKey = nullptr;
    lr = RegCreateKeyExW(HKEY_CLASSES_ROOT, instanceKeyPath,
                          0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_WRITE, nullptr, &hInstanceKey, nullptr);
    if (lr != ERROR_SUCCESS) return HRESULT_FROM_WIN32(lr);

    SetRegistryString(hInstanceKey, L"CLSID",       clsidStr);
    SetRegistryString(hInstanceKey, L"FriendlyName", L"HEIC WIC Codec");

    RegCloseKey(hInstanceKey);

    return S_OK;
}

// ---------------------------------------------------------------------------
// UnregisterWICDecoder
// ---------------------------------------------------------------------------

HRESULT UnregisterWICDecoder()
{
    wchar_t clsidStr[39] = {};
    GuidToString(CLSID_HeicDecoder, clsidStr, 39);

    // 1. Delete entire HKLM\SOFTWARE\Classes\CLSID\{CLSID_HeicDecoder} subtree.
    wchar_t clsidKeyPath[256] = {};
    _snwprintf_s(clsidKeyPath, _countof(clsidKeyPath), _TRUNCATE,
                 L"SOFTWARE\\Classes\\CLSID\\%s", clsidStr);

    LONG lr = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKeyPath);
    if (lr != ERROR_SUCCESS && lr != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(lr);

    // 2. Delete HKCR\CLSID\{CATID_WICBitmapDecoders}\Instance\{CLSID_HeicDecoder} subtree.
    wchar_t instanceKeyPath[256] = {};
    _snwprintf_s(instanceKeyPath, _countof(instanceKeyPath), _TRUNCATE,
                 L"CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\%s",
                 clsidStr);

    lr = RegDeleteTreeW(HKEY_CLASSES_ROOT, instanceKeyPath);
    if (lr != ERROR_SUCCESS && lr != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(lr);

    return S_OK;
}
