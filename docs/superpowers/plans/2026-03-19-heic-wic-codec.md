# HEIC WIC Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a WIC codec DLL that gives Windows 11 native HEIC image support, deployed silently via BigFix.

**Architecture:** A C++ COM DLL implementing WIC decoder interfaces, backed by libheif (container parsing) and libde265 (HEVC decoding). Built on GitHub Actions (windows-latest), developed on macOS. Registered via `regsvr32` which writes registry entries Windows uses to discover the codec.

**Tech Stack:** C++17, COM, WIC API, CMake, libheif (LGPL-3.0), libde265 (LGPL-3.0), MSVC, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-03-19-heic-wic-codec-design.md`

**Important — Development Environment:** This project is developed on macOS but targets Windows. Code cannot be compiled or tested locally. GitHub Actions CI is the build/test environment. "Verify it builds" means push and check CI.

---

## File Map

| File | Responsibility |
|------|---------------|
| `CMakeLists.txt` | Top-level build: vcpkg for libheif/libde265, DLL target, DEF file |
| `src/guids.h` | All COM GUIDs (decoder CLSID, container format, vendor) |
| `src/common.h` | Umbrella header: windows.h, wincodec.h, wincodecsdk.h, guids.h |
| `src/com_counter.h` | Singleton tracking live COM objects and server locks |
| `src/class_factory.h/.cpp` | IClassFactory — creates decoder instances |
| `src/codec.h/.cpp` | IWICBitmapDecoder — opens HEIC via libheif, owns frames |
| `src/frame_decode.h/.cpp` | IWICBitmapFrameDecode + IWICBitmapSource — pixel decoding to BGRA32, EXIF orientation |
| `src/metadata_reader.h/.cpp` | IWICMetadataBlockReader — exposes EXIF metadata blocks |
| `src/dll_main.cpp` | DllMain, DllGetClassObject, DllCanUnloadNow, DllRegisterServer, DllUnregisterServer |
| `src/registration.h/.cpp` | Registry write/delete helpers for WIC codec registration |
| `src/heic_wic.def` | Module definition — exports 4 DLL functions |
| `src/version.rc` | VERSIONINFO resource (SemVer) |
| `.github/workflows/build.yml` | CI: build on windows-latest, upload artifacts |
| `installer/install.ps1` | Silent installer for BigFix |
| `installer/uninstall.ps1` | Silent uninstaller |
| `test/test_decode.cpp` | CI test: loads DLL, decodes a test HEIC file, verifies pixel output |
| `test/samples/test.heic` | Small test HEIC image (a few KB) |
| `.gitignore` | Build artifacts, IDE files |
| `LICENSE` | MIT license |
| `vcpkg.json` | vcpkg manifest for libheif dependency |

---

## Task 1: Project Scaffolding — CMake, GUIDs, CI

**Files:**
- Create: `CMakeLists.txt`
- Create: `vcpkg.json`
- Create: `src/guids.h`
- Create: `src/common.h`
- Create: `src/heic_wic.def`
- Create: `src/version.rc`
- Create: `.github/workflows/build.yml`
- Create: `.gitignore`
- Create: `LICENSE`

This task sets up the skeleton so CI can confirm the project configures and produces a DLL (even if it's empty).

- [ ] **Step 1: Create `.gitignore`**

```
build/
*.obj
*.dll
*.exp
*.lib
*.pdb
.vs/
CMakeUserPresets.json
```

- [ ] **Step 2: Create `LICENSE`**

```
MIT License

Copyright (c) 2026 HEICconvert contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 3: Create `src/guids.h`**

Generate unique GUIDs for: `CLSID_HeicDecoder`, `GUID_ContainerFormatHeic`, `GUID_VendorHEICconvert`. Use `uuidgen` or an online generator. Define using `DEFINE_GUID` macro.

```cpp
#pragma once
#include <guiddef.h>

// {INSERT-GENERATED-GUID} — COM class ID for our decoder
DEFINE_GUID(CLSID_HeicDecoder,
    0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

// {INSERT-GENERATED-GUID} — HEIC container format identifier
DEFINE_GUID(GUID_ContainerFormatHeic,
    0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

// {INSERT-GENERATED-GUID} — Vendor identifier
DEFINE_GUID(GUID_VendorHEICconvert,
    0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
```

Replace `0x00000000...` with actual generated GUIDs.

- [ ] **Step 4: Create `src/common.h`**

```cpp
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <objbase.h>
#include <new>

#include "guids.h"
```

Note: `shlwapi.h` is only needed in `metadata_reader.cpp` for `SHCreateMemStream` — include it there locally, not in the umbrella header.

- [ ] **Step 5: Create `src/heic_wic.def`**

