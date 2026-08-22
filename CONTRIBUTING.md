# Contributing

Changes should preserve compatibility and performance on real PSP hardware.
Prefer fixes that apply to the GBA or PSP platform generally; keep game-specific
workarounds documented and narrowly scoped.

Before submitting a change, run:

```sh
make check
make test-sanitize
make psp-build
```

Changes to hot paths should include a focused host harness where practical and
a three-run PSP benchmark comparison when performance may move. Test suspend,
resume and menu transitions for changes involving threads, audio, files,
networking or timing.

Do not commit ROMs, BIOS files, saves, savestates, cheat databases, generated
executables or local configuration. Synthetic test ROM sources and test vectors
must be original and redistributable.

Keep the public documentation in the existing four Markdown files: the root
README, changelog, this guide and the launcher README. Update an existing file
instead of adding design diaries, implementation logs or one-document-per-topic
notes. Legal notices may remain plain text beside the component they cover.

Commit subjects should be short, imperative and describe the observable
change. Preserve copyright, license and attribution notices when moving or
modifying inherited code.
