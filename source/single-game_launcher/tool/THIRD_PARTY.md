# Third-Party Components

This document lists all third-party software bundled with or required by
**TempGBA4PSP-mod Single-Game Launcher Builder**, along with their licenses
and source code availability.

> This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.
> All bundled components are compatible with this license.

---

## Table of Contents

- [Python Packages](#python-packages)
- [Binary Tools](#binary-tools)
- [Source Files](#source-files)
- [Trademark Notice](#trademark-notice)

---

## License Texts

Full license texts for all bundled components are included in the `LICENSES/`
folder of this repository and the release distribution.

---

## Python Packages

Bundled in the PyInstaller release.

| Package | License | Source | Used For |
|---------|---------|--------|----------|
| **customtkinter** | [MIT](LICENSES/MIT-customtkinter.txt) | [GitHub](https://github.com/TomSchimansky/CustomTkinter) | GUI framework |
| **Pillow (PIL Fork)** | [HPND](LICENSES/HPND-Pillow.txt) | [GitHub](https://github.com/python-pillow/Pillow) | Image processing and conversion |
| **miniaudio** | [MIT](LICENSES/MIT-pyminiaudio.txt) | [GitHub](https://github.com/irmen/pyminiaudio) | Audio playback preview |
| **requests** | [Apache-2.0](LICENSES/Apache-2.0-requests.txt) | [GitHub](https://github.com/psf/requests) | HTTP image downloads |
| **yt-dlp** (Python lib) | [Unlicense](LICENSES/Unlicense-yt-dlp.txt) | [GitHub](https://github.com/yt-dlp/yt-dlp) | YouTube audio extraction |
| **PyInstaller** | [GPL-2.0+](LICENSES/GPL-2.0+-PyInstaller.txt) | [GitHub](https://github.com/pyinstaller/pyinstaller) | Building standalone executable |
| **PyInstaller Bootloader** | [GPL-2.0+](LICENSES/GPL-2.0+-PyInstaller.txt) | [GitHub](https://github.com/pyinstaller/pyinstaller) | Runtime component embedded in the `.exe` |

---

## Binary Tools

Bundled in the PyInstaller release via `--add-binary`.

### ffmpeg

| | |
|---|---|
| **License** | LGPL-2.1+ or GPL-2.0+ (depends on build configuration) |
| **Source** | [ffmpeg.org/download.html#source](https://ffmpeg.org/download.html#source) |
| **Used For** | Audio conversion and seeking |
| **Legal Info** | [ffmpeg.org/legal.html](https://ffmpeg.org/legal.html) |

> **Note:** Bundled Windows binary is **ffmpeg 7.1.1 essentials** from
> [GyanD/codexffmpeg](https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-essentials_build.zip)
> (www.gyan.dev). Run `ffmpeg -version` to confirm. Matching sources are available from
> [ffmpeg.org](https://ffmpeg.org/download.html#source). This essentials build is GPL-configured.

### atracdenc

| | |
|---|---|
| **License** | [LGPL-2.1+](LICENSES/LGPL-2.1.txt) |
| **Source** | [github.com/dcherednik/atracdenc](https://github.com/dcherednik/atracdenc) |
| **Used For** | ATRAC3 audio encoding for PSP SND0.AT3 |
| **License File** | `LICENSES/LGPL-2.1.txt` |

> **Note:** Run `atracdenc --version` to identify the exact version bundled with this release.
> The source code is available at the GitHub repository above. The LGPL requires
> that recipients of the binary be able to obtain the corresponding source code.

### yt-dlp (binary)

| | |
|---|---|
| **License** | Unlicense |
| **Source** | [github.com/yt-dlp/yt-dlp](https://github.com/yt-dlp/yt-dlp) |
| **Used For** | YouTube audio download (command-line tool) |

> **Note:** Run `yt-dlp --version` to identify the exact version bundled with this release.

---

## Source Files

### `riff.py`

| | |
|---|---|
| **Original Source** | [gba2psp](https://github.com/sahlberg/gba2psp) |
| **Original License** | GNU Lesser General Public License v2.1 (LGPL-2.1) |
| **License URL** | [www.gnu.org/licenses/old-licenses/lgpl-2.1.html](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) |
| **License File** | `LICENSES/LGPL-2.1.txt` |
| **Used For** | RIFF/WAVE AT3 wrapper for PSP SND0.AT3 creation |
| **Modified By** | JxP (2026) |

#### Modifications from Original

```
Modifications Copyright (C) 2026 JxP
Original licensed under LGPL-2.1
```

1. **Refactored from standalone CLI script to importable Python module**
   - Removed `argparse`, `__main__` block, and `print()`/`exit()`-based I/O

2. **Removed `copy_riff()` function**
   - Not needed for module use

3. **Added `RiffError` exception class**
   - For proper programmatic error handling

4. **Silent failure in `parse_riff()`**
   - Returns `None` instead of printing errors to stdout

5. **Removed `dump_riff()` function**
   - CLI debug output, not needed for module use

6. **`create_riff()` raises `RiffError`**
   - Instead of printing and calling `exit()`

7. **Named constant `ATRAC3_STEREO_BYTES_PER_SEC = 16537`**
   - Was previously a bare magic number

8. **Critical bugfix: `loop_end` clamping**
   - Added `max(0x800, ...)` to prevent underflow on short audio files
   - Original calculation preserved as inline comment

9. **Code style cleanup**
   - Single quotes → double quotes
   - Explicit escapes instead of ` `
   - `pass` instead of `True` in empty branches

10. **Added module docstring**
    - Includes attribution, original source URL, license reference, and modification summary

11. **Removed unused `max_data_size` parameter from `create_riff()`**
    - Size limiting is handled by the caller (`convert_audio()`)
    - Simplifies API surface

12. **Added SPDX license header**
    - Standard `# SPDX-License-Identifier` comment for automated license detection

13. **Moved `ATRAC3_STEREO_BYTES_PER_SEC` to shared `constants.py`**
    - Eliminated duplication with `convert.py`; single source of truth

14. **Removed `parse_riff()` function**
    - Dead code, never called by the builder; removed to reduce module surface
---

## Fonts

### DejaVu Sans

| | |
|---|---|
| **License** | [DejaVu Fonts License](LICENSES/DejaVu-fonts.txt) (based on Bitstream Vera Fonts License) |
| **License URL** | [dejavu-fonts.github.io/License.html](https://dejavu-fonts.github.io/License.html) |
| **Source** | [github.com/dejavu-fonts/dejavu-fonts](https://github.com/dejavu-fonts/dejavu-fonts) |
| **Used For** | XMB preview title rendering |
| **Bundled File** | `assets/DejaVuSans.ttf` |
| **License File** | `LICENSES/DejaVu-fonts.txt` |
| **Modifications** | None — distributed verbatim |

> The DejaVu Fonts License permits free use, modification, and redistribution
> of the fonts, provided the license and copyright notice are included.
> See the license URL above for full terms.

---

## Trademark Notice

**FFmpeg** is a trademark of Fabrice Bellard, originator of the FFmpeg project.
