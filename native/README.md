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

The build image is based on **`vitalets/docker-tizen-webos-sdk`** — the same
headless Tizen CLI image the Jellyfin community installer uses. It ships the Tizen
Studio CLI (installed non-interactively via the web-cli installer), `sdb`, and a
sample dev author cert + `profiles.xml`, so there is no GUI installer and no
Tizen-Studio install step of our own.

```bash
# 1. one-time: fetch toolchain + Tizen 5.0 sources, copy the TV's libraries
native/fetch-tizen-sources.sh ./tizen-deps
ssh root@<TV> 'cd / && tar cf - lib usr/lib' | tar xf - -C tizen-deps/tv-libs

# 2. build the image (vitalets base + a few build tools)
docker build -t moonlight-tizen-build native/pkg

# 3. build the engine .so + the web .wgt inside the container
docker run --rm -v "$PWD":/work/repo -v <moonlight-tizen>:/work/mlt \
  moonlight-tizen-build bash -lc '
    REPO=/work/repo MLT=/work/mlt DEPS=/work/repo/tizen-deps \
      /work/repo/native/pkg/build-package.sh'
```

Then install on the TV exactly like Jellyfin — `sdb` + `tizen install`, in
Developer Mode:

```bash
docker run --rm --network host -v "$PWD":/work/repo moonlight-tizen-build \
  /work/repo/native/pkg/install-on-tv.sh <TV_IP> <package.wgt|.tpk> [cert_password]
```

> **Packaging status.** The web `.wgt` builds, signs and installs headless on the
> vitalets image (the Tizen-Studio-install problem is gone). The native engine ships
> as `libmlengine.so` driven by a thin exe (root path) or a **.NET host `.tpk`**
> (no-root dev-cert path — see `pkg/DEV-CERT-PATH.md`); the `.tpk` is built with the
> .NET + Tizen workload and installed with the same `install-on-tv.sh`. Recent TVs
> reject the default Tizen cert ("Invalid certificate chain"); pass a Samsung
> certificate bound to the TV's DUID via the `cert_password` argument (mount
> `author.p12`/`distributor.p12`), as the Jellyfin installer does.

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
