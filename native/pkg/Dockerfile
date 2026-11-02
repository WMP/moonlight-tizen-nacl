# Headless build image for the hybrid Moonlight Tizen 5.0 package. No GUI.
#
# It carries the native cross-build tools AND a GUI-less Tizen Studio CLI with the
# native packaging component (NativeCLI). Tizen Studio's GUI *installer* cannot run
# headless, so install-tizen-cli.sh bypasses it: it extracts the installer's
# self-extracting payload and unzips the Tizen Studio tree directly, then uses the
# bundled headless package-manager to add NativeCLI. No X, no clicking.
#
# Still mounted at run time (heavy/device-specific, never committed):
#   /work/repo, /work/mlt, /work/tizen-deps (fetch-tizen-sources.sh), /work/tv-libs
#
# Build:  podman build -t moonlight-tizen-build native/pkg   (large: downloads the SDK)
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 GIT_TERMINAL_PROMPT=0 \
    TIZEN_HOME=/opt/tizen-studio
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl wget git make cmake pkg-config file xz-utils bzip2 \
      unzip zip rsync python3 libarchive-tools \
      openjdk-17-jre-headless \
      libcurl3-gnutls libglib2.0-0 libpython2.7 \
    && rm -rf /var/lib/apt/lists/*

# 1.3 GB SDK download in its own layer so it is cached across script edits.
RUN curl -fsSL -o /tmp/ts.bin \
      https://download.tizen.org/sdk/Installer/tizen-studio_5.0/Baseline_Tizen_Studio_5.0_ubuntu-64.bin

# Extra OS prerequisites the NativeCLI component checks for (KDE/KF5 libs it would
# otherwise try to apt-install itself). In a separate layer after the download so
# the 1.3 GB layer stays cached.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libkf5itemmodels5 libkf5kiowidgets5 libkchart2 \
    && rm -rf /var/lib/apt/lists/*

# The Tizen package-manager refuses to run as root, so the SDK install (and the
# build/packaging at run time) happen as a 'builder' user. For rootless-podman bind
# mounts to stay writable, run the container with:  --userns=keep-id --user builder
RUN useradd -m -u 1000 builder && mkdir -p "$TIZEN_HOME" "$TIZEN_HOME-data" \
    && chown -R builder:builder "$TIZEN_HOME" "$TIZEN_HOME-data" /tmp/ts.bin
COPY install-tizen-cli.sh /usr/local/bin/install-tizen-cli.sh
RUN chmod +x /usr/local/bin/install-tizen-cli.sh
USER builder
# Headless install (no GUI installer): extract payload + unzip + add NativeCLI.
RUN TS_URL=file:///tmp/ts.bin NATIVE_PKG=NativeCLI \
       install-tizen-cli.sh "$TIZEN_HOME" "$TIZEN_HOME-data"
ENV PATH="$TIZEN_HOME/tools/ide/bin:$TIZEN_HOME/tools:$PATH"

WORKDIR /work
CMD ["/bin/bash"]
