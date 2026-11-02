# moonlight-tizen5.0

A **native** [Moonlight](https://moonlight-stream.org/) GameStream client for
**Samsung TVs running Tizen 5.0**, with low-latency hardware video — **no NaCl,
no WebAssembly**.

It streams games from a GameStream host (Sunshine, GeForce Experience,
[Wolf](https://games-on-whales.github.io/wolf/)) and decodes video on the TV's
hardware video plane through the vendor library `libesplusplayer.so`.

> Status: **experimental / work in progress.** The streaming engine is verified
> end-to-end on real hardware (pairing → launch → RTSP → hardware decode, 2–3
> frame player latency). The hybrid app package (UI + service) is still being
> brought up. See [BACKPORT.md](BACKPORT.md) for the step-by-step state.

## Why this exists

[moonlight-tizen](https://github.com/brightcraft/moonlight-tizen) is a
WebAssembly app that needs **Tizen 5.5+** because it uses Samsung EMSS (elementary
media stream source) for the fast, low-latency video path. **Tizen 5.0 has no
EMSS** and the public 5.0 SDK offers only the buffering `capi-media-player` path
(the same high-latency route the old NaCl client used).

This project backports the fast path to 5.0 by reusing moonlight-tizen's
reusable parts and replacing the EMSS video/audio path with a native engine built
on `libesplusplayer.so`. That library is **internal / not in the public SDK**, so
its ABI was reverse-engineered from the device binary — see
[PROVENANCE.md](PROVENANCE.md).

## What is reused vs. new

| | |
|---|---|
| **Reused unchanged** | `moonlight-common-c` (streaming core), `libgamestream` (pairing/HTTP), the web UI (host list, pairing, app grid, settings) |
| **Replaced** | EMSS video → `video_esplus.c`, EMSS/WebAudio → `audio_opus.c`, WASM embind transport → a message-port bridge, cert handling → native |
| **New** | the reverse-engineered `esplusplayer_tv50.h`, the message-port service + JS bridge, `esplus_player.c`, `input_evdev.c`, `gs_client.c` (+ own `xml_search`), the whole build/packaging |

Full map and attribution in [PROVENANCE.md](PROVENANCE.md); design in
[pkg/ARCHITECTURE.md](pkg/ARCHITECTURE.md).

## Architecture (hybrid package)

```
┌─────────────────────────── Tizen hybrid package ───────────────────────────┐
│  Web UI (wgt)  — moonlight-tizen front-end, unchanged logic                 │
│      │  tizen.messageport  (mlbridge.js replaces the WASM embind transport)  │
│      ▼                                                                       │
│  Native service (tpk)  — mlservice.c                                         │
│      makeCert / httpInit / openUrl / pair / startStream / sendKeyboardEvent  │
│      → moonlight-common-c + libgamestream + esplusplayer video/audio + input │
└─────────────────────────────────────────────────────────────────────────────┘
```

During streaming the native service owns the hardware video overlay; the web UI
is the menu/launcher.

## Build (headless, Docker)

Everything builds in a container — no GUI, no host Tizen install needed. Heavy or
device-specific inputs are **mounted**, never committed:

- `tizen-deps/` — cross toolchain + Tizen 5.0 sysroot/sources (`fetch-tizen-sources.sh`)
- `tv-libs/` — `.so` libraries copied from **your** TV (never redistribute these)

```bash
# 1. one-time: fetch toolchain + Tizen 5.0 sources, copy the TV's libraries
native/fetch-tizen-sources.sh ./tizen-deps
ssh root@<TV> 'cd / && tar cf - lib usr/lib' | tar xf - -C tizen-deps/tv-libs

# 2. build the lean build image (build tools + a JRE; no GUI)
podman build -t moonlight-tizen-build native/pkg

# 3. build + package inside the container, mounting the Tizen Studio CLI dir
#    (the installer is GUI-only, but the CLI itself runs headless, so it is
#     mounted rather than installed into the image)
podman run --rm \
  -v "$PWD":/work/repo -v <moonlight-tizen>:/work/mlt \
  -v <tizen-studio>:/work/tizen-cli \
  moonlight-tizen-build \
  bash -lc 'REPO=/work/repo MLT=/work/mlt DEPS=/work/repo/tizen-deps \
            TIZEN_CLI=/work/tizen-cli/tools/ide/bin /work/repo/native/pkg/build-package.sh'
```

`build-package.sh` runs fully headless: build engine → assemble the web UI with
the message-port transport → sign/package. The signing certificate is generated
inside the container (self-signed author + the bundled Tizen dev distributor), so
no GUI certificate wizard is needed.

> **Packaging status.** The image installs Tizen Studio fully headless (no GUI):
> `native/pkg/install-tizen-cli.sh` bypasses the GUI installer by extracting its
> self-extracting payload, then adds `NativeCLI` via the bundled package manager.
> The CLI runs, generates a dev cert, and **signs the web `.wgt`** headless.
> The native `.tpk` step still needs a TV **native rootstrap + templates** (the
> Samsung TV SDK extension) that `NativeCLI` alone does not provide — until that is
> added, build the `.tpk` by hand-assembling the zip and signing it. See
> `native/pkg/install-tizen-cli.sh` and `pkg/ARCHITECTURE.md`.

## Install & run (no root required)

The hardware video path works from a sandboxed, dev-signed app — **root is not
required** for a signed `.tpk`/`.wgt` installed in developer mode (verified: the
esplusplayer path runs under a `User::Pkg` SMACK label). Root is only needed for
the raw-binary CLI test harness (`esppplay`, `mlclient`).

```bash
sdb connect <TV-IP>
tizen install -n <package>.tpk
```

## Requirements

- Samsung TV, **Tizen 5.0**, Muse.M, armv7l.
- A GameStream host on the LAN (Sunshine / GeForce Experience / Wolf).
- To build: Linux with `podman`, and the TV's `/lib` + `/usr/lib` (copied once).

## Repository layout

```
esplusplayer_tv50.h   reverse-engineered ABI of the TV's libesplusplayer.so
mlclient/             native engine + CLI harness (video, audio, input, pairing)
pkg/                  hybrid package: Dockerfile, service (tpk), ui (wgt), build
fetch-*.sh build-*.sh reproducible build scripts
esppplay.c            minimal H.264→esplusplayer probe used during bring-up
BACKPORT.md           milestone-by-milestone status
PROVENANCE.md         code origins + reverse-engineering results
```

## Credits & license

- [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) and
  `libgamestream` — the Moonlight project (GPLv3). This project is therefore GPLv3.
- [moonlight-tizen](https://github.com/brightcraft/moonlight-tizen) — the web UI and
  the Tizen integration this port builds on.
- Samsung's TV libraries (`libesplusplayer.so` etc.) are **not** included and must
  not be redistributed; they are used only locally, linked against copies from your
  own device.

## Disclaimer

Independent, unofficial project. Reverse-engineering notes are engineering aids;
items marked "assumed" need verification on the target hardware before relying on
them. Not affiliated with or endorsed by Samsung, NVIDIA, or the Moonlight project.
