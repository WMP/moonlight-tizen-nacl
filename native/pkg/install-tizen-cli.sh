#!/bin/sh
# Install Tizen Studio CLI fully headless (no GUI, no clicking) by bypassing the
# GUI InstallManager: the Baseline .bin is a self-extracting bash archive whose
# payload already contains the Tizen Studio tree (tools/ide/bin/tizen) + a bundled
# JDK + the headless package-manager. We extract that payload and drop in the
# sdk.info the GUI installer would have written. Then the package-manager-cli adds
# any extra component (e.g. native-app packaging) — also headless.
#
# Verified: the extracted CLI runs (Tizen CLI 2.5.25), generates a dev cert, and
# packages+signs web .wgt. NativeCLI installs too (the engine jars appear).
#
# KNOWN GAP: `tizen package -t tpk` still reports "Cannot provide a package
# function" / "no rootstrap" / "Not Found Native templates" - the CLI's native
# packaging wants a full TV native platform SDK (rootstrap + templates), i.e. the
# Samsung TV SDK extension, not just NativeCLI. Until that is added, build the
# native .tpk another way (e.g. hand-assemble the zip + sign). The web .wgt path
# is unaffected.
#
# Usage: install-tizen-cli.sh <install-dir> [data-dir]
#   must run as a non-root user (the installer refuses root; we never run it, but
#   the package-manager also prefers non-root).
set -e
DEST=${1:?usage: install-tizen-cli.sh <install-dir> [data-dir]}
DATA=${2:-$DEST-data}
TS_URL=${TS_URL:-https://download.tizen.org/sdk/Installer/tizen-studio_5.0/Baseline_Tizen_Studio_5.0_ubuntu-64.bin}
NATIVE_PKG=${NATIVE_PKG:-TIZEN-10.0-NativeAppDevelopment-CLI}
WORK=$(mktemp -d)

echo "[tizen] downloading baseline installer"
curl -fsSL -o "$WORK/ts.bin" "$TS_URL"

echo "[tizen] extracting SFX payload (no GUI installer run)"
# The bash header stores its own length in ORI_FILE_LEN; the payload is a tar.gz
# appended after it.
LEN=$(grep -a -m1 '^ORI_FILE_LEN=' "$WORK/ts.bin" | sed 's/[^0-9]//g')
tail -n +"$LEN" "$WORK/ts.bin" | tar xmz -C "$WORK"

echo "[tizen] unzipping Tizen Studio base into $DEST"
mkdir -p "$DEST" "$DATA"
"$WORK/unzip" -qq "$WORK/tizen-sdk.zip" -d "$DEST"

echo "[tizen] setting up bundled JDK + sdk.info"
mkdir -p "$HOME/.package-manager/jdk"
[ -e "$HOME/.package-manager/jdk/bin/java" ] || cp -a "$DEST/jdk/." "$HOME/.package-manager/jdk/"
cat > "$DEST/sdk.info" <<EOF
TIZEN_SDK_INSTALLED_PATH=$DEST
TIZEN_SDK_DATA_PATH=$DATA
EOF

echo "[tizen] base CLI: $("$DEST/tools/ide/bin/tizen" version 2>/dev/null | head -1)"

# Native-app packaging component (for .tpk). The exact package name can vary by
# repo snapshot; override via NATIVE_PKG. Web .wgt already works without it.
if [ -n "$NATIVE_PKG" ]; then
  echo "[tizen] installing native component: $NATIVE_PKG"
  "$DEST/package-manager/package-manager-cli.bin" install --accept-license --no-java-check "$NATIVE_PKG" \
    2>&1 | grep -ivE 'at (java|sun)|ClassNotFound|Caused by|\.\.\. ' | tail -5 || true
fi

rm -rf "$WORK"
echo "[tizen] done -> $DEST (PATH += $DEST/tools/ide/bin)"
