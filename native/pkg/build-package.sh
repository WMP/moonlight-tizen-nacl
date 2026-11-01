#!/bin/sh
# Build the hybrid Moonlight Tizen 5.0 package: native engine service (tpk) +
# moonlight-tizen web UI (wgt) with the transport swapped to the message port.
# Run inside the Docker image (native/pkg/Dockerfile) for a reproducible toolchain,
# or on a host that has the same tools. Heavy/secret inputs come via env:
#
#   REPO       repo root (this checkout)
#   MLT        a moonlight-tizen checkout (web UI + moonlight-common-c)
#   DEPS       tizen-deps dir (fetch-tizen-sources.sh output; path without spaces)
#   BPD        backport-deps dir (fetch-backport-deps.sh output)
#   DEPSBUILD  build-deps-lib.sh output (core/gs objs + libmbedcrypto_tv.a)
#   TIZEN_CLI      Tizen Studio CLI dir (host ~/tizen-tmp/tools)      [phase 4]
#   TIZEN_PROFILE  signing profiles.xml path                          [phase 4]
#   SIGN_PROFILE   profile name in profiles.xml (default: Moonlight)  [phase 4]
set -e
: "${REPO:?}"; : "${MLT:?}"; : "${DEPS:?}"; : "${BPD:?}"; : "${DEPSBUILD:?}"
HERE="$REPO/native/pkg"
OUT=${OUT:-$HERE/out}; rm -rf "$OUT"; mkdir -p "$OUT/service/bin" "$OUT/ui"

echo "== phase 1: native engine service =="
OUT="$OUT/_sb" MLT="$MLT" DEPS="$DEPS" BPD="$BPD" DEPSBUILD="$DEPSBUILD" "$HERE/service/build-service.sh"
cp "$OUT/_sb/moonlight-engine" "$OUT/service/bin/moonlight-engine"
cp "$HERE/service/tizen-manifest.xml" "$OUT/service/tizen-manifest.xml"

echo "== phase 2: web UI (moonlight-tizen front-end + message-port transport) =="
# Ship the web front-end, NOT the WASM module.
cp "$MLT/wasm/index.html" "$OUT/ui/index.html"
cp -r "$MLT/wasm/platform" "$OUT/ui/platform"
cp -r "$MLT/wasm/static" "$OUT/ui/static" 2>/dev/null || true
[ -d "$MLT/wasm/res" ] && cp -r "$MLT/wasm/res" "$OUT/ui/res" || true
cp "$HERE/ui/mlbridge.js" "$OUT/ui/platform/mlbridge.js"
cp "$HERE/ui/config.xml" "$OUT/ui/config.xml"
cp "$HERE/ui/icon.png" "$OUT/ui/icon.png" 2>/dev/null || true
# Drop the Emscripten module loader and inject the message-port shim after messages.js.
sed -i '/src="platform\.js"/d' "$OUT/ui/index.html"
sed -i 's#<script type="text/javascript" src="platform/messages.js"></script>#&\n  <script type="text/javascript" src="platform/mlbridge.js"></script>#' "$OUT/ui/index.html"
echo "ui assembled -> $OUT/ui"

echo "== phase 3: tpk service project ready, wgt ui project ready =="
echo "service: $OUT/service   ui: $OUT/ui"

echo "== phase 4: package + sign (needs Tizen CLI) =="
if [ -n "$TIZEN_CLI" ] && [ -x "$TIZEN_CLI/tizen" ]; then
  P=${SIGN_PROFILE:-Moonlight}
  [ -n "$TIZEN_PROFILE" ] && "$TIZEN_CLI/tizen" cli-config "default.profiles.path=$TIZEN_PROFILE/profile/profiles.xml" >/dev/null 2>&1 || true
  # web wgt (UI)
  "$TIZEN_CLI/tizen" build-web -- "$OUT/ui" || true
  "$TIZEN_CLI/tizen" package -t wgt -s "$P" -- "$OUT/ui"
  # native tpk (engine)
  "$TIZEN_CLI/tizen" package -t tpk -s "$P" -- "$OUT/service"
  # hybrid: combine the signed wgt into the tpk package (-r = HybridApp reference)
  "$TIZEN_CLI/tizen" package -t tpk -r "$(ls "$OUT"/ui/*.wgt 2>/dev/null | head -1)" -s "$P" -- "$OUT/service"
  echo "package(s):"; ls "$OUT/service"/*.tpk "$OUT/ui"/*.wgt 2>/dev/null
else
  echo "TIZEN_CLI not set/executable - skipping package+sign."
  echo "Install:  TIZEN_CLI=~/tizen-tmp/tools TIZEN_PROFILE=~/tizen-tmp-data \\"
  echo "          \$TIZEN_CLI/sdb install \$OUT/service/org.moonlight.tizen5-*.tpk"
fi
