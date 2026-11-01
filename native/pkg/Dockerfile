# Headless build image for the hybrid Moonlight Tizen 5.0 package. No GUI.
#
# Lean on purpose: it carries the build tools + a JRE, but NOT a Tizen Studio
# install. The Tizen Studio *installer* is a GUI app and cannot run headless, but
# the Tizen *CLI* is just files and runs headless - so a Tizen Studio directory is
# MOUNTED at run time instead of installed into the image. Everything else that is
# heavy or device-specific is mounted too (never committed):
#   /work/repo        this checkout
#   /work/mlt         a moonlight-tizen checkout (web UI + moonlight-common-c)
#   /work/tizen-deps  cross toolchain + Tizen 5.0 sysroot/sources (fetch-tizen-sources.sh)
#   /work/tv-libs     .so libraries copied from the TV
#   /work/tizen-cli   a Tizen Studio dir (its tools/ide/bin/tizen runs headless)
#
# Build:  podman build -t moonlight-tizen-build native/pkg
# Run:    native/pkg/build-package.sh (wires the mounts; see its header)
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 GIT_TERMINAL_PROMPT=0
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl wget git make cmake pkg-config file xz-utils bzip2 \
      unzip zip rsync python3 libarchive-tools \
      openjdk-17-jre-headless \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
