# Single-game integration — confirmed bugs

Review of the JxPv2 `single-game` integration (commits `0aeb520`, `ac1e707` on `master`).

Only **confirmed** defects are listed. Design gaps, hardening ideas, and intentional risky patterns are omitted.

---

## Summary

| ID | Priority | Component | Nature |
|----|----------|-----------|--------|
| H1 | High | PC **builder tool** | Breaks documented PBP-path override in `emulator_path.txt` |
| M1 | Medium | **Emulator** | Recent + D-pad focus mismatch |
| M2 | Medium | **Emulator** | R-scroll polish with Recent visible |
| L1 | Low | **Launcher stub** | Error-message typo |
| H5 | Low | **Emulator** | Single-game boot paths incorrectly call `add_recent_rom` |

---

## H1 — Builder forces trailing `/` on emulator paths

**Where it lives: the PC builder tool**, not the emulator.

Specifically `source/single-game_launcher/tool/builder.py` when writing `emulator_path.txt`.

### How the pieces relate

| Piece | Role |
|-------|------|
| PC builder (`source/single-game_launcher/tool/`) | Builds the XMB bubble folder, including `emulator_path.txt` |
| PSP launcher stub (`source/single-game_launcher/main.c`) | Reads that file and chain-loads the emu |
| Emulator | Not involved in this bug |

The stub already supports two forms of `emulator_path.txt`:

1. **Directory:** `ms0:/PSP/GAME/tempgba4psp-mod/` → append `EBOOT.PBP`
2. **Full boot file:** `ms0:/PSP/GAME/tempgba4psp-mod/EBOOT.PBP` → use as-is

The stub’s `ensure_trailing_slash` correctly **does not** add `/` when the path ends in `.pbp`.

The builder disagrees: before writing, it always does “if no trailing `/`, append one.” So a user (or the readme example) that puts a full PBP path gets:

```text
ms0:/…/EBOOT.PBP   → written as →   ms0:/…/EBOOT.PBP/
```

The stub then sees a trailing `/`, treats it as a **directory**, and looks for `EBOOT.PBP` *inside* that path → boot file not found.

Normal builder UI (dir placeholder like `PSP/GAME/tempgba4psp-mod/`) is fine. This only breaks the **documented alternate** “point at EBOOT.PBP” override.

H1 does **not** change emulator ROM loading; it’s packaging/path text written by the Windows/Python tool for the stub to read.

### Proposed fix (tool only)

In `write_text_files`, append `/` only for non-`.pbp` paths (same rule as the stub):

```python
p = emu_path.rstrip()
if p and not p.endswith("/") and not p.lower().endswith(".pbp"):
    emu_path = p + "/"
```

No emulator or stub change required for this one.

---

## M1 — Recent section vs Left/Right

**Where:** Emulator — `source/src/gui.c` (CURSOR_LEFT / CURSOR_RIGHT ~1996–2010, SELECT ~2013)

### What’s wrong

Left/Right switch FILE↔DIR columns but never clear `in_recent_section`. Cross still prefers Recent when that flag is set.

### How it fails

Highlight/cursor can look like you’re on the directory column, but confirm still loads a Recent ROM.

### Proposed fix

Set `in_recent_section = 0` whenever Left/Right changes column (and when entering DIR_LIST). Optionally refuse to leave FILE while Recent is “active” until Down exits Recent first — clearing the flag is enough.

---

## M2 — R-trigger scroll vs Recent-shrunk row count

**Where:** Emulator — `source/src/gui.c` (CURSOR_RTRIGGER ~1880–1913)

### What’s wrong

Up/Down already use per-column `visible_rows[]` (FILE shrinks when Recent is shown; DIR stays full height). R-trigger still mixes `file_list_visible_rows` and `FILE_LIST_ROWS`, including when focus is on DIR.

### How it fails

Page-scroll / highlight can land wrong after R when the Recent block is visible (and DIR can use the wrong row count).

### Proposed fix

In R/L-trigger handlers, use `visible_rows[column]` consistently for clamps (same as Down already does); drop the hard-coded `FILE_LIST_ROWS` clamp on the FILE path when Recent is active.

---

## L1 — Wrong `snprintf` in stub error text

**Where:** PSP launcher stub — `source/single-game_launcher/main.c` (~363–369)

### What’s wrong

Format string has no `%s`, but `DEFAULT_EMU_FOLDER` is passed as an argument. Help text is incomplete; warning possible at compile time.

### How it fails

Cosmetics only — path is still shown via `error_screen`’s third argument. Auto-detect failure message is just less clear.

### Proposed fix

Put the expected folder name in the format (`"... Expected: %s"`, `DEFAULT_EMU_FOLDER`) or drop the unused argument and hard-code the name in the string.

---

## H5 — Single-game boot paths should not update Recent ROMs

**Where:** Emulator — `source/src/main.c` (~898, ~918)  
**Priority:** Low

### Intent

There are two single-game boot modes:

1. **Bubble / launcher** — ROM path passed as `argv[1]`
2. **GrabowskiDev-style drop-in** — `roms/game.gba` auto-loads when present

Both are fixed single-ROM packages. Recent ROMs is for the multi-game file browser only. Neither single-game mode should add anything to the Recent list.

### What’s wrong

Both paths still call `add_recent_rom` after a successful load. That is unnecessary (and for `game.gba`, against drop-in intent). Side effects can include writing a useless or corrupt `recent.cfg` entry (device paths are also mishandled by `add_recent_rom`’s absolute-path check), even though the user never needs Recent in these modes.

### Proposed fix

Skip `add_recent_rom` on both single-game boot paths:

- Remove / don’t call it after `argv[1]` load
- Remove / don’t call it after `roms/game.gba` load

Keep `add_recent_rom` only for the normal browser / menu load path. Hardening `add_recent_rom` for `ms0:` absolutes is optional and not required to satisfy this bug once those callers are removed.
