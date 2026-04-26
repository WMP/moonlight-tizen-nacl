# Moonlight-Tizen-NaCL

GameStream / Sunshine client for Samsung Smart TVs running Tizen OS (3.0 – 6.0).

This is a fork of [OneLiberty/moonlight-tizen-nacl](https://github.com/OneLiberty/moonlight-tizen-nacl), which is itself based on the original [moonlight-chrome](https://github.com/moonlight-stream/moonlight-chrome) NaCL port.

---

## What's new in this fork

### Xbox Wireless Controller over Bluetooth — fixed

The original project had several bugs that made BT gamepad use unreliable. All three were fixed:

| Symptom | Root cause | Status |
|---|---|---|
| 10–20 s input lag burst after stream start, then connection timeout | `LiSendMultiControllerEvent` was called at ~60 Hz regardless of whether anything changed, flooding the control stream queue | ✅ Fixed — events now only sent on actual state change |
| Controller visible in Windows joy.cpl but zero input passes through when stream was started with TV remote | Premature `LiSendControllerArrivalEvent(mask=0x0)` caused Sunshine/ViGEm to create then immediately destroy the virtual controller | ✅ Fixed — proactive arrival removed; real arrival sent on first pad connection |
| Duplicate controller arrivals causing input drop | Race between main thread and InputThread both calling `ResetGamepadState` after connection | ✅ Fixed — reset removed from `OnConnectionStarted` |
| Gamepad menu navigation broken after stream termination | `Controller.startWatching()` not restored in `streamTerminated` handler | ✅ Fixed |

### Updated moonlight-common-c streaming library

The upstream project used `moonlight-common-c` at commit `cbd0ec1` (February 2024). This fork tracks the latest upstream — currently `7b026e7` (March 2026), 61 commits ahead. Key improvements included in the update:

- **Optimized FEC decoding** — `nanors` Reed-Solomon implementation replaces the old decoder; faster packet-loss recovery on unstable Wi-Fi
- **NEON/SIMDe acceleration** — ARM SIMD path enabled for all non-x86 targets (covers the Tizen ARM build)
- **LTR ACK control messages** — better adaptive bitrate signalling with Sunshine
- **Improved local address detection** — uses UDP connect instead of interface enumeration; more reliable on complex home networks (VPNs, Docker bridges)
- **RTSP hardening** — handles malformed `Session` headers and oversized responses from some hosts
- **Higher-resolution timestamps** — `presentationTimeUs` (microseconds) instead of milliseconds; benefits frame pacing
- **ENet updates** — network library updated for improved stability
- **Gamepad input batching rewrite** — batching moved to enqueue-side; reduces latency spikes under load

### Docker-based build toolchain

A self-contained build system using Docker — no local Tizen Studio or NaCL SDK installation required. See [Building from source](#building-from-source) below.

### Debug logging infrastructure

Optional real-time log collection across TV, NaCL module, and Windows — useful for diagnosing controller or streaming issues. See [Debug logging](#debug-logging) below.

---

## Installing (end user)

No build required. Download the pre-built `.wgt` package from the [Releases](../../releases) page.

### 1. Enable Developer Mode on the TV

1. Open the **Apps** panel on your Samsung TV.
2. Using the remote, type `12345` — a hidden dialog appears.
3. Turn on **Developer mode**.
4. Enter your PC's IP address (the machine you will install from).
5. Restart the TV.

### 2. Install via sdb

You need the Samsung Smart Development Bridge (`sdb`). The easiest way to get it is via Tizen Studio, or you can run it from inside the pre-built Docker SDK image:

```bash
# Using the SDK Docker image (no local Tizen Studio needed):
docker run --rm --network host \
  ghcr.io/oneliberty/moonlight-tizen-nacl:samsung_nacl \
  bash -c "sdb connect <TV_IP> && tizen install -n MoonlightNaCl.wgt"

# Or if you have sdb installed locally:
sdb connect <TV_IP>
sdb devices                       # confirm the TV is listed
tizen install -n MoonlightNaCl.wgt
# If you have multiple devices:
tizen install -n MoonlightNaCl.wgt -t <DEVICE_ID>
```

Where `<DEVICE_ID>` is the last column shown by `sdb devices` (e.g. `UE65NU7400`).

Moonlight will appear under **Recent Apps** on your TV after installation.

### 3. (Optional) Disable Developer Mode

Return to the Apps panel → type `12345` → turn off Developer mode → restart the TV.

---

## Building from source

Requirements: **Docker** (Desktop or Engine). Nothing else needs to be installed locally.

### Step 1 — Build the SDK image (once)

```bash
docker build -t moonlight-tizen-nacl-sdk:5.6 -f Dockerfile.sdk .
```

This downloads Tizen Studio 5.6 and the Samsung NaCL SDK (~2 GB) and bakes them into a Docker image. It only needs to run once; subsequent builds use the cached image.

### Step 2 — Build and install on the TV

```bash
./build-install.sh <TV_IP>
```

Or set the TV IP as an environment variable so you don't have to type it every time:

```bash
export MOONLIGHT_TV_IP=192.168.1.50
./build-install.sh
```

Additional option:

```
--clean    Run 'make clean' before building (clears the NaCL object file cache)
```

The script:
1. Compiles the NaCL module inside Docker (source tree mounted as a bind-mount — incremental builds reuse the `pnacl/` cache)
2. Translates the `.pexe` bytecode to an ARM `.nexe` binary
3. Signs and packages the Tizen `.wgt` file
4. Installs the `.wgt` on the TV via `sdb`

Build version is automatically set to `<git-hash>-<timestamp>` and embedded in the app for identification.

---

## Debug logging

The `debug/` directory contains two tools for capturing and correlating gamepad events across the full TV → Sunshine → ViGEm pipeline. Both tools send timestamped log lines to a central HTTP server via POST.

### `debug/log-server.py` — runs on your Linux/Mac/Windows PC

```bash
python3 debug/log-server.py                        # listen on 0.0.0.0:8765, write to tv_pad_debug.log
python3 debug/log-server.py --port 9000            # custom port
python3 debug/log-server.py --log-file session.log # custom log file
```

Health check: `curl http://localhost:8765/health`

### `debug/win-gamepad-logger.py` — runs on Windows (alongside Sunshine)

Polls the XInput virtual controller (ViGEm) at 1000 Hz and reports button press/release events with precise timestamps. Used to measure end-to-end latency from TV button press to Windows ViGEm event.

```bash
python win-gamepad-logger.py --server http://192.168.1.100:8765/log
python win-gamepad-logger.py --server http://192.168.1.100:8765/log --controller 1
```

Requires Python 3 and Windows. No third-party packages needed.

### Configuring the log server URL in the Moonlight app

The app sends logs via HTTP POST. To set the target URL **without rebuilding**:

1. Open the Moonlight app on the TV.
2. Click the **`LOG: OFF`** button in the top toolbar.
3. Enter the log server URL, e.g. `http://192.168.1.100:8765/log`.
4. Click **Save**. The button changes to **`LOG: <host>`** to confirm.

The URL is saved in `localStorage` and persists across app restarts. Click **Disable** to turn logging off again.

### Log verbosity levels

The adjacent **`DBG:`** button in the toolbar cycles through log source filters:

| Level | What is logged |
|---|---|
| `EVENTS` (default) | JS UI events only (gamepad overlay, key events) — low volume |
| `NACL` | NaCL/PPAPI gamepad events only — controller connect, arrival, input events |
| `ALL` | All sources: JS events + NaCL + any other source |
| `OFF` | No remote logging (overlay still works locally) |

For gamepad debugging, use `ALL` or `NACL`. For stream/UI debugging, `EVENTS` is usually enough.

### Reading the logs

See [`GAMEPAD_LOGGING.md`](GAMEPAD_LOGGING.md) for the full field glossary, expected log sequences, and grep reference.

Quick grep recipes:

```bash
# Session flow overview
grep -E "GAMEPAD_INPUT|GAMEPAD_STREAM|WIN_CONTROLLER" tv_pad_debug.log

# Controller connect / arrival lifecycle
grep -E "ARRIVAL|CONNECTED|DISCONNECTED" tv_pad_debug.log

# Count state events (should only fire on actual input changes, not every 60 Hz tick)
grep -c "LiSendMultiControllerEvent" tv_pad_debug.log
```

---

## Credits

- [moonlight-stream/moonlight-chrome](https://github.com/moonlight-stream/moonlight-chrome) — original NaCL streaming client by the Moonlight developers
- [OneLiberty/moonlight-tizen-nacl](https://github.com/OneLiberty/moonlight-tizen-nacl) — Samsung Tizen port this fork is based on
- [pablojrl123/moonlight-tizen-docker](https://github.com/pablojrl123/moonlight-tizen-docker) — original Docker build approach
- [MrPhaze62](https://github.com/MrPhaze62) — gamepad testing and the original BT controller work this fork continues
- [henry2fa](https://github.com/henryfa2) — Discord and community support
