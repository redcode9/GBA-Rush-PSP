# GBA Rush PSP Single-Game Launcher

Build custom PSP XMB bubbles that launch a single GBA game through
GBA Rush PSP.

---

## What is this?

This tool creates ready-to-install `EBOOT.PBP` packages for your PSP. Each package is a standalone XMB bubble with your chosen game title, icon, background, overlay image, and menu audio. No command-line knowledge needed — everything is click-and-build.

---

## Features

- **Game Info** — Set title, ROM path, emulator path, and output folder
- **Images** — Choose ICON0 (icon), PIC1 (background), and PIC0 (overlay) with live XMB preview
- **Audio** — Add looping SND0.AT3 menu audio from local files or YouTube
- **Build** — One-click package generation with full summary review

---

## Requirements

- **Windows 10/11**, **Linux**, or **macOS**
- Python 3.10+ (if running from source)

The following tools must be available in `PATH` or beside `build_exe.py`:

| Tool | Purpose |
|------|---------|
| `ffmpeg` | Audio conversion & seeking |
| `atracdenc` | ATRAC3 encoder for PSP SND0.AT3 |
| `yt-dlp` | YouTube audio downloads |

---

## Quick Start

First build the PSP launcher template from the repository root with a PSPDev
toolchain:

```bash
make launcher-stub
```

Then run the desktop tool from source:

```bash
cd source/single-game_launcher/tool
python3 -m pip install -r requirements.txt
python3 gui.py
```

### Build a standalone executable

```bash
python3 -m pip install -r requirements.txt -r requirements-build.txt
python3 build_exe.py
```

Output: `build_exe_output/dist/GBA Rush PSP Single-Game Launcher Builder.exe`

---

## How to Use

### 1. Game Info Tab

| Field | What to enter |
|-------|---------------|
| **Game Title** | Name shown on PSP XMB. Max 127 UTF-8 bytes. |
| **ROM PSP Path** | Full path to your `.gba` file on PSP. Use `ms0:/` for Memory Stick or `ef0:/` for PSP Go internal storage. |
| **Emulator PSP Path** | Where GBA Rush PSP is installed on your PSP. Optional if using the default `GBARushPSP` folder in the parent directory. |
| **Output Folder** | Where the finished package is saved on your PC. Auto-updates based on Game Title. |

> **Tip:** The Output Folder auto-updates when you type a Game Title. Browse to change the base directory.

### 2. Images Tab

| Asset | Size | Purpose |
|-------|------|---------|
| **ICON0** | 144×80 | Game icon in the XMB game list |
| **PIC1** | 480×272 | Full-screen background behind the XMB |
| **PIC0** | 310×180 | Overlay image (bottom-right corner) |

**Modes:**
- **Fit** — Maintains aspect ratio, transparent letterboxing
- **Stretch** — Fills the entire target resolution

**XMB Preview:** Shows exactly how your bubble appears on a real PSP, including authentic icon slot position and chrome overlay.

**Load from URL:** Paste a direct image URL (http/https) and click **Load URL** to download automatically.

### 3. Audio Tab

| Source | How it works |
|--------|-------------|
| **Local File** | Browse for `.mp3`, `.wav`, `.flac`, `.ogg`, or `.m4a` |
| **YouTube URL** | Paste a YouTube link; downloads and converts automatically |

**Segment Selection:**
- Set **Start** and **End** times in `HH:MM:SS` format
- Click **Set Start** / **Set End** while previewing to mark positions
- Leave **End** blank for auto-trim to the PSP ~500KB limit

**Controls:**
- **Play** — Preview the full file from current position
- **Play Segment** — Preview only the selected range
- **Loop** — Repeat continuously
- **Timeline** — Click or drag to seek

> **Note:** PSP SND0.AT3 has a hard 500KB size limit. Long tracks are auto-trimmed to fit.

### 4. Build Tab

Review the **Build Summary**, then click **Build Package**.

The output folder contains:

```
YourGameFolder/
├── EBOOT.PBP          ← Custom launcher with your assets injected
├── rom_path.txt       ← Points to your GBA ROM on the PSP
├── emulator_path.txt  ← Points to GBA Rush PSP on the PSP
├── assets/
│   ├── ICON0.PNG
│   ├── PIC0.PNG
│   ├── PIC1.PNG
│   └── SND0.AT3
└── readme.txt
```

**Required files:** `EBOOT.PBP` and `rom_path.txt` must both be present in the folder.
**Conditional:** `emulator_path.txt` is only needed if you set a custom emulator path (default is auto-detected).
**Optional:** `assets/` (icon, background, audio) and `readme.txt` are cosmetic — the launcher works without them.

Copy the folder to your PSP:
```
PSP/GAME/YourGameFolder/
```

---

## PSP Path Reference

| Device | Prefix | Example |
|--------|--------|---------|
| PSP (Memory Stick) | `ms0:/` | `ms0:/PSP/GAME/GBARushPSP/roms/SampleGame.gba` |
| PSP Go (Internal) | `ef0:/` | `ef0:/PSP/GAME/GBARushPSP/roms/SampleGame.gba` |

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Audio preview unavailable" | miniaudio is not installed or no audio device detected. You can still configure audio and build — playback just won't preview. |
| "AT3 output too large" | Auto-trim couldn't compress below 500KB even at 5 seconds. Use a shorter source file or lower bitrate source. |
| "YouTube download failed" | Check internet connection. Some videos are geo-blocked or require cookies. |
| Preview doesn't update | Click **Refresh Preview** or switch tabs to force a redraw. |
| Build button grayed out | Fix the highlighted validation error and try again. |
| Need to report a bug | Attach the log file from `%LOCALAPPDATA%\GBA Rush PSP Builder\logs\` (Windows), `~/Library/Logs/GBA Rush PSP Builder/` (macOS), or `~/.cache/gba-rush-psp-builder/logs/` (Linux). |

---

## Building from Source (Developers)

```bash
# Install dependencies
python3 -m pip install -r requirements.txt

# Run the GUI
python3 gui.py

# Build standalone executable
python3 build_exe.py
```

Make sure `ffmpeg`, `atracdenc`, and `yt-dlp` binaries are in the project root or in your system PATH before running `build_exe.py`.

---

## Credits

See [CREDITS.md](CREDITS.md) for full attribution of the emulator lineage, third-party libraries, and special thanks.

## License

GBA Rush PSP Single-Game Launcher Builder is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

See [LICENSE](LICENSE) for the full legal text. Third-party components and their licenses are documented in [THIRD_PARTY.md](THIRD_PARTY.md).
