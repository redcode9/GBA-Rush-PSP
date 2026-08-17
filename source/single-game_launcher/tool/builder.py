# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
PBP injection and build orchestration.

This module handles:
  - Parsing and editing the PARAM.SFO inside an EBOOT.PBP stub
  - Replacing image/audio sections in the PBP
  - Converting user assets to PSP-compatible formats
  - Writing the final output folder with EBOOT.PBP, text files, and readme
"""

import struct
from pathlib import Path
from typing import Optional, Dict, List, Tuple

from convert import convert_icon, convert_pic, convert_pic0, convert_audio
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")


class PBPBuilderError(Exception):
    """Raised when PBP/SFO parsing or modification fails."""
    pass


class SFOEditor:
    """
    Generic SFO parser and editor.

    Finds the TITLE entry by key name and rebuilds the data table with correct
    offsets regardless of the stub's original layout. This avoids hardcoded
    offsets that break when different EBOOT stubs are used.
    """

    def __init__(self, data: bytes):
        self.raw = bytearray(data)
        if len(self.raw) < 20 or self.raw[:4] != b'\x00PSF':
            raise PBPBuilderError("Invalid SFO signature")

        # Parse SFO header
        self.key_offset = struct.unpack('<I', self.raw[8:12])[0]
        self.data_offset = struct.unpack('<I', self.raw[12:16])[0]
        self.count = struct.unpack('<I', self.raw[16:20])[0]
        self.entries = []
        self._parse_entries()

    def _read_cstring(self, offset: int, max_len: int = 256) -> str:
        """Read null-terminated string at given offset."""
        end = min(offset + max_len, len(self.raw))
        buf = bytearray()
        for i in range(offset, end):
            if self.raw[i] == 0:
                break
            buf.append(self.raw[i])
        return bytes(buf).decode('utf-8', errors='replace')

    def _parse_entries(self):
        """Parse all SFO entries into self.entries list."""
        for i in range(self.count):
            off = 20 + i * 16
            name_off = struct.unpack('<H', self.raw[off:off+2])[0]
            align = self.raw[off+2]
            data_type = self.raw[off+3]
            data_size = struct.unpack('<I', self.raw[off+4:off+8])[0]
            data_max_size = struct.unpack('<I', self.raw[off+8:off+12])[0]
            data_rel_off = struct.unpack('<I', self.raw[off+12:off+16])[0]

            key = self._read_cstring(self.key_offset + name_off)

            self.entries.append({
                'key': key,
                'type': data_type,
                'align': align,
                'size': data_size,
                'max_size': data_max_size,
                'data_rel_off': data_rel_off,
            })

    def set_title(self, title: str):
        """
        Find TITLE entry by name, rebuild data table with new title.

        The new title is capped at 127 UTF-8 bytes and null-padded to a
        4-byte boundary. All entries after TITLE have their data_rel_off
        updated to account for the size change.
        """
        # Prepare title bytes
        title_bytes = title.encode('utf-8', errors='replace')[:C.MAX_TITLE_UTF8_BYTES]
        new_size = len(title_bytes) + 1
        new_max_size = ((new_size + 3) // 4) * 4
        if new_max_size > 128:
            new_max_size = 128

        # Find TITLE entry index
        title_idx = None
        for i, e in enumerate(self.entries):
            if e['key'] == 'TITLE':
                title_idx = i
                break
        if title_idx is None:
            raise PBPBuilderError("TITLE key not found in SFO")

        title_entry = self.entries[title_idx]

        # --- Rebuild data table ---
        # Start with everything before TITLE's data
        new_data_table = bytearray()
        title_data_start = self.data_offset + title_entry['data_rel_off']

        # Copy all data before TITLE's position
        new_data_table += self.raw[self.data_offset:title_data_start]

        # Write new title block (null-padded to new_max_size)
        new_data_table += title_bytes + b'\x00'
        pad = new_max_size - len(title_bytes) - 1
        if pad > 0:
            new_data_table += b'\x00' * pad

        # Append everything after TITLE's original block
        old_block_end = title_data_start + title_entry['max_size']
        remaining = self.raw[old_block_end:]
        new_data_table += remaining

        # --- Update all entries' data_rel_off for entries after TITLE ---
        # Entries before TITLE keep their offsets
        # Entries after TITLE shift by (new_max_size - old_max_size)
        size_delta = new_max_size - title_entry['max_size']

        for i, e in enumerate(self.entries):
            entry_off = 20 + i * 16
            if i > title_idx:
                new_rel = e['data_rel_off'] + size_delta
                self.raw[entry_off+12:entry_off+16] = struct.pack('<I', new_rel)
                e['data_rel_off'] = new_rel

        # Update TITLE's own size fields (data_rel_off stays the same)
        title_entry_off = 20 + title_idx * 16
        self.raw[title_entry_off+4:title_entry_off+8] = struct.pack('<I', new_size)
        self.raw[title_entry_off+8:title_entry_off+12] = struct.pack('<I', new_max_size)
        title_entry['size'] = new_size
        title_entry['max_size'] = new_max_size

        # --- Reassemble SFO ---
        # Keep header + entry table + key table intact
        # Replace data table with rebuilt one
        self.raw = self.raw[:self.data_offset] + new_data_table

    def to_bytes(self) -> bytes:
        """Return the modified SFO as bytes."""
        return bytes(self.raw)


class PBPBuilder:
    """
    Parser and builder for PSP EBOOT.PBP files.

    PBP format: 0x28-byte header followed by 8 sections:
      0: SFO (PARAM.SFO)
      1: ICON0.PNG
      2: ICON1.PMF
      3: PIC0.PNG
      4: PIC1.PNG
      5: SND0.AT3
      6: DATA.PSP
      7: DATA.PSAR
    """

    SECTION_SFO = 0
    SECTION_ICON0 = 1
    SECTION_ICON1 = 2
    SECTION_PIC0 = 3
    SECTION_PIC1 = 4
    SECTION_SND0 = 5
    SECTION_DATA_PSP = 6
    SECTION_DATA_PSAR = 7

    def __init__(self, stub_path: Path):
        self.stub_path = stub_path
        self.sections: Dict[int, bytes] = {}
        self._parse()

    def _parse(self):
        """
        Read the stub EBOOT.PBP and extract all sections.

        Each section's size is determined by the difference between its offset
        and the next non-zero offset in the header.
        """
        with open(self.stub_path, 'rb') as f:
            data = f.read()

        if data[:4] != b'\x00PBP':
            raise PBPBuilderError("Invalid PBP")

        # 8 section offsets at 0x08-0x27
        offsets = struct.unpack('<8I', data[0x08:0x28])

        for i in range(8):
            start = offsets[i]
            if start == 0:
                continue
            # Section end = start of next section, or EOF if last
            end = len(data)
            for j in range(i + 1, 8):
                if offsets[j] != 0:
                    end = offsets[j]
                    break
            self.sections[i] = data[start:end]

    def set_section(self, section_id: int, data: Optional[bytes]):
        """Replace or remove a section by ID."""
        if data is None:
            self.sections.pop(section_id, None)
        else:
            self.sections[section_id] = data

    def build(self, output_path: Path):
        """
        Write the modified PBP to disk.

        Recalculates all section offsets with 4-byte alignment.
        """
        offsets = [0] * 8
        current = 0x28

        # Calculate offsets with 4-byte alignment
        for i in range(8):
            if i in self.sections:
                offsets[i] = current
                current += len(self.sections[i])
                # Align to 4 bytes
                pad = (4 - (current % 4)) % 4
                current += pad
            else:
                offsets[i] = 0

        with open(output_path, 'wb') as f:
            f.write(b'\x00PBP')
            f.write(struct.pack('<I', 0x00010000))
            f.write(struct.pack('<8I', *offsets))
            for i in range(8):
                if i in self.sections:
                    f.write(self.sections[i])
                    # Write alignment padding
                    pad = (4 - (len(self.sections[i]) % 4)) % 4
                    f.write(b'\x00' * pad)

    def get_sfo(self) -> SFOEditor:
        """Extract and return the SFO section as an editable SFOEditor."""
        if self.SECTION_SFO not in self.sections:
            raise PBPBuilderError("No SFO in EBOOT.PBP")
        return SFOEditor(self.sections[self.SECTION_SFO])

    def set_sfo(self, sfo: SFOEditor):
        """Replace the SFO section with modified data."""
        self.sections[self.SECTION_SFO] = sfo.to_bytes()


class SingleGameBuilder:
    """
    High-level builder that orchestrates asset conversion and PBP injection.

    Usage:
        builder = SingleGameBuilder(stub_path, output_dir)
        builder.set_title("My Game")
        builder.set_icon(icon_path, mode)
        builder.set_pic1(pic1_path, mode)
        builder.set_pic0(pic0_path, mode)
        builder.set_snd0(audio_path, loop, start_ms, end_ms)
        builder.write_text_files(rom_path, emu_path)
        builder.write_readme(rom_path, emu_path, title)
        log = builder.build()
    """

    def __init__(self, stub_path: Path, output_dir: Path):
        self.stub_path = stub_path
        self.output_dir = output_dir
        self.builder = PBPBuilder(stub_path)
        self.assets_dir = output_dir / "assets"
        # Build log: list of 3-tuples (status, label, value) for results display
        self.log: List[Tuple[str, ...]] = []

    def set_title(self, title: str):
        """Set the PSP XMB title in the SFO."""
        try:
            sfo = self.builder.get_sfo()
            sfo.set_title(title)
            self.builder.set_sfo(sfo)
            self.log.append(("OK", "Title set to:", title))
        except Exception as e:
            logger.warning("Failed to set SFO title: %s", e)
            self.log.append(("WARN", "Title", f"Could not set: {e}"))

    def set_icon(self, icon_path: Path, mode: str):
        """Convert and inject ICON0.PNG."""
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        out = self.assets_dir / "ICON0.PNG"
        # Pass GUI dropdown string directly to convert_icon
        convert_icon(icon_path, mode, out)
        with open(out, 'rb') as f:
            self.builder.set_section(PBPBuilder.SECTION_ICON0, f.read())
        self.log.append(("OK", "ICON0 converted:", out.name))

    def set_pic0(self, pic_path: Path, mode: str = "stretch"):
        """
        Convert and inject PIC0.PNG (overlay).

        PIC0 is always 310x180. The image is resized per mode and positioned
        at the bottom-right corner.
        """
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        out = self.assets_dir / "PIC0.PNG"
        # Pass GUI dropdown string directly to convert_pic0
        convert_pic0(pic_path, out, mode=mode)

        with open(out, 'rb') as f:
            self.builder.set_section(PBPBuilder.SECTION_PIC0, f.read())
        self.log.append(("OK", "PIC0 converted:", out.name))

    def set_pic1(self, pic_path: Path, mode: str = "stretch"):
        """
        Convert and inject PIC1.PNG (background).

        PIC1 is always 480x272.
        """
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        out = self.assets_dir / "PIC1.PNG"
        # Pass GUI dropdown string directly to convert_pic
        convert_pic(pic_path, out, size=(480, 272), mode=mode)
        with open(out, 'rb') as f:
            self.builder.set_section(PBPBuilder.SECTION_PIC1, f.read())
        self.log.append(("OK", "PIC1 converted:", out.name))

    def set_snd0(self, audio_path: Path, loop: bool = False, start_ms: int = 0, end_ms: int = 0):
        """Convert audio to SND0.AT3 and inject into the PBP."""
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        out = self.assets_dir / "SND0.AT3"
        convert_audio(audio_path, out, loop, start_ms=start_ms, end_ms=end_ms)
        with open(out, 'rb') as f:
            self.builder.set_section(PBPBuilder.SECTION_SND0, f.read())
        self.log.append(("OK", "SND0 converted:", out.name))

    def write_text_files(self, rom_path: str, emu_path: str):
        """
        Write rom_path.txt and emulator_path.txt to the output folder.

        These text files tell the launcher stub where to find the GBA ROM
        and the TempGBA4PSP-mod emulator on the PSP.
        """
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # --- rom_path.txt ---
        rom_lines = [
            "# TempGBA4PSP-mod Single-Game Launcher",
            "# This file MUST contain the full path to your GBA ROM on the PSP.",
            "# Example: ms0:/PSP/GAME/tempgba4psp-mod/roms/MyGame.gba",
            "#",
        ]
        if rom_path:
            rom_lines.append(rom_path)
        else:
            rom_lines.append("# IMPORTANT: Add your ROM path on the line below before using on PSP.")
            rom_lines.append("# (Remove this comment and replace it with your actual ROM path)")

        with open(self.output_dir / "rom_path.txt", "w") as f:
            f.write("\n".join(rom_lines) + "\n")

        # --- emulator_path.txt ---
        # Directory form needs a trailing slash for stub path concatenation.
        # Full .pbp boot paths must stay as-is (stub treats trailing '/' as a dir).
        if emu_path:
            emu_path = emu_path.strip()
            stripped = emu_path.rstrip("/")
            if stripped.lower().endswith(".pbp"):
                emu_path = stripped
            elif not emu_path.endswith("/"):
                emu_path += "/"

        emu_lines = [
            "# TempGBA4PSP-mod Single-Game Launcher",
            "# This file is OPTIONAL if your TempGBA4PSP-mod emulator folder is named",
            '# "tempgba4psp-mod" and is in the same parent folder as this launcher folder.',
            "# It is ONLY required if the emulator folder or eboot file have a different name or are in a different parent folder.",
            "# Example: ms0:/PSP/GAME/tempgba4psp-mod/",
            "# Example: ms0:/PSP/GAME/tempgba4psp-mod/EBOOT.PBP",
            "#",
        ]
        if emu_path:
            emu_lines.append(emu_path)
        else:
            emu_lines.append("# (No override set — launcher will auto-detect emulator folder)")
            emu_lines.append("# To override, remove the line above and add your path on the next line.")

        with open(self.output_dir / "emulator_path.txt", "w") as f:
            f.write("\n".join(emu_lines) + "\n")

        self.log.append(("OK", "rom_path.txt", "written"))
        self.log.append(("OK", "emulator_path.txt", "written"))

    def write_readme(self, rom_path: str, emu_path: str, title: str):
        """Write a human-readable readme.txt with installation instructions."""
        text = f"""TempGBA4PSP-mod Single-Game Launcher Package
=====================================
Game: {title}

Install this entire folder to your PSP:
  PSP/GAME/<foldername>/

--- rom_path.txt ---
This file MUST contain the full path to your GBA ROM on the PSP.
The launcher cannot work without it.

Example:
  ms0:/PSP/GAME/tempgba4psp-mod/roms/MyGame.gba

--- emulator_path.txt ---
This file is OPTIONAL if your TempGBA4PSP-mod emulator is installed in the
same parent folder with the default folder name "TempGBA4PSP-mod".
It is ONLY required if:
  - The emulator folder has a different name
  - The emulator eboot has a different name
  - The emulator is in a different parent folder than this launcher folder.

Example (when required):
  ms0:/PSP/GAME/tempgba4psp-mod/EBOOT.PBP

"""
        with open(self.output_dir / "readme.txt", "w") as f:
            f.write(text)
        self.log.append(("OK", "readme.txt", "written"))

    def build(self):
        """Write the final EBOOT.PBP and return the build log."""
        self.output_dir.mkdir(parents=True, exist_ok=True)
        eboot_path = self.output_dir / "EBOOT.PBP"
        self.builder.build(eboot_path)
        self.log.append(("OK", "EBOOT.PBP", "rebuilt"))
        return self.log