```
LIBRARY heic_wic
EXPORTS
    DllGetClassObject   PRIVATE
    DllCanUnloadNow     PRIVATE
    DllRegisterServer   PRIVATE
    DllUnregisterServer PRIVATE
```

- [ ] **Step 6: Create `src/version.rc`**

```rc
#include <winver.h>

VS_VERSION_INFO VERSIONINFO
FILEVERSION     1,0,0,0
PRODUCTVERSION  1,0,0,0
FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK
FILEFLAGS       0
FILEOS          VOS_NT_WINDOWS32
FILETYPE        VFT_DLL
FILESUBTYPE     VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      "HEICconvert"
            VALUE "FileDescription",  "HEIC WIC Codec"
            VALUE "FileVersion",      "1.0.0"
            VALUE "InternalName",     "heic_wic"
            VALUE "LegalCopyright",   "MIT License"
            VALUE "OriginalFilename", "heic_wic.dll"
            VALUE "ProductName",      "HEIC WIC Codec"
            VALUE "ProductVersion",   "1.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 0x04B0
    END
END
```

- [ ] **Step 7: Create `src/dll_main.cpp` (stub)**

Minimal stub that compiles — just `DllMain` and empty exports. Will be filled in later tasks.

```cpp
#include "common.h"

HMODULE g_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID, REFIID, LPVOID*) {
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return S_OK;
}

STDAPI DllRegisterServer() {
    return S_OK;
}

STDAPI DllUnregisterServer() {
    return S_OK;
}
```

- [ ] **Step 8: Create `vcpkg.json`**

```json
{
  "name": "heic-wic-codec",
  "version": "1.0.0",
  "dependencies": [
    {
      "name": "libheif",
      "features": ["libde265"]
    }
  ]
}
```

This tells vcpkg to install libheif with the libde265 HEVC decoder backend. vcpkg handles the libheif ↔ libde265 dependency resolution automatically — avoiding the FetchContent `find_package` issue.

