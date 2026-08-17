# Cheat files

TempGBA4PSP loads cheat lists manually from the **Cheats options** menu. A
successful load shows the selected filename, the number of entries, and each
entry's name and `ON`/`OFF` state.

Cheat codes are specific to a game's region and revision. Use codes intended
for the exact ROM you own. A code for a different language, revision, or ROM
hack can do nothing, corrupt game state, or crash the game.

## File structure

Each entry starts with a format and a display name. Code lines follow it, and a
blank line ends the entry.

```text
codebreaker Example 16-bit write
82000100 1234

direct_v1 Example direct write
22000104 00000001
```

The examples above are synthetic format demonstrations, not codes for a game.
Keep each address at eight hexadecimal digits. CodeBreaker values must contain
no more than four hexadecimal digits.

Supported headers are:

- `codebreaker`, `codebreaker_v1`, `codebreaker_v2`, or `CB`
- `gameshark_v1`, `gameshark_v2`, `PAR_v1`, or `PAR_v2`
- `gameshark_v3` or `PAR_v3`
- `direct_v1`, `direct_v2`, or `direct_v3`

Names are limited to 24 stored bytes. A list can contain up to 640 entries;
each entry can contain up to 128 code lines.

## CodeBreaker support

The loader supports plaintext and encrypted CodeBreaker sequences, including
the following operation families:

- game identifiers, encryption seeds, and timed hooks;
- 8-bit and 16-bit writes;
- 16-bit fill and byte-list writes;
- bitwise `OR` and `AND`;
- equality, inequality, greater-than, less-than, button, and bit-test
  conditions;
- 16-bit and signed 32-bit addition.

Encryption state follows file order. Do not reorder encrypted lines or split a
seed from the entries that follow it.

Timed master codes run from a dynamic-recompiler hook. Enable the master entry
required by the list, then enable the ordinary entries you want. Several
ordinary entries can be active together. If masters for different hook
addresses are enabled, TempGBA4PSP keeps the most recently enabled master and
turns off the conflicting master entries to prevent duplicate execution.

The CodeBreaker button condition supports the standard keypad test form. Other
device-specific extensions are rejected when the loader cannot interpret them
safely.

## Loading and troubleshooting

1. Start the correct ROM.
2. Open the emulator menu and choose **Cheats options**.
3. Choose **Load cheat file** and select the `.cht` file.
4. Confirm that the filename and entry count appear.
5. Enable the required master entry, if present, and the desired ordinary
   entries.
6. Return to the game. Some codes take effect only after changing rooms,
   entering a battle, or reloading game data.

The loader distinguishes missing, empty, malformed, and oversized files. A
failed replacement does not overwrite a previously loaded valid list. Loading
a new ROM clears the current list; select its cheat file manually.

If a code still has no effect:

- verify the ROM's region and revision;
- preserve every line of a multiline code;
- activate its required master entry;
- avoid mixing master codes from unrelated lists;
- test from an in-game state where the original code is expected to trigger.

Cheats modify emulated memory and can damage saves. Keep a backup of important
`.sav` files.
