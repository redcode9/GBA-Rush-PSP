# Saves

GBA Rush PSP keeps each game's save in a `.sav` file and writes it so that a
power cut or a full Memory Stick never leaves it empty or cut short. This page
lists the files you may find beside it and how to put an older one back.

`<rom>` below is the ROM's file name without its extension:
`Pokemon - FireRed.gba` saves to `Pokemon - FireRed.sav`.

## Where the files are

Saves go to the folder named by `save_directory` in `dir.ini`, savestates to
the one named by `save_state_directory`. Without `dir.ini`, or when a folder
it names cannot be opened, they go to the folder that holds the EBOOT.

| File | What it is |
|---|---|
| `<rom>.sav` | The save. |
| `<rom>.sav.tmp` | A write that has not finished. |
| `<rom>.sav.bak` | The save as it was before the last write. |
| `<rom>.sav.orig` | The file as it was before its size changed. |
| `<rom>.sav.link1` to `.link3` | Copies made before a link session. |

## The save

The `.sav` has the size of the game's save chip: 32 or 64 KB for SRAM, 64 or
128 KB for Flash, 512 bytes or 8 KB for EEPROM. The chip comes from the save
library string inside the ROM, not from the size of the file; only a ROM
without that string goes by the file's size. A file of another size still
loads, read from its start: a larger one, such as a 128 KB file from libretro
gpSP, loads its first part. The next time the game saves, the file is written
back at the chip's size.

## How a save is written

Every write goes first to `<rom>.sav.tmp`. The emulator syncs it to the Memory
Stick and reads it back. Only when it matches does the old `.sav` become
`<rom>.sav.bak`, replacing the previous `.bak`, and the `.tmp` take the name
`<rom>.sav`. So the `.bak` always holds the save from before the last write.

A write needs free space for one more copy of the save. When a write fails,
for example on a full Memory Stick, the `.sav` stays as it was, and the game
keeps the new save in memory and tries again. The menu opens once to tell
you, and when you quit, suspend or load another game from the menu it asks
whether to try again. Free some space and the next try writes it.

## After a power cut

When a game starts, and before every write, the emulator finishes a write
that a power cut stopped:

- With the `.sav` there, a `.tmp` beside it is deleted: it never became the
  save.
- With no `.sav` but a `.bak`, the `.tmp` becomes the `.sav` if there is one,
  since the old save only becomes the `.bak` once the `.tmp` has been checked.
  Without a `.tmp`, the `.bak` becomes the `.sav` again.
- With neither, a `.orig` beside a `.tmp` becomes the `.sav` again, and a
  `.tmp` alone is deleted.

At most the save being written when the power went is lost.

## `<rom>.sav.orig`

The first time a write replaces a `.sav` of another size, the old file is kept
as `<rom>.sav.orig` instead of `.bak`, and the old `.bak` is deleted. That
happens when a file of the wrong size is written back at the chip's size, and
when the game itself switches its chip to the larger size. The `.orig` is
kept once: no write replaces or deletes it, so if the size changes again it
still holds the first file. Delete it yourself once you no longer need it.

## Pre-link copies

Before a link session starts, the save is written and copied to
`<rom>.sav.link1`. The copy that was `.link1` becomes `.link2`, `.link2`
becomes `.link3`, and the old `.link3` is deleted. Each copy is written and
checked the same way as the `.sav`. If the save cannot be written or copied,
the session does not start and the Link cable menu says why. A
`<rom>.sav.link1.tmp` is a copy a power cut stopped; the next link session
deletes it.

To put a copy back on the PSP, open Link cable > Restore pre-link save with
the game running and no session open. Each copy is offered in turn, newest
first, with the date and time it was made: confirm to restore it, or decline
to see the next one. The current save is kept as `<rom>.sav.bak` and the game
restarts on the restored save. To undo the restore, put the `.bak` back by
hand (see below) before the game saves again, since its next write replaces
the `.bak`.

## Savestates

Savestates are `<rom>_0.svs` to `<rom>_9.svs`, plus the automatic one:
`<rom>_auto.svs` with `gba_bios.bin`, `<rom>_auto-builtin.svs` with the
built-in BIOS. They are written the same way as the `.sav`, so a `.svs.tmp`,
`.svs.bak` or `.svs.orig` can appear beside them: `<rom>_0.svs.bak` is the
state slot 0 held before the last save to it. A `.svs.orig` appears only when
a state file had the wrong size, such as one cut short by an earlier version.
Deleting a state with Triangle in the Savestate menu also deletes its `.bak`,
`.tmp` and `.orig`.

## A save that cannot be read

When a `.sav` exists but cannot be read, the emulator asks "The save file
cannot be read. Try again?". Yes reads it again. No asks "Play without
saving? The file stays as it is.": yes starts the game and writes nothing to
the `.sav` until another game is loaded, and no goes back to the game list.
While you play without saving, no pre-link copy is made or restored. The file
itself is never written over.

## Restoring a file by hand

1. Quit GBA Rush PSP. While a game runs, the emulator keeps its save in
   memory, and its next write would replace the file you put back.
2. Connect the PSP to the PC by USB, or put the Memory Stick in a card reader,
   and open the saves folder. If the PC hides file name extensions, turn them
   on to see `.bak` and the others.
3. Move the current `<rom>.sav` somewhere else on the PC, to keep it.
4. Rename the file you want back, `<rom>.sav.bak`, `<rom>.sav.orig` or
   `<rom>.sav.link1` to `.link3`, to `<rom>.sav`.
5. Start the game.

A savestate goes back the same way: rename `<rom>_0.svs.bak` to
`<rom>_0.svs` in the savestates folder.
