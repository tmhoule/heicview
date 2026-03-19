# HEIC WIC Codec — Design Spec

## Problem

iPhones send photos in HEIC format. Windows 11 cannot open, preview, or thumbnail HEIC files without additional codecs. Microsoft's HEVC codec costs $0.99 per machine, which is unacceptable for fleet-wide enterprise deployment.

## Solution

Build an open-source WIC (Windows Imaging Component) codec that enables native HEIC support across all WIC-aware Windows applications. Deploy silently to Windows 11 machines via HCL BigFix.

## Goals

- Windows Explorer shows HEIC thumbnails
- Photos, Paint, and other WIC-aware apps open HEIC files natively
- Zero end-user interaction required
- Silent deployment and uninstall via BigFix
- No per-machine licensing cost
- MIT-licensed codec with LGPL-3.0 dependencies (dynamically linked)

## Non-Goals

- HEIC encoding (users receive HEIC, they don't create it)
- Windows 10 support
- GUI installer
- HEIC-to-JPEG batch conversion

## Architecture

### Overview

A C++ COM DLL implementing WIC decoder interfaces, backed by libheif (HEIF container parsing) and libde265 (HEVC decoding). Registered via `regsvr32`, which writes registry entries that Windows uses to discover and load the codec on demand.

### Components

```
HEICconvert/
├── CMakeLists.txt              # Top-level CMake, fetches dependencies
├── src/
│   ├── class_factory.cpp/.h    # IClassFactory — COM object creation
│   ├── codec.cpp/.h            # IWICBitmapDecoder — file open, frame count
│   ├── frame_decode.cpp/.h     # IWICBitmapFrameDecode — pixel decoding (BGRA32)
│   ├── metadata_reader.cpp/.h  # IWICMetadataBlockReader — EXIF metadata
│   ├── dll_main.cpp            # DllRegisterServer, DllUnregisterServer,
│   │                           # DllGetClassObject, DllCanUnloadNow
│   └── guids.h                 # COM GUIDs for the codec
├── installer/
│   ├── install.ps1             # Silent installer for BigFix
│   └── uninstall.ps1           # Silent uninstaller
└── docs/
    └── superpowers/specs/      # This document
```

### WIC Interfaces

Decode-only implementation:

| Interface | Purpose |
|-----------|---------|
| `IWICBitmapDecoder` | Entry point. Opens HEIC file via libheif, reports frame count (top-level images, not tiles). |
| `IWICBitmapFrameDecode` | Decodes a single image frame to BGRA32 pixels. Inherits `IWICBitmapSource`, implementing `GetSize` (post-rotation dimensions), `GetPixelFormat`, `GetResolution`, `CopyPalette`, and `CopyPixels`. Applies EXIF orientation transforms during `CopyPixels`, returning pre-rotated pixel data. |
| `IWICMetadataBlockReader` | Exposes EXIF metadata blocks. WIC constructs `IWICMetadataQueryReader` from this automatically. Implements `GetContainerFormat`, `GetCount`, `GetReaderByIndex`, `GetEnumerator`. |
| `IClassFactory` | Standard COM factory for DLL registration. |

### DLL Exports

- `DllRegisterServer` — writes full registry tree (see Registry Entries below)
- `DllUnregisterServer` — removes those registry entries
- `DllGetClassObject` — returns IClassFactory for COM activation
- `DllCanUnloadNow` — ref-counting for safe unload

### Registry Entries

`DllRegisterServer` writes the following entries for WIC codec discovery:

1. **COM Server:** `HKLM\SOFTWARE\Classes\CLSID\{codec-guid}`
   - `InprocServer32` = path to `heic_wic.dll`
   - `InprocServer32\ThreadingModel` = `Both`

2. **WIC Decoder:** `HKLM\SOFTWARE\Microsoft\Windows Imaging Component\Decoders\{codec-guid}`
   - `FriendlyName` = `HEIC WIC Codec`
   - `ContainerFormat` = `{heic-container-guid}`
   - `MimeTypes` = `image/heic,image/heif`
   - `FileExtensions` = `.heic,.heif`
   - `Author`, `Version`
   - `Formats` subkey — lists `GUID_WICPixelFormat32bppBGRA`
   - `Patterns` subkey — HEIC magic bytes (`ftypheic`, `ftypmif1`, `ftypheix`) with position, length, pattern, and mask values

3. **WIC Decoder Category:** `HKCR\CLSID\{7ED96837-96F0-4812-B211-F13C24117ED3}\Instance\{codec-guid}`
   - Required for WIC runtime discovery of the codec

### HEIC Format Considerations

iPhone photos are typically stored as grid images — multiple 512x512 HEVC-encoded tiles assembled via an `idat`/`grid` derived image item. libheif handles tile assembly internally, compositing the grid into a single image. From WIC's perspective, frame count reflects the number of top-level images, not individual tiles.

### Data Flow

1. User double-clicks a .heic file (or Explorer generates a thumbnail)
2. Windows asks WIC: "who can decode .heic?"
3. WIC matches file bytes against the `Patterns` registry entries, finds our codec
4. WIC loads `heic_wic.dll`, calls `IWICBitmapDecoder::Initialize` with the file stream
5. Our codec passes the stream to libheif, which uses libde265 to decompress HEVC data (assembling grid tiles transparently)
6. `IWICBitmapFrameDecode::CopyPixels` returns BGRA32 pixel data with EXIF orientation already applied
7. App renders the image

### Dependencies

| Library | License | Role | Linking |
|---------|---------|------|---------|
| libheif | LGPL-3.0 | HEIF container parsing | Dynamic (libheif.dll) |
| libde265 | LGPL-3.0 | HEVC decoding | Dynamic (libde265.dll) |

All LGPL dependencies are dynamically linked as separate DLLs, keeping our codec under MIT license. Users retain the right to substitute their own versions of the LGPL libraries. Source code for the LGPL libraries is available at the strukturag GitHub repositories linked in the Reference Projects section.

Minimum versions: libheif >= 1.17.0, libde265 >= 1.0.15.

### Build System

- **CMake** with `FetchContent` to pull libheif and libde265 at build time
- **Compiler:** MSVC (Visual Studio 2022)
- **Target:** x64 only (Windows 11 is 64-bit only)
- **Output:** `heic_wic.dll`, `libheif.dll`, `libde265.dll`
- **Versioning:** SemVer (e.g., 1.0.0) embedded in the DLL's VERSIONINFO resource. BigFix relevance checks minimum version, not just presence.

## Installer

### install.ps1

1. Checks for admin elevation (exit code 3 if not admin)
2. Stops `explorer.exe` to release any DLL locks
3. Creates `C:\Program Files\HEICconvert\`
4. Copies `heic_wic.dll`, `libheif.dll`, `libde265.dll`
5. Runs `regsvr32 /s "C:\Program Files\HEICconvert\heic_wic.dll"`
6. Restarts `explorer.exe`
7. Clears Explorer thumbnail cache
8. Logs to `C:\ProgramData\HEICconvert\install.log`

### uninstall.ps1

1. Checks for admin elevation (exit code 3 if not admin)
2. Stops `explorer.exe` to release DLL locks
3. Runs `regsvr32 /u /s "C:\Program Files\HEICconvert\heic_wic.dll"`
4. Removes `C:\Program Files\HEICconvert\` directory (if locked, schedules removal on reboot via `MoveFileEx` with `MOVEFILE_DELAY_UNTIL_REBOOT`)
5. Restarts `explorer.exe`
6. Clears Explorer thumbnail cache
7. Logs to `C:\ProgramData\HEICconvert\install.log`

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Registration failed |
| 2 | File copy failed |
| 3 | Not running as administrator |

## Deployment

Deployed via HCL BigFix as a Fixlet or Task:
- **Action:** Run `install.ps1` with PowerShell in admin context
- **Relevance:** Windows 11 machines where `heic_wic.dll` is not registered or version is below minimum
- **Payload:** ZIP containing the three DLLs and `install.ps1`

## License

- **Our code (heic_wic.dll):** MIT
- **libheif:** LGPL-3.0 (dynamic linking)
- **libde265:** LGPL-3.0 (dynamic linking)

## Error Handling

All libheif and libde265 errors are caught and mapped to appropriate WIC `HRESULT` codes:

| Scenario | HRESULT |
|----------|---------|
| Corrupt or unreadable HEIC file | `WINCODEC_ERR_BADIMAGE` |
| Unsupported HEVC profile | `WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT` |
| Out of memory during decode | `E_OUTOFMEMORY` |
| libheif/libde265 internal error | `WINCODEC_ERR_GENERIC_ERROR` |

The codec must never crash the host process. All decode paths are wrapped in exception handlers.

## Reference Projects

- [jpegxl-wic](https://github.com/mirillis/jpegxl-wic) (Apache-2.0) — WIC codec architectural template
- [libheif](https://github.com/strukturag/libheif) (LGPL-3.0) — HEIF/HEIC decoder library
- [libde265](https://github.com/strukturag/libde265) (LGPL-3.0) — HEVC decoder library
