# Contributing

Contributions should preserve performance on real PSP hardware and avoid
game-specific shortcuts in the emulator core.

Before submitting a change:

```sh
make check
make psp-build
```

CodeBreaker changes should include a synthetic regression test in
`tests/test_codebreaker.c`. Do not commit ROMs, BIOS files, save data, cheat
databases, generated PSP binaries, or local configuration.

Keep commit subjects short, imperative, and focused on the observable change.
Retain existing copyright and license notices when moving or modifying code.
