# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
PyInstaller build script for creating a standalone Windows executable.

Output layout:
    build_exe_output/
        TempGBA4PSP-mod Single-Game Launcher Builder.spec
        dist/
            TempGBA4PSP-mod Single-Game Launcher Builder.exe
            LICENSE.txt
            README.md
            SOURCE_OFFER.txt
            THIRD_PARTY.md
            LICENSES/
        build/
            TempGBA4PSP-mod Single-Game Launcher Builder/   ← PyInstaller intermediates
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path
import constants as C


APP_NAME = "TempGBA4PSP-mod Single-Game Launcher Builder"


def main():
    here = Path(__file__).parent
    output_dir = here / "build_exe_output"

    # Ensure output directory exists
    output_dir.mkdir(parents=True, exist_ok=True)

    # Auto-install PyInstaller if missing
    try:
        import PyInstaller
    except ImportError:
        print("Installing PyInstaller...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyinstaller"])

    # Determine binary extensions per platform
    if sys.platform == "win32":
        ffmpeg_bin = "ffmpeg.exe"
        atracdenc_bin = "atracdenc.exe"
        ytdlp_bin = "yt-dlp.exe"
    else:
        ffmpeg_bin = "ffmpeg"
        atracdenc_bin = "atracdenc"
        ytdlp_bin = "yt-dlp"

    separator = os.pathsep

    dist_dir = output_dir / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)
    build_dir = output_dir / "build"

    # Base PyInstaller command
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--name", APP_NAME,
        "--onefile",
        "--windowed",
        "--noconfirm",
        "--distpath", str(dist_dir),         # put .exe in dist/
        "--workpath", str(build_dir),        # put intermediates in build/
        "--specpath", str(output_dir),       # .spec goes here
        "--add-data", f"{here / 'assets'}{separator}assets",
        "--add-data", f"{here / 'LICENSES'}{separator}LICENSES",
        "--add-data", f"{here / 'CREDITS.md'}{separator}.",
        "--add-binary", f"{here / ffmpeg_bin}{separator}.",
        "--add-binary", f"{here / atracdenc_bin}{separator}.",
        "--add-binary", f"{here / ytdlp_bin}{separator}.",
        str(here / "gui.py")
    ]

    # Add icon if available
    icon_file = None
    if sys.platform == "win32":
        icon_file = here / "assets" / "icon.ico"
    elif sys.platform == "darwin":
        icon_file = here / "assets" / "icon.icns"
    else:
        icon_file = here / "assets" / "icon.png"

    if icon_file and icon_file.exists():
        cmd.extend(["--icon", str(icon_file)])

    # --- Pre-flight checks ---

    # Verify required binaries exist
    for tool in [ffmpeg_bin, atracdenc_bin, ytdlp_bin]:
        if not (here / tool).exists():
            print(f"ERROR: Required binary not found: {tool}")
            print(f"  Expected at: {here / tool}")
            print("  Place ffmpeg, atracdenc, and yt-dlp in the project root.")
            sys.exit(1)

    # Verify assets folder exists
    assets_dir = here / "assets"
    if not assets_dir.exists():
        print("ERROR: Required folder not found: assets/")
        print(f"  Expected at: {assets_dir}")
        print("  Place EBOOT.PBP, default images, and icon files in this folder.")
        sys.exit(1)

    eboot_stub = assets_dir / "EBOOT.PBP"
    if not eboot_stub.exists():
        print("ERROR: Required file not found: assets/EBOOT.PBP")
        print(f"  Expected at: {eboot_stub}")
        print("  This is the launcher stub required for all builds.")
        sys.exit(1)

    # Verify critical preview assets
    critical_assets = {
        C.FONT_NAME: "Font for XMB preview title rendering",
        C.DEFAULT_BG_NAME: "Default XMB background",
        C.DEFAULT_ICON_NAME: "Default icon placeholder",
        C.DEFAULT_CHROME_NAME: "XMB chrome overlay",
    }

    for asset_name, purpose in critical_assets.items():
        asset_path = assets_dir / asset_name
        if not asset_path.exists():
            print(f"WARNING: Missing asset: assets/{asset_name}")
            print(f"  Purpose: {purpose}")
            print(f"  The app will still build but preview quality may be degraded.")

    # DejaVu font license check
    dejavu_license = here / "LICENSES" / "DejaVu-fonts.txt"
    if not dejavu_license.exists():
        print("WARNING: LICENSES/DejaVu-fonts.txt not found.")
        print("  This file is required for release packaging (font compliance).")
        print("  Download from: https://dejavu-fonts.github.io/License.html")

    print("Running PyInstaller...")
    subprocess.check_call(cmd)

    exe_name = APP_NAME
    if sys.platform == "win32":
        exe_name += ".exe"

    print(f"Done.")
    print(f"  Executable: {dist_dir / exe_name}")
    print(f"  Spec:       {output_dir / (APP_NAME + '.spec')}")
    print(f"  Build:      {build_dir / APP_NAME}")

    # --- Copy LICENSES folder ---
    licenses_src = here / "LICENSES"
    licenses_dst = dist_dir / "LICENSES"
    if licenses_src.exists():
        if licenses_dst.exists():
            shutil.rmtree(str(licenses_dst))
        shutil.copytree(str(licenses_src), str(licenses_dst))
        print(f"  Copied:     {licenses_dst}")
    else:
        print(f"  WARNING:    LICENSES/ folder not found — release may not be compliant")

    # --- Copy release files ---
    files_to_ship = [
        ("README.md", here / "README.md"),
        ("LICENSE.txt", here / "LICENSE"),
        ("THIRD_PARTY.md", here / "THIRD_PARTY.md"),
        ("SOURCE_OFFER.txt", here / "SOURCE_OFFER.txt"),
        ("CHANGELOG.md", here / "CHANGELOG.md"),
    ]

    for dest_name, src_path in files_to_ship:
        if src_path.exists():
            dest = dist_dir / dest_name
            shutil.copy2(str(src_path), str(dest))
            print(f"  Copied:     {dest}")
        else:
            print(f"  WARNING:    Missing {src_path.name} — not copied")

    print(f"\nRelease folder ready: {dist_dir}")


if __name__ == "__main__":
    main()