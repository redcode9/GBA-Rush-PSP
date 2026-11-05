# The images .github/workflows/check.yml runs in (CONTRIBUTING.md, Build).
# Base images are pinned by digest and CBMC by version and SHA-256: to update
# one, read the new digest or checksum and change it here, never use a tag.

# The PSP toolchain (pspdev, on Alpine) plus what the host tests, the source
# checks and packaging need. The GNU tools stand in for BusyBox's, so the
# scripts run with the same date, touch, diff and find as on a Linux desktop.
# The workspace is mounted from the runner, owned by another user than the
# container's root, so Git is told to trust it.
FROM pspdev/pspdev@sha256:ee385df3aa783189d5a033af5eb1d4a9cdd93718fb8bf652ec7bb48e54038a8b AS psp
RUN apk add --no-cache gcc musl-dev zlib-dev python3 py3-yaml ripgrep zip \
  unzip coreutils diffutils findutils \
 && git config --system --add safe.directory '*'

# CBMC for make check-cbmc. Its releases ship only glibc packages, which do
# not run on Alpine, so it has an Ubuntu image of its own. gcc is the
# preprocessor CBMC calls.
FROM ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852 AS cbmc
ARG CBMC_VERSION=6.11.0
ARG CBMC_SHA256=b3721aa541038384d7801ea3aeabbcddc3e8845ac8f1cbff637cf8dec7481ac8
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl gcc \
  libc6-dev make \
 && curl -fsSL -o /tmp/cbmc.deb "https://github.com/diffblue/cbmc/releases/download/cbmc-$CBMC_VERSION/ubuntu-24.04-cbmc-$CBMC_VERSION-Linux.deb" \
 && echo "$CBMC_SHA256  /tmp/cbmc.deb" | sha256sum -c - \
 && apt-get install -y --no-install-recommends /tmp/cbmc.deb \
 && rm -rf /tmp/cbmc.deb /var/lib/apt/lists/*
