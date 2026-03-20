# HEICView — Free HEIC Image Support for Windows 11

A Windows Imaging Component (WIC) codec and shell extension that enables native HEIC/HEIF image support on Windows 11 — no Microsoft Store purchase required.

## Problem

iPhones capture photos in HEIC format. Windows 11 cannot open, preview, or thumbnail these files without the [HEVC Video Extensions](https://apps.microsoft.com/detail/9nmzlz57r3t7) from the Microsoft Store (~$0.99 per machine). This project provides a free, open-source alternative.

## How It Works

The codec is a COM DLL (`heic_wic.dll`) that bundles open-source HEIC/HEVC decoding libraries:

- **libheif** (LGPL-3.0) — parses the HEIF container format
- **libde265** (LGPL-3.0) — decodes H.265/HEVC compressed image data

It registers two components with Windows:

| Component | Interface | Purpose |
|-----------|-----------|---------|
| WIC Bitmap Decoder | `IWICBitmapDecoder` | Enables apps to decode HEIC via the Windows Imaging Component API |
| Shell Thumbnail Provider | `IThumbnailProvider` | Provides HEIC thumbnails directly to Explorer, bypassing Windows' built-in HEIF handler |

The thumbnail provider is necessary because Windows 11's `windowscodecs.dll` contains a hard-coded HEIF handler that intercepts `.heic` files before any registered WIC decoders. That built-in handler delegates to the Microsoft HEVC extension — if it's not installed, decoding fails and Windows never falls back to third-party WIC decoders. The `IThumbnailProvider` shell extension bypasses this entirely.

## Installation

### Download

Download the latest release from the [Releases](https://github.com/tmhoule/heicview/releases) page, or from the build artifacts in [Actions](https://github.com/tmhoule/heicview/actions).

The package contains:
- `heic_wic.dll` — the codec
- `heif.dll`, `libde265.dll`, `libx265.dll` — HEIF/HEVC libraries
- `install.ps1` — installer script
- `uninstall.ps1` — uninstaller script

### Install

Run PowerShell **as Administrator**:

```powershell
.\install.ps1
```

The installer:
1. Stops `explorer.exe` to release any DLL locks
2. Copies all DLLs to `C:\Program Files\HEICconvert\`
3. Registers the COM DLL via `regsvr32`, which writes:
   - **WIC decoder registration** — CLSID, file patterns (`ftypheic`, `ftypheix`, `ftypmif1`), pixel format, and WIC decoder category instance under `HKLM\SOFTWARE\Classes\CLSID`
   - **Thumbnail provider registration** — CLSID and `IThumbnailProvider` handler under `HKCR\.heic\shellex` and `HKCR\.heif\shellex`
   - **Shell extension approval** under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved`
4. Restarts `explorer.exe`
5. Clears the Windows thumbnail cache

All operations are logged to `C:\ProgramData\HEICconvert\install.log`.

### Uninstall

Run PowerShell **as Administrator**:

```powershell
.\uninstall.ps1
```

This reverses all changes: unregisters the COM DLL, removes installed files, restarts Explorer, and clears the thumbnail cache. If files are locked, they are scheduled for deletion on the next reboot.

### Silent/Enterprise Deployment

Both scripts support silent execution and return structured exit codes:

| Exit Code | Meaning |
|-----------|---------|
| 0 | Success |
| 1 | Registration/unregistration failed |
| 2 | Unexpected error |
| 3 | Not running as administrator |

Example for deployment tools (SCCM, BigFix, Intune, etc.):
```powershell
powershell -ExecutionPolicy Bypass -File install.ps1 -SourceDir "\\share\heicview"
```

## Building from Source

### Requirements

- CMake 3.24+
- Visual Studio 2022 (or MSVC Build Tools) with C++ workload
- vcpkg

### Build

```bash
git clone https://github.com/tmhoule/heicview.git
cd heicview
cmake -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Output DLLs are in `build/Release/`.

### CI

Pushes to `main` trigger a GitHub Actions build that produces a ready-to-install artifact.

## Architecture

```
iPhone photo (.heic)
        |
        v
  Explorer / Paint / Photos
        |
        +---> IThumbnailProvider (shell extension, bypasses WIC)
        |         |
        +---> IWICBitmapDecoder (WIC codec)
                  |
                  v
            heic_wic.dll
                  |
                  v
          libheif (HEIF container parsing)
                  |
                  v
          libde265 (HEVC decoding)
                  |
                  v
          BGRA32 pixel data
```

## License

MIT — see [LICENSE](LICENSE).

Dependencies (`libheif`, `libde265`) are LGPL-3.0 and dynamically linked.
