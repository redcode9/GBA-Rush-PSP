# GBA Rush PSP Single-Game Launcher

This tool builds a PSP XMB bubble that starts one GBA ROM through GBA Rush PSP.
The bubble can carry its own title, icon, background, overlay and menu audio.

## Requirements

- Windows, Linux or macOS
- Python 3.10 or newer when running from source
- `ffmpeg`, `atracdenc` and `yt-dlp` in `PATH` or beside `build_exe.py`
- A PSP launcher stub generated with PSPDev

From the repository root:

```sh
make launcher-stub
python3 -m pip install -r source/single-game_launcher/tool/requirements.txt
python3 source/single-game_launcher/tool/gui.py
```

To build a standalone desktop executable:

```sh
cd source/single-game_launcher/tool
python3 -m pip install -r requirements.txt -r requirements-build.txt
python3 build_exe.py
```

## Configure a launcher

Enter the game title, the ROM path as seen by the PSP and the output folder.
The default emulator path looks for `GBARushPSP` beside the launcher. Set an
explicit path when the emulator is installed elsewhere.

Use `ms0:/` for a Memory Stick or `ef0:/` for PSP Go internal storage:

```text
ms0:/PSP/GAME/GBARushPSP/roms/Example.gba
ef0:/PSP/GAME/GBARushPSP/roms/Example.gba
```

Supported XMB assets are:

| Asset | Size | Use |
|---|---:|---|
| `ICON0.PNG` | 144 x 80 | Game-list icon |
| `PIC1.PNG` | 480 x 272 | Background |
| `PIC0.PNG` | 310 x 180 | Foreground overlay |
| `SND0.AT3` | about 500 KiB maximum | Looping menu audio |

Images can be fitted with their aspect ratio preserved or stretched to the
target size. Audio can come from a local file or a URL supported by `yt-dlp`;
the selected segment is converted to ATRAC3.

## Output

The generated folder has this layout:

```text
GameFolder/
├── EBOOT.PBP
├── rom_path.txt
├── emulator_path.txt       only for a custom emulator location
├── assets/                 optional source assets
└── readme.txt              optional build summary
```

Copy `GameFolder` to `PSP/GAME/`. `EBOOT.PBP` and `rom_path.txt` are required.
The launcher does not embed the ROM and does not copy it to the PSP.

## Common failures

- If the PSP returns to the XMB, verify both PSP paths and confirm that the
  emulator EBOOT and ROM exist at those locations.
- If audio conversion fails, check that all three external tools are callable
  from a terminal. Shorten the segment when the AT3 file exceeds the PSP size
  limit.
- If preview audio is unavailable, install `miniaudio`; building still works
  without playback.
- If the launcher stub is missing, run `make launcher-stub` again from the
  repository root.

Desktop logs are stored under `%LOCALAPPDATA%\GBA Rush PSP Builder\logs\` on
Windows, `~/Library/Logs/GBA Rush PSP Builder/` on macOS and
`~/.cache/gba-rush-psp-builder/logs/` on Linux.

## Credits and license

The launcher and desktop builder were created by JxPv2 and inherited from
[andymcca/TempGBA4PSP-mod](https://github.com/andymcca/TempGBA4PSP-mod).
GBA Rush PSP retains that authorship and the upstream notices.

The builder is GPL-3.0-or-later. See `LICENSE`, [CREDITS](CREDITS),
[THIRD_PARTY](THIRD_PARTY), `SOURCE_OFFER.txt` and `LICENSES/`.
