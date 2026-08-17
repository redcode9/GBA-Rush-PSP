# Building with PSPDev in Docker

The emulator can be built with the official `pspdev/pspdev` container instead
of a locally installed Allegrex toolchain.

## Build the emulator

From the repository root on macOS or Linux:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace/source \
  pspdev/pspdev:latest \
  make
```

From PowerShell:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -w /workspace/source `
  pspdev/pspdev:latest `
  make
```

The main outputs are `source/EBOOT.PBP` and `source/exception.prx`.

## Run host checks

The CodeBreaker tests use the host compiler rather than the PSP container:

```sh
make check
```

## Build the single-game launcher template

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace/source/single-game_launcher \
  pspdev/pspdev:latest \
  make

cp source/single-game_launcher/EBOOT.PBP \
  source/single-game_launcher/tool/assets/EBOOT.PBP
```

The generated template is intentionally ignored by Git.

## Clean

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace/source \
  pspdev/pspdev:latest \
  make clean
```

If Docker Desktop rejects the bind mount, move the checkout outside a synced
folder or allow the parent directory in Docker Desktop's file-sharing settings.
