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
│   ├── metadata_reader.cpp/.h  # IWICMetadataQueryReader — EXIF metadata
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
| `IWICBitmapDecoder` | Entry point. Opens HEIC file via libheif, reports frame count. |
| `IWICBitmapFrameDecode` | Decodes a single image frame to BGRA32 pixels. Handles EXIF orientation. |
| `IWICMetadataQueryReader` | Exposes EXIF metadata (orientation, date taken, camera model). |
| `IClassFactory` | Standard COM factory for DLL registration. |

### DLL Exports

- `DllRegisterServer` — writes registry entries under `HKLM\SOFTWARE\Classes\CLSID\{codec-guid}` and `HKLM\SOFTWARE\Microsoft\Windows Imaging Component\Decoders\{codec-guid}`
- `DllUnregisterServer` — removes those registry entries
- `DllGetClassObject` — returns IClassFactory for COM activation
- `DllCanUnloadNow` — ref-counting for safe unload

### Data Flow

1. User double-clicks a .heic file (or Explorer generates a thumbnail)
2. Windows asks WIC: "who can decode .heic?"
3. WIC finds our codec via registry entries
4. WIC loads `heic_wic.dll`, calls `IWICBitmapDecoder::Initialize` with the file stream
5. Our codec passes the stream to libheif, which uses libde265 to decompress HEVC data
6. `IWICBitmapFrameDecode::CopyPixels` returns BGRA32 pixel data to the calling app
7. App renders the image

### Dependencies

| Library | License | Role | Linking |
|---------|---------|------|---------|
| libheif | LGPL-3.0 | HEIF container parsing | Dynamic (libheif.dll) |
| libde265 | LGPL-3.0 | HEVC decoding | Dynamic (libde265.dll) |

All LGPL dependencies are dynamically linked as separate DLLs, keeping our codec under MIT license. Users retain the right to substitute their own versions of the LGPL libraries.

### Build System

- **CMake** with `FetchContent` to pull libheif and libde265 at build time
- **Compiler:** MSVC (Visual Studio 2022)
- **Target:** x64 only (Windows 11 is 64-bit only)
- **Output:** `heic_wic.dll`, `libheif.dll`, `libde265.dll`

## Installer

### install.ps1

1. Checks for admin elevation (exit code 3 if not admin)
2. Creates `C:\Program Files\HEICconvert\`
3. Copies `heic_wic.dll`, `libheif.dll`, `libde265.dll`
4. Runs `regsvr32 /s "C:\Program Files\HEICconvert\heic_wic.dll"`
5. Clears Explorer thumbnail cache
6. Logs to `C:\ProgramData\HEICconvert\install.log`

### uninstall.ps1

1. Runs `regsvr32 /u /s "C:\Program Files\HEICconvert\heic_wic.dll"`
2. Removes `C:\Program Files\HEICconvert\` directory
3. Clears Explorer thumbnail cache
4. Logs to `C:\ProgramData\HEICconvert\install.log`

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
- **Relevance:** Windows 11 machines where `heic_wic.dll` is not registered
- **Payload:** ZIP containing the three DLLs and `install.ps1`

## License

- **Our code (heic_wic.dll):** MIT
- **libheif:** LGPL-3.0 (dynamic linking)
- **libde265:** LGPL-3.0 (dynamic linking)

## Reference Projects

- [jpegxl-wic](https://github.com/mirillis/jpegxl-wic) (Apache-2.0) — WIC codec architectural template
- [libheif](https://github.com/nicktencate/libheif) — HEIF/HEIC decoder library
- [libde265](https://github.com/nicktencate/libde265) — HEVC decoder library
