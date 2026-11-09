# The images .github/workflows/check.yml runs in (CONTRIBUTING.md, Build).
# The base image is pinned by digest, its apt packages by a snapshot of the
# Ubuntu archive, CBMC by version and SHA-256 and pspdev by the release and
# SHA-256 in scripts/setup-dev.sh: to update one, change the snapshot date
# or read the new digest or checksum and change it there, never use a tag.
# The psp image reads setup-dev.sh from a second build context:
#   docker build --build-context scripts=scripts --target psp tools/ci

# Ubuntu 24.04, the base of both images.
ARG UBUNTU=ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

# Both images install their packages from the Ubuntu archive as it was at
# this moment (UTC), served by snapshot.ubuntu.com. That is HTTPS, so
# ca-certificates (and the openssl it needs) comes from the live archive;
# then apt is set to the snapshot and the live package lists are dropped,
# so every later apt call, CBMC's dependencies included, reads the snapshot.
ARG UBUNTU_SNAPSHOT=20261109T000000Z

FROM $UBUNTU AS base
ARG UBUNTU_SNAPSHOT
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && echo "APT::Snapshot \"$UBUNTU_SNAPSHOT\";" > /etc/apt/apt.conf.d/50snapshot \
 && rm -rf /var/lib/apt/lists/*

# The PSP toolchain that setup-dev.sh installs for developers plus what the
# host tests, the source checks and packaging need. Its GCC converts the
# message tables to GBK with the C library's iconv, which glibc can do and
# musl cannot. cc is GCC 14, which rejects implicit declarations even under
# -w. The workspace is mounted from the runner, owned by another user than
# the container's root, so Git is told to trust it.
FROM base AS psp
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl gcc-14 \
  libc6-dev make zlib1g-dev python3 python3-yaml git zip unzip \
 && rm -rf /var/lib/apt/lists/* \
 && ln -sf gcc-14 /usr/bin/cc \
 && git config --system --add safe.directory '*'
ENV PSPDEV=/opt/gba-rush/ext/pspdev
ENV PATH=$PSPDEV/bin:$PATH
WORKDIR /opt/gba-rush
COPY --from=scripts setup-dev.sh scripts/
RUN sh scripts/setup-dev.sh pspdev && rm -rf ext/downloads

# CBMC for make check-cbmc, from its Ubuntu 24.04 package. gcc is the
# preprocessor CBMC calls.
FROM base AS cbmc
ARG CBMC_VERSION=6.11.0
ARG CBMC_SHA256=b3721aa541038384d7801ea3aeabbcddc3e8845ac8f1cbff637cf8dec7481ac8
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl gcc \
  libc6-dev make \
 && curl -fsSL -o /tmp/cbmc.deb "https://github.com/diffblue/cbmc/releases/download/cbmc-$CBMC_VERSION/ubuntu-24.04-cbmc-$CBMC_VERSION-Linux.deb" \
 && echo "$CBMC_SHA256  /tmp/cbmc.deb" | sha256sum -c - \
 && apt-get install -y --no-install-recommends /tmp/cbmc.deb \
 && rm -rf /tmp/cbmc.deb /var/lib/apt/lists/*
