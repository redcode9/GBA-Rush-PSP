# Benchmark archive

Raw performance reports of published GBA Rush PSP releases, one directory
per console model, game and release:

```text
benchmarks/<model>/<game-code>-r<revision>/<version>/<run>.txt
```

- `<model>` is `psp-1000`, `psp-2000`, `psp-3000` or `psp-go`.
- `<game-code>-r<revision>` is the report's `game-code` and `rom-revision`,
  for example `BPRE-r0`.
- `<version>` is the report's `version`, for example `1.4.3`.
- `<run>.txt` is a report saved from Diagnostics > Start measurement,
  unchanged. Every run in one directory must share the settings
  `scripts/compare-benchmarks.py` checks (`MATCH_FIELDS`).

Rules:

- Only `.txt` reports and `.md` notes; `scripts/check-tree.sh` fails on any
  other file here. Never add ROMs, saves, save states or BIOS files.
- Only reports from the `EBOOT.PBP` of a published release package. Write
  the sha256 of that `EBOOT.PBP` in the commit that adds the reports.
- Keep only the baselines of each release; development builds are compared
  with `scripts/compare-benchmarks.py` and not archived.

After adding or removing reports, regenerate the table in
`docs/PERFORMANCE.md`:

```sh
python3 scripts/perf-table.py --update docs/PERFORMANCE.md
```
