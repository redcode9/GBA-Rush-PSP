# Building with Docker Desktop (PSP)

This repo is built on the maintainer’s machine using **Docker Desktop** and the official **PSPDev** toolchain image. Use this instead of installing `psp-config` / Allegrex locally on Windows.

## Image

```text
pspdev/pspdev:latest
```

Pull once if needed: `docker pull pspdev/pspdev:latest`

## Where to build

The `Makefile` and `SDK/` layout live under **`source/`**. Run `make` with the container working directory set to that folder.

## PowerShell (from repo root)

Replace the host path if your clone lives elsewhere.

```powershell
docker run --rm `
  -v "C:/Users/Owner/OneDrive/Documents/TempGBA-2025/source:/build" `
  -w /build `
  pspdev/pspdev:latest `
  make
```

## One-liner using current directory

From `source/`:

```powershell
docker run --rm -v "${PWD}:/build" -w /build pspdev/pspdev:latest make
```

(`$PWD` should resolve to the `source` folder.)

## Output

Artifacts (e.g. `EBOOT.PBP`, `TempGBA.prx`) are written under `source/` on the host.

## Notes

- If bind mounts fail under OneDrive, try moving the repo to a non-synced path or enable the appropriate Docker Desktop file-sharing settings.
- For interactive shells: `docker run -it --rm -v "...:/build" -w /build pspdev/pspdev:latest bash`
