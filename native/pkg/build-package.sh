#!/bin/sh
# Build the hybrid Moonlight Tizen 5.0 package in the Docker image
# (native/pkg/Dockerfile). Heavy/secret inputs are mounted, not baked in.
#
# Usage (from repo root):
#   podman build -t moonlight-tizen-build native/pkg
#   REPO=$PWD MLT=/path/to/moonlight-tizen \
#   TVLIBS=/path/to/tv-libs TIZEN_CLI=~/tizen-tmp/tools TIZEN_PROFILE=~/tizen-tmp-data \
#   native/pkg/build-package.sh
#
# Phases:
#   1. native engine  - VERIFIED: compiles in-container with the mounted GCC 6.2
#                        toolchain (tizen-deps) + backport deps + tv-libs.
#   2. bridge         - TODO: message-port server in the engine (libcapi-message-port)
#                        + the service_app lifecycle (libcapi-appfw-application).
#   3. web UI         - TODO: copy moonlight-tizen wasm/platform + index.html + res,
#                        swap the sendMessage/handleMessage transport to a message-port
#                        client (tizen.messageport).
#   4. package+sign   - TODO: assemble hybrid (ui wgt + service tpk), sign with the
#                        mounted Tizen CLI + Moonlight profile.
set -e
REPO=${REPO:?set REPO to the repo root}
MLT=${MLT:?set MLT to a moonlight-tizen checkout}
IMG=${IMG:-moonlight-tizen-build}
OUT=${OUT:-$REPO/native/pkg/out}
mkdir -p "$OUT"

run() { podman run --rm -v "$REPO":/work/repo -v "$MLT":/work/mlt ${TVLIBS:+-v "$TVLIBS":/work/tv-libs:ro} "$IMG" bash -lc "$1"; }

echo "== phase 1: native engine (in container) =="
# tizen-deps (GCC 6.2 toolchain + Tizen 5.0 sysroot/sources) is expected at
# repo/tizen-deps (run native/fetch-tizen-sources.sh first, and copy tv-libs in).
run '
  set -e
  D=/work/repo/tizen-deps
  [ -d "$D/sysroot" ] || { echo "run native/fetch-tizen-sources.sh first (tizen-deps missing)"; exit 1; }
  MLT=/work/mlt/moonlight-tizen DEPS=$D OUT=/tmp/coreout /work/repo/native/build-core.sh | tail -2
'
echo "phase 1 ok (engine core builds in-container)."
echo "phases 2-4 (bridge / web UI / hybrid package+sign): see native/pkg/ARCHITECTURE.md"