- [ ] **Step 9: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.24)
project(heic_wic VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg provides libheif and libde265
find_package(libheif CONFIG REQUIRED)

# Our WIC codec DLL — include .def file in sources (CMake handles it for MSVC)
add_library(heic_wic SHARED
    src/dll_main.cpp
    src/heic_wic.def
    src/version.rc
)

target_include_directories(heic_wic PRIVATE src)
# Note: vcpkg target may be heif::heif — check find_package output and adjust
target_link_libraries(heic_wic PRIVATE heif ole32 shlwapi windowscodecs advapi32)
set_target_properties(heic_wic PROPERTIES OUTPUT_NAME "heic_wic")

# Copy dependency DLLs next to our DLL for packaging
# Note: vcpkg imported target name may be heif::heif or similar —
# adjust based on find_package output. If generator expression fails,
# use a glob copy of vcpkg's installed bin directory as fallback.
add_custom_command(TARGET heic_wic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        $<TARGET_FILE_DIR:heic_wic>
)
```

Note: The `.def` file is listed directly in `add_library` sources — CMake handles module definition files automatically for MSVC. `advapi32` is included for registry APIs (used in Task 6).

- [ ] **Step 10: Create `.github/workflows/build.yml`**

```yaml
name: Build

on:
  push:
    branches: [main]
    tags: ['v*']
  pull_request:
    branches: [main]
  workflow_dispatch:

env:
  VCPKG_DEFAULT_TRIPLET: x64-windows

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11
        with:
          vcpkgGitCommitId: 'a34c873a9717a888f58dc05268dea15592c2f0ff'  # pin to a known-good commit

      - name: Configure CMake
        run: cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=${{ github.workspace }}/vcpkg/scripts/buildsystems/vcpkg.cmake

      - name: Build
        run: cmake --build build --config Release

      - name: Collect artifacts
        run: |
          mkdir artifact
          Get-ChildItem build/Release/*.dll | Copy-Item -Destination artifact/
          Copy-Item installer/install.ps1 artifact/
          Copy-Item installer/uninstall.ps1 artifact/
          Get-ChildItem artifact/
        shell: pwsh

      - uses: actions/upload-artifact@v4
        with:
          name: heic-wic-codec
          path: artifact/

  release:
    needs: build
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - uses: actions/download-artifact@v4
        with:
          name: heic-wic-codec
          path: artifact/

      - name: Create Release
        uses: softprops/action-gh-release@v2
        with:
          files: artifact/*
```

Note: The artifact collection step copies ALL DLLs from the Release dir. This captures `heic_wic.dll` plus whatever vcpkg names the dependency DLLs (may be `heif.dll`, `libheif.dll`, `de265.dll`, or `libde265.dll`). The installer scripts should be updated in Task 7 to match the actual DLL names from CI output.

- [ ] **Step 11: Create placeholder installer scripts**

Create `installer/install.ps1` and `installer/uninstall.ps1` with placeholder comments — they'll be fully implemented in Task 7.

```powershell
# install.ps1 — placeholder, implemented in Task 7
Write-Host "Not yet implemented"
exit 0
```

```powershell
# uninstall.ps1 — placeholder, implemented in Task 7
Write-Host "Not yet implemented"
exit 0
```

- [ ] **Step 12: Commit and push**

```bash
git add .gitignore LICENSE vcpkg.json CMakeLists.txt src/ .github/ installer/
git commit -m "feat: project scaffolding — CMake, vcpkg, GUIDs, CI, stubs"
```

Push to GitHub. Verify the CI workflow triggers and the build produces `heic_wic.dll`.

- [ ] **Step 13: Fix CI until the build passes**

Iterate on `CMakeLists.txt`, `vcpkg.json`, and `.github/workflows/build.yml` until CI produces `heic_wic.dll`. Common issues:
- vcpkg commit ID may need updating if the pinned one is too old for the libheif version
- libheif feature name may differ (try `libde265` vs `de265`)
- Dependency DLL names may differ — check CI output to see actual filenames
- Artifact copy paths may differ (e.g., `build/Release/` vs `build/`)

Fix each issue, push, check CI. Commit each fix separately.

---

## Task 2: COM Infrastructure — Counter, Class Factory

**Files:**
- Create: `src/com_counter.h`
- Create: `src/class_factory.h`
- Create: `src/class_factory.cpp`
- Modify: `src/dll_main.cpp` — wire up DllGetClassObject and DllCanUnloadNow

- [ ] **Step 1: Create `src/com_counter.h`**

Singleton tracking live COM objects using `InterlockedIncrement`/`InterlockedDecrement`.

```cpp
#pragma once
#include <windows.h>

class COMCounter {
public:
    static void ObjectCreated()  { InterlockedIncrement(&s_objectCount); }
    static void ObjectDestroyed(){ InterlockedDecrement(&s_objectCount); }
    static void LockServer()     { InterlockedIncrement(&s_lockCount); }
    static void UnlockServer()   { InterlockedDecrement(&s_lockCount); }
    static bool CanUnload()      { return s_objectCount == 0 && s_lockCount == 0; }

private:
    static inline volatile LONG s_objectCount = 0;
    static inline volatile LONG s_lockCount = 0;
};
```

- [ ] **Step 2: Create `src/class_factory.h`**

```cpp
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
```

- [ ] **Step 3: Create `src/class_factory.cpp`**

Implements `ClassFactory`. `CreateInstance` checks for `IID_IWICBitmapDecoder` and creates the decoder (stubbed for now — returns `E_NOINTERFACE` until Task 3). Uses `COMCounter` for ref tracking.

```cpp
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
```

- [ ] **Step 4: Update `src/dll_main.cpp`**

Wire up `DllGetClassObject` to create a `ClassFactory` when our CLSID is requested. Wire up `DllCanUnloadNow` to check `COMCounter`.

```cpp
#include "common.h"
#include "com_counter.h"
#include "class_factory.h"

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
    return S_OK; // Implemented in Task 6
}

STDAPI DllUnregisterServer() {
    return S_OK; // Implemented in Task 6
}
```

- [ ] **Step 5: Update `CMakeLists.txt`**

Add `src/class_factory.cpp` to the `add_library` sources.

- [ ] **Step 6: Commit and push, verify CI builds**

```bash
git add src/com_counter.h src/class_factory.h src/class_factory.cpp src/dll_main.cpp CMakeLists.txt
git commit -m "feat: COM infrastructure — counter, class factory"
```

---

## Task 3: IWICBitmapDecoder — Opening HEIC Files

**Files:**
- Create: `src/codec.h`
- Create: `src/codec.cpp`
- Modify: `src/class_factory.cpp` — wire CreateInstance to create HeicDecoder
- Modify: `CMakeLists.txt` — add source file

The decoder receives an `IStream` from WIC, reads the entire stream into a buffer, passes it to libheif to open, and serves frames.

- [ ] **Step 1: Create `src/codec.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/codec.cpp`**

Implement the decoder. Key logic:

- `Initialize`: read `IStream` into `m_data`, create `heif_context`, call `heif_context_read_from_memory`.
- `GetFrameCount`: use `heif_context_get_number_of_top_level_images`.
- `GetFrame`: for index 0, use `heif_context_get_primary_image_handle`. For other indices, use `heif_context_get_list_of_top_level_image_IDs` to get the ID array, then `heif_context_get_image_handle` for the specific ID. Create a `HeicFrameDecode` (Task 4) and pass the handle.
- Stubs: `CopyPalette` returns `WINCODEC_ERR_PALETTEUNAVAILABLE`, `GetPreview`/`GetThumbnail`/`GetMetadataQueryReader` return `WINCODEC_ERR_UNSUPPORTEDOPERATION`.
- `GetContainerFormat` returns `GUID_ContainerFormatHeic`.
- `QueryCapability` returns `WICBitmapDecoderCapabilityCanDecodeAllImages`.
- **Error mapping** per spec: `heif_error_Ok` → `S_OK`, `heif_error_Invalid_input` / `heif_error_Unsupported_feature` → `WINCODEC_ERR_BADIMAGE`, unsupported codec → `WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT`, memory errors → `E_OUTOFMEMORY`, all other libheif errors → `WINCODEC_ERR_GENERIC_ERROR`.
- `CRITICAL_SECTION` initialized in constructor, destroyed in destructor.
- **Exception safety:** Every public COM method body must be wrapped in `try { ... } catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }`. The codec runs in-process inside Explorer, Photos, etc. — an unhandled exception crashes the host application.

The full implementation should be ~150-200 lines. Key function bodies:

```cpp
STDMETHODIMP HeicDecoder::Initialize(IStream* pIStream, WICDecodeOptions) {
    // Exception safety: every COM entry point must catch all exceptions
    try {
        if (!pIStream) return E_INVALIDARG;
        EnterCriticalSection(&m_cs);

        // Read entire stream into m_data
        STATSTG stat = {};
        HRESULT hr = pIStream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) { LeaveCriticalSection(&m_cs); return hr; }

        m_data.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        ULONG bytesRead = 0;
        hr = pIStream->Read(m_data.data(), static_cast<ULONG>(m_data.size()), &bytesRead);
        if (FAILED(hr)) { LeaveCriticalSection(&m_cs); return hr; }

        // Open with libheif
        m_ctx = heif_context_alloc();
        heif_error err = heif_context_read_from_memory_without_copy(
            m_ctx, m_data.data(), m_data.size(), nullptr);
        if (err.code != heif_error_Ok) {
            heif_context_free(m_ctx);
            m_ctx = nullptr;
            LeaveCriticalSection(&m_cs);
            return WINCODEC_ERR_BADIMAGE;
        }

        m_initialized = true;
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }
}
```

- [ ] **Step 3: Wire up `ClassFactory::CreateInstance`**

In `src/class_factory.cpp`, replace the TODO with:

```cpp
#include "codec.h"

// In CreateInstance — always create the object and let QueryInterface
// handle interface negotiation (caller may request IID_IUnknown):
auto* decoder = new(std::nothrow) HeicDecoder();
if (!decoder) return E_OUTOFMEMORY;
HRESULT hr = decoder->QueryInterface(riid, ppv);
decoder->Release();
return hr;
```

- [ ] **Step 4: Update `CMakeLists.txt`**

Add `src/codec.cpp` to sources.

- [ ] **Step 5: Commit and push, verify CI builds**

```bash
git add src/codec.h src/codec.cpp src/class_factory.cpp CMakeLists.txt
git commit -m "feat: IWICBitmapDecoder — open HEIC files via libheif"
```

---

## Task 4: IWICBitmapFrameDecode — Pixel Decoding

**Files:**
- Create: `src/frame_decode.h`
- Create: `src/frame_decode.cpp`
- Modify: `src/codec.cpp` — `GetFrame` creates `HeicFrameDecode`
- Modify: `CMakeLists.txt` — add source file

This is the core: decode HEVC data to BGRA32 pixels with EXIF orientation applied.

- [ ] **Step 1: Create `src/frame_decode.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/frame_decode.cpp`**

Key implementation details:

**`Initialize` / `DecodeImage`:**
The image handle is passed in from `HeicDecoder::GetFrame` (which handles primary vs indexed image selection).

1. Decode to RGB interleaved: `heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA)`. By default libheif applies EXIF orientation transforms during decoding (`ignore_transformations=false`), so the decoded image is already correctly oriented. No manual rotation needed.
2. Get pixel data: `heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride)`
3. Convert RGBA to BGRA (swap R and B channels) into `m_pixels`. Set `m_width`/`m_height` from `heif_image_get_width`/`heif_image_get_height` on the decoded image (these are post-orientation dimensions).
4. Free heif_image and handle
5. Apply **error mapping** per spec for any libheif errors during decode

**Exception safety:** Every public COM method must be wrapped in `try { ... } catch (...) { return WINCODEC_ERR_GENERIC_ERROR; }` — same pattern as Task 3.

**`CopyPixels`:**
Standard WIC implementation — copies a rectangle from `m_pixels` to the caller's buffer, respecting stride.

```cpp
STDMETHODIMP HeicFrameDecode::CopyPixels(const WICRect* prc, UINT cbStride,
                                          UINT cbBufferSize, BYTE* pbBuffer) {
    if (!pbBuffer) return E_INVALIDARG;

    WICRect rect = {0, 0, static_cast<INT>(m_width), static_cast<INT>(m_height)};
    if (prc) rect = *prc;

    if (rect.X < 0 || rect.Y < 0 ||
        rect.X + rect.Width > static_cast<INT>(m_width) ||
        rect.Y + rect.Height > static_cast<INT>(m_height))
        return E_INVALIDARG;

    const UINT bytesPerPixel = 4; // BGRA32

    // Validate caller's buffer is large enough
    if (cbBufferSize < static_cast<UINT>(rect.Height - 1) * cbStride + static_cast<UINT>(rect.Width) * bytesPerPixel)
        return E_INVALIDARG;

    const UINT srcStride = m_width * bytesPerPixel;

    for (INT y = 0; y < rect.Height; y++) {
        const BYTE* srcRow = m_pixels.data() + (rect.Y + y) * srcStride + rect.X * bytesPerPixel;
        BYTE* dstRow = pbBuffer + y * cbStride;
        memcpy(dstRow, srcRow, rect.Width * bytesPerPixel);
    }
    return S_OK;
}
```

**`GetSize`:** Returns `m_width`, `m_height` (post-orientation).

**`GetPixelFormat`:** Returns `GUID_WICPixelFormat32bppBGRA`.

**`GetResolution`:** Returns 96.0, 96.0 DPI.

**Stubs:** `CopyPalette` → `WINCODEC_ERR_PALETTEUNAVAILABLE`, `GetThumbnail` → `WINCODEC_ERR_CODECNOTHUMBNAIL`, `GetColorContexts` → sets `pcActualCount = 0`, returns `S_OK`. `GetMetadataQueryReader` → `WINCODEC_ERR_UNSUPPORTEDOPERATION` (metadata comes via Task 5's block reader).

- [ ] **Step 3: Wire up `HeicDecoder::GetFrame`**

In `src/codec.cpp`, implement `GetFrame` to create a `HeicFrameDecode` and call its `Initialize`.

- [ ] **Step 4: Update `CMakeLists.txt`**

Add `src/frame_decode.cpp` to sources.

- [ ] **Step 5: Commit and push, verify CI builds**

```bash
git add src/frame_decode.h src/frame_decode.cpp src/codec.cpp CMakeLists.txt
git commit -m "feat: IWICBitmapFrameDecode — decode HEIC to BGRA32 pixels"
```

---

## Task 5: IWICMetadataBlockReader — EXIF Metadata

**Files:**
- Create: `src/metadata_reader.h`
- Create: `src/metadata_reader.cpp`
- Modify: `src/frame_decode.h/.cpp` — `QueryInterface` returns metadata reader
- Modify: `CMakeLists.txt` — add source file

- [ ] **Step 1: Create `src/metadata_reader.h`**

```cpp
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
    std::vector<std::vector<uint8_t>> m_metadataBlocks; // Raw EXIF blocks
};
```

- [ ] **Step 2: Create `src/metadata_reader.cpp`**

Key logic:
- `Initialize`: use `heif_image_handle_get_list_of_metadata_block_content_type(handle, "Exif", ...)` to enumerate EXIF blocks. Read each block's raw bytes via `heif_image_handle_get_metadata`.
- `GetContainerFormat`: returns `GUID_ContainerFormatHeic`.
- `GetCount`: returns number of metadata blocks found.
- `GetReaderByIndex`: creates an `IStream` from the raw EXIF bytes, then uses `IWICComponentFactory::CreateMetadataReaderFromContainer` or `WICMatchMetadataContent` to get a WIC metadata reader for the EXIF data. This lets WIC handle EXIF parsing — we just provide the raw bytes.
- `GetEnumerator`: returns `WINCODEC_ERR_UNSUPPORTEDOPERATION` (optional method).

```cpp
STDMETHODIMP HeicMetadataBlockReader::GetReaderByIndex(UINT nIndex, IWICMetadataReader** ppIMetadataReader) {
    if (nIndex >= m_metadataBlocks.size()) return E_INVALIDARG;
    if (!ppIMetadataReader) return E_INVALIDARG;

    // Create IStream from raw EXIF bytes
    IStream* pStream = SHCreateMemStream(
        m_metadataBlocks[nIndex].data(),
        static_cast<UINT>(m_metadataBlocks[nIndex].size()));
    if (!pStream) return E_OUTOFMEMORY;

    // IMPORTANT: EXIF data from HEIF has a 4-byte TIFF header offset prefix
    // before the actual EXIF APP1 data. Skip these 4 bytes before passing
    // to WIC, otherwise CreateMetadataReaderFromContainer will fail.
    LARGE_INTEGER offset;
    offset.QuadPart = 4;
    pStream->Seek(offset, STREAM_SEEK_SET, nullptr);

    // Use WIC to create a metadata reader from the EXIF stream
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
```

- [ ] **Step 3: Wire metadata into frame decode**

In `src/frame_decode.cpp`, when `QueryInterface` is called with `IID_IWICMetadataBlockReader`, return the metadata reader. Store a `HeicMetadataBlockReader*` as a member of `HeicFrameDecode`, initialized during `DecodeImage`.

- [ ] **Step 4: Update `CMakeLists.txt`**

Add `src/metadata_reader.cpp` to sources.

- [ ] **Step 5: Commit and push, verify CI builds**

```bash
git add src/metadata_reader.h src/metadata_reader.cpp src/frame_decode.h src/frame_decode.cpp CMakeLists.txt
git commit -m "feat: IWICMetadataBlockReader — expose EXIF metadata"
```

---

## Task 6: COM Registration — DllRegisterServer / DllUnregisterServer

**Files:**
- Create: `src/registration.h`
- Create: `src/registration.cpp`
- Modify: `src/dll_main.cpp` — call registration helpers

- [ ] **Step 1: Create `src/registration.h`**

```cpp
#pragma once
#include "common.h"

HRESULT RegisterWICDecoder(HMODULE hModule);
HRESULT UnregisterWICDecoder();
```

- [ ] **Step 2: Create `src/registration.cpp`**

Implements the full registry tree from the spec:

Note: Per Microsoft's WIC documentation and the jpegxl-wic reference project, WIC decoder metadata is registered under the decoder's CLSID key (not under `HKLM\SOFTWARE\Microsoft\Windows Imaging Component\Decoders\`). The WIC Decoder Category instance entry is what actually tells WIC about the decoder.

1. **COM Server + WIC Decoder metadata**: `HKLM\SOFTWARE\Classes\CLSID\{CLSID_HeicDecoder}`
   - `InprocServer32` = DLL path (get via `GetModuleFileName(g_hModule, ...)`)
   - `InprocServer32\ThreadingModel` = `Both`

2. **WIC Decoder metadata**: under same CLSID key
   - `FriendlyName`, `ContainerFormat`, `MimeTypes`, `FileExtensions`, `Author`, `Version`
   - `Formats\{GUID_WICPixelFormat32bppBGRA}` subkey
   - `Patterns\0`: Position=4, Length=8, Pattern=`ftypheic`, Mask=`\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF`
   - `Patterns\1`: Position=4, Length=8, Pattern=`ftypheix`, Mask=same
   - `Patterns\2`: Position=4, Length=8, Pattern=`ftypmif1`, Mask=same

3. **WIC Decoder Category**: `HKCR\CLSID\{CATID_WICBitmapDecoders}\Instance\{CLSID_HeicDecoder}`
   - `CLSID` = string representation of `CLSID_HeicDecoder`
   - `FriendlyName` = `HEIC WIC Codec`

Use `RegCreateKeyEx` / `RegSetValueEx` / `RegDeleteTree` for write and cleanup. Convert GUIDs to strings via `StringFromGUID2`.

`UnregisterWICDecoder` deletes all three registry branches.

- [ ] **Step 3: Update `src/dll_main.cpp`**

```cpp
#include "registration.h"

STDAPI DllRegisterServer() {
    return RegisterWICDecoder(g_hModule);
}

STDAPI DllUnregisterServer() {
    return UnregisterWICDecoder();
}
```

- [ ] **Step 4: Update `CMakeLists.txt`**

Add `src/registration.cpp` to sources. Add `advapi32` to link libraries (for registry APIs).

- [ ] **Step 5: Commit and push, verify CI builds**

```bash
git add src/registration.h src/registration.cpp src/dll_main.cpp CMakeLists.txt
git commit -m "feat: DllRegisterServer/DllUnregisterServer — WIC registry entries"
```

---

## Task 7: Installer Scripts

**Files:**
- Modify: `installer/install.ps1`
- Modify: `installer/uninstall.ps1`

- [ ] **Step 1: Implement `installer/install.ps1`**

```powershell
param(
    [string]$SourceDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

# Check admin elevation — exit 3 per spec if not admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "Must be run as administrator"
    exit 3
}

$InstallDir = "$env:ProgramFiles\HEICconvert"
$LogDir = "$env:ProgramData\HEICconvert"
$LogFile = "$LogDir\install.log"

function Write-Log($msg) {
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    $line = "$timestamp  $msg"
    if (!(Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
    Add-Content -Path $LogFile -Value $line
}

try {
    Write-Log "=== Install started ==="

    # Stop Explorer to release DLL locks
    Write-Log "Stopping explorer.exe"
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # Copy files
    Write-Log "Copying files to $InstallDir"
    if (!(Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
    # Copy all DLLs — vcpkg may name them heif.dll or libheif.dll, de265.dll or libde265.dll
    Get-ChildItem "$SourceDir\*.dll" | Copy-Item -Destination "$InstallDir\" -Force

    # Register COM DLL
    Write-Log "Registering heic_wic.dll"
    $regResult = Start-Process -FilePath regsvr32 -ArgumentList '/s', "`"$InstallDir\heic_wic.dll`"" -Wait -PassThru
    if ($regResult.ExitCode -ne 0) {
        Write-Log "ERROR: regsvr32 failed with exit code $($regResult.ExitCode)"
        Start-Process explorer
        exit 1
    }

    # Restart Explorer
    Write-Log "Restarting explorer.exe"
    Start-Process explorer

    # Clear thumbnail cache
    Write-Log "Clearing thumbnail cache"
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue

    Write-Log "=== Install completed successfully ==="
    exit 0
}
catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    Start-Process explorer -ErrorAction SilentlyContinue
    exit 2
}
```

- [ ] **Step 2: Implement `installer/uninstall.ps1`**

```powershell
$ErrorActionPreference = 'Stop'

# Check admin elevation — exit 3 per spec if not admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "Must be run as administrator"
    exit 3
}

$InstallDir = "$env:ProgramFiles\HEICconvert"
$LogDir = "$env:ProgramData\HEICconvert"
$LogFile = "$LogDir\install.log"

function Write-Log($msg) {
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    $line = "$timestamp  $msg"
    if (!(Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
    Add-Content -Path $LogFile -Value $line
}

try {
    Write-Log "=== Uninstall started ==="

    # Stop Explorer
    Write-Log "Stopping explorer.exe"
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    # Unregister COM DLL
    if (Test-Path "$InstallDir\heic_wic.dll") {
        Write-Log "Unregistering heic_wic.dll"
        Start-Process -FilePath regsvr32 -ArgumentList '/u', '/s', "`"$InstallDir\heic_wic.dll`"" -Wait
    }

    # Remove files
    Write-Log "Removing $InstallDir"
    try {
        Remove-Item $InstallDir -Recurse -Force
    }
    catch {
        Write-Log "WARN: Could not remove $InstallDir (may be locked). Scheduling removal on reboot."
        # MoveFileEx with MOVEFILE_DELAY_UNTIL_REBOOT via .NET interop
        Add-Type @"
            using System;
            using System.Runtime.InteropServices;
            public class FileOps {
                [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
                public static extern bool MoveFileEx(string lpExistingFileName, string lpNewFileName, int dwFlags);
            }
"@
        Get-ChildItem $InstallDir -File | ForEach-Object {
            [FileOps]::MoveFileEx($_.FullName, $null, 4) | Out-Null  # 4 = MOVEFILE_DELAY_UNTIL_REBOOT
        }
        [FileOps]::MoveFileEx($InstallDir, $null, 4) | Out-Null
    }

    # Restart Explorer
    Write-Log "Restarting explorer.exe"
    Start-Process explorer

    # Clear thumbnail cache
    Write-Log "Clearing thumbnail cache"
    Remove-Item "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\thumbcache_*.db" -Force -ErrorAction SilentlyContinue

    Write-Log "=== Uninstall completed ==="
    exit 0
}
catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    Start-Process explorer -ErrorAction SilentlyContinue
    exit 1
}
```

- [ ] **Step 3: Commit**

```bash
git add installer/install.ps1 installer/uninstall.ps1
git commit -m "feat: install/uninstall scripts for BigFix deployment"
```

---

## Task 8: CI Test — Validate Decode on Windows

**Files:**
- Create: `test/test_decode.cpp`
- Create: `test/samples/test.heic`
- Modify: `CMakeLists.txt` — add test target
- Modify: `.github/workflows/build.yml` — run test after build

A minimal test that loads the DLL directly via COM (bypassing WIC discovery — no registry writes needed), feeds it a test HEIC file, and verifies it produces pixels of the expected size.

- [ ] **Step 1: Create a test HEIC file**

Generate a tiny (e.g., 2x2 pixel) HEIC file for testing. Options:
- Use `ffmpeg` on your Mac: `ffmpeg -f lavfi -i color=c=red:s=2x2:d=1 -frames:v 1 -c:v libx265 test/samples/test.heic`
- Or find a freely-licensed sample HEIC from the libheif test suite

Commit the binary file.

- [ ] **Step 2: Create `test/test_decode.cpp`**

Uses direct COM instantiation (DllGetClassObject → IClassFactory → IWICBitmapDecoder) — no registry writes or admin required.

```cpp
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdlib>

// Forward-declare our CLSID (must match src/guids.h)
// Copy the actual GUID values from src/guids.h
DEFINE_GUID(CLSID_HeicDecoder,
    0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

typedef HRESULT(WINAPI* DllGetClassObjectFunc)(REFCLSID, REFIID, LPVOID*);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: test_decode <path-to-heic>\n");
        return 1;
    }

    CoInitialize(nullptr);

    // Load our DLL directly
    HMODULE hDll = LoadLibraryA("heic_wic.dll");
    if (!hDll) {
        fprintf(stderr, "FAIL: Could not load heic_wic.dll (error %lu)\n", GetLastError());
        return 1;
    }

    // Get class factory via DllGetClassObject — no registry needed
    auto getClassObject = (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    if (!getClassObject) {
        fprintf(stderr, "FAIL: DllGetClassObject not found\n");
        return 1;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = getClassObject(CLSID_HeicDecoder, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: DllGetClassObject hr=0x%08lx\n", hr);
        return 1;
    }

    // Create decoder via factory
    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IWICBitmapDecoder, (void**)&pDecoder);
    pFactory->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CreateInstance hr=0x%08lx\n", hr);
        return 1;
    }

    // Create IStream from test file
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wpath, MAX_PATH);
    IStream* pStream = nullptr;
    hr = SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: SHCreateStreamOnFile hr=0x%08lx\n", hr);
        return 1;
    }

    // Initialize decoder with stream
    hr = pDecoder->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
    pStream->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: Initialize hr=0x%08lx\n", hr);
        return 1;
    }

    UINT frameCount = 0;
    pDecoder->GetFrameCount(&frameCount);
    printf("Frame count: %u\n", frameCount);
    if (frameCount == 0) {
        fprintf(stderr, "FAIL: no frames\n");
        return 1;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: GetFrame hr=0x%08lx\n", hr);
        return 1;
    }

    UINT w = 0, h = 0;
    pFrame->GetSize(&w, &h);
    printf("Image size: %u x %u\n", w, h);
    if (w == 0 || h == 0) {
        fprintf(stderr, "FAIL: zero dimensions\n");
        return 1;
    }

    // Read pixels
    UINT stride = w * 4;
    UINT bufSize = stride * h;
    BYTE* pixels = (BYTE*)malloc(bufSize);
    hr = pFrame->CopyPixels(nullptr, stride, bufSize, pixels);
    if (FAILED(hr)) {
        fprintf(stderr, "FAIL: CopyPixels hr=0x%08lx\n", hr);
        return 1;
    }

    printf("PASS: decoded %u x %u HEIC image (%u bytes)\n", w, h, bufSize);

    free(pixels);
    pFrame->Release();
    pDecoder->Release();
    FreeLibrary(hDll);
    CoUninitialize();
    return 0;
}
```

Note: Replace the placeholder GUID in `CLSID_HeicDecoder` with the actual GUID from `src/guids.h`.

- [ ] **Step 3: Add test target to `CMakeLists.txt`**

```cmake
# Test executable
add_executable(test_decode test/test_decode.cpp)
target_link_libraries(test_decode PRIVATE ole32 shlwapi windowscodecs)

# Copy test HEIC sample to build dir at build time
add_custom_command(TARGET test_decode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test/samples
        ${CMAKE_BINARY_DIR}/test/samples
)
```

- [ ] **Step 4: Update `.github/workflows/build.yml`**

Add a test step after build:

```yaml
      - name: Test
        run: |
          cd build
          copy Release\heic_wic.dll .
          copy Release\heif.dll . 2>nul
          copy Release\de265.dll . 2>nul
          Release\test_decode.exe test\samples\test.heic
        shell: cmd
```

- [ ] **Step 5: Commit and push, verify test passes on CI**

```bash
git add test/ CMakeLists.txt .github/workflows/build.yml
git commit -m "test: CI decode test with sample HEIC file"
```

---

## Task 9: Final Polish and v1.0.0 Tag

**Files:**
- Possibly modify any file based on CI results

- [ ] **Step 1: Verify full CI pipeline**

Push to `main`. Confirm:
- Build succeeds on `windows-latest`
- Test passes
- Artifact ZIP contains `heic_wic.dll`, `heif.dll`, `de265.dll`, `install.ps1`, `uninstall.ps1`

- [ ] **Step 2: Fix any remaining issues**

Iterate on CI failures until clean.

- [ ] **Step 3: Tag v1.0.0**

```bash
git tag v1.0.0
git push origin v1.0.0
```

Verify the release workflow creates a GitHub Release with the artifact attached.

- [ ] **Step 4: Download release artifact and test on a Windows 11 machine**

Manual validation:
1. Extract the ZIP
2. Run `install.ps1` as admin
3. Copy a HEIC file from an iPhone
4. Verify: Explorer shows thumbnail, double-click opens in Photos, Paint can open it
5. Run `uninstall.ps1` — verify thumbnails stop working
