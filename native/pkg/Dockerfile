# Headless build+install image for Moonlight Tizen 5.0, built on the SAME community
# Tizen image the Jellyfin installer uses: vitalets/docker-tizen-webos-sdk. It already
# ships the Tizen Studio CLI (installed headless via the web-cli installer), `sdb`, and
# a sample dev author cert + profiles.xml — so we no longer install Tizen Studio
# ourselves (this replaces the old install-tizen-cli.sh / slim-tizen-cli.sh).
#
# Mounted at run time (heavy/device-specific, never committed):
#   /work/repo (this repo), /work/mlt (a moonlight-tizen checkout),
#   /work/tizen-deps (fetch-tizen-sources.sh: cross toolchain + sysroot + tv-libs)
#
# Flow:
#   - engine:   build-engine-so.sh  -> libmlengine.so (mounted arm gcc-6.2 toolchain)
#   - web .wgt: build-package.sh     -> moonlight-tizen UI, signed by the image's CLI
#   - install:  install-on-tv.sh <TV_IP> <pkg.wgt|.tpk> [cert_pw]  (sdb + tizen install)
#
# Build:  docker build -t moonlight-tizen-build native/pkg
FROM docker.io/vitalets/tizen-webos-sdk

# Extra build tools the core/engine scripts use (the cross toolchain itself is mounted).
RUN apt-get update && apt-get install -y --no-install-recommends \
      make cmake pkg-config file xz-utils rsync \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
