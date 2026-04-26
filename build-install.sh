#!/usr/bin/env bash
set -euo pipefail

CLEAN_BUILD=0
TV_IP_ARG=""

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--clean] [TV_IP]"
            echo "  --clean   Run 'make clean' inside the build container before building"
            echo "  TV_IP     Target TV IP address (or set MOONLIGHT_TV_IP env var)"
            exit 0
            ;;
        *)
            if [ -z "$TV_IP_ARG" ]; then
                TV_IP_ARG="$1"
            else
                echo "[ERROR] Unexpected argument: $1" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

TV_IP="${TV_IP_ARG:-${MOONLIGHT_TV_IP:-}}"

if [ -z "$TV_IP" ]; then
    echo "[ERROR] TV IP not specified. Pass it as an argument or set MOONLIGHT_TV_IP." >&2
    echo "Usage: $0 [--clean] TV_IP" >&2
    exit 1
fi
SDK_IMAGE="moonlight-tizen-nacl-sdk:5.6"
APP_ID="ChqHQExU28.MoonlightNaCl"
APP_WGT="MoonlightNaCl.wgt"

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if ! git diff --quiet 2>/dev/null || ! git diff --cached --quiet 2>/dev/null; then
    GIT_HASH="${GIT_HASH}-dirty"
fi
BUILD_VERSION="${GIT_HASH}-$(date -u +%Y%m%dT%H%M%SZ)"

echo "[INFO] Build version: $BUILD_VERSION"
echo "[INFO] Clean build:   $CLEAN_BUILD"
echo "[INFO] Target TV:     $TV_IP"

echo "[INFO] Checking tailscaled status..."

TAILSCALED_RUNNING=0

if command -v systemctl >/dev/null 2>&1; then
    if systemctl is-active --quiet tailscaled 2>/dev/null; then
        TAILSCALED_RUNNING=1
    fi
fi

if pgrep -x tailscaled >/dev/null 2>&1; then
    TAILSCALED_RUNNING=1
fi

if [ "$TAILSCALED_RUNNING" -eq 1 ]; then
    echo "[INFO] tailscaled is running. Stopping it..."

    if command -v systemctl >/dev/null 2>&1; then
        sudo systemctl stop tailscaled 2>/dev/null || true
    fi

    if command -v service >/dev/null 2>&1; then
        sudo service tailscaled stop 2>/dev/null || true
    fi

    sudo pkill -x tailscaled 2>/dev/null || true
else
    echo "[INFO] tailscaled is not running."
fi

echo "[INFO] Building with mounted repo, using make cache..."

docker run --rm -i \
  --network host \
  --user root \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -e CLEAN_BUILD="$CLEAN_BUILD" \
  -e BUILD_VERSION="$BUILD_VERSION" \
  -v "$PWD:/work" \
  "$SDK_IMAGE" \
  bash -lc "
set -euo pipefail

export HOME=/home/moonlight
export NACL_SDK_ROOT=/home/moonlight/pepper_63
export PATH=/home/moonlight/tizen-studio/tools/ide/bin:/home/moonlight/tizen-studio/tools:\$PATH

cd /work

if [ \"\$CLEAN_BUILD\" = \"1\" ]; then
  echo '[Docker] make clean'
  make clean
fi
make

rm -rf /tmp/moonlight-build
mkdir -p /tmp/moonlight-build/static /tmp/moonlight-build/pnacl/Release

/home/moonlight/pepper_63/toolchain/linux_pnacl/bin/pnacl-translate \
  -arch arm \
  pnacl/Release/moonlight-chrome.pexe \
  -o /tmp/moonlight-build/pnacl/Release/moonlight-chrome-arm.nexe

cp -r index.html config.xml icons/icon128.png /tmp/moonlight-build/
cp -r static/* /tmp/moonlight-build/static/
mv /tmp/moonlight-build/icon128.png /tmp/moonlight-build/icon.png
sed -i \"s|__BUILD_VERSION__|\$BUILD_VERSION|g\" /tmp/moonlight-build/index.html
printf '%s\n' \
  '{' \
  '  \"program\": {' \
  '    \"arm\": {' \
  '      \"url\": \"moonlight-chrome-arm.nexe\"' \
  '    }' \
  '  }' \
  '}' > /tmp/moonlight-build/pnacl/Release/moonlight-chrome.nmf

cd /tmp

printf '%s\n' \
  'set timeout -1' \
  'spawn tizen package -t wgt -- moonlight-build' \
  'expect \"Author password:\"' \
  'send -- \"1234\r\"' \
  'expect \"Yes: (Y), No: (N) ?\"' \
  'send -- \"N\r\"' \
  'expect eof' \
| expect

cp /tmp/moonlight-build/MoonlightNaCl.wgt /work/MoonlightNaCl.wgt
chown -R \$HOST_UID:\$HOST_GID /work/pnacl /work/MoonlightNaCl.wgt
"

echo "[INFO] Installing $APP_WGT on TV: $TV_IP"

docker run --rm -i \
  --network host \
  -v "$PWD:/work" \
  "$SDK_IMAGE" \
  bash -lc "set -euo pipefail; \
            export PATH=/home/moonlight/tizen-studio/tools/ide/bin:/home/moonlight/tizen-studio/tools:\$PATH; \
            rm -rf /tmp/moonlight-install; \
            mkdir -p /tmp/moonlight-install; \
            cp /work/$APP_WGT /tmp/moonlight-install/$APP_WGT; \
            sdb kill-server || true; \
            sdb start-server || true; \
            sdb connect $TV_IP; \
            sdb devices; \
            sdb uninstall $APP_ID || true; \
            tizen install -n /tmp/moonlight-install/$APP_WGT"

echo "[INFO] Done."
