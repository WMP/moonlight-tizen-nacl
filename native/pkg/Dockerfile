# Build image for the hybrid Moonlight Tizen 5.0 package (native service + web UI).
# Lean: only host build tools + a JRE for the Tizen CLI. The heavy/secret inputs
# are MOUNTED at run time, not baked in:
#   /work/repo       - this repo (native/ scripts + sources)
#   /work/tizen-deps - fetch-tizen-sources.sh output (GCC 6.2 toolchain + Tizen 5.0 sources)
#   /work/tv-libs    - .so libraries copied from the TV (never committed)
#   /work/tizen-cli  - Tizen Studio CLI (host ~/tizen-tmp/tools) for package+sign
#   /work/tizen-profile - signing profile dir (host ~/tizen-tmp-data, has Moonlight.p12)
#
# Build:  podman build -t moonlight-tizen-build native/pkg
# Run:    see native/pkg/build-package.sh (it wires the mounts and runs the build).
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 GIT_TERMINAL_PROMPT=0
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl wget git make cmake pkg-config file xz-utils bzip2 \
      unzip zip rsync python3 \
      openjdk-17-jre-headless \
      libarchive-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
