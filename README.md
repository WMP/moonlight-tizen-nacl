<p align=left>
	<a href="https://discord.gg/zHafSd3bTw">
		<img src="https://discordapp.com/api/guilds/1196915612522393651/widget.png?style=banner2" alt="Discord Banner 2"/> 
	</a>
</p>

## Moonlight-Tizen-NaCl
GameStream client for Samsung Smart TV's running Tizen OS (3.0 to 6.0)

---

## Xbox Wireless Controller (Bluetooth) Fix — fork notes

This is a personal development fork of [OneLiberty/moonlight-tizen-nacl](https://github.com/OneLiberty/moonlight-tizen-nacl) focused on fixing Xbox Wireless Controller support over Bluetooth on Samsung Tizen TVs (NaCL build).

### What was fixed

| Problem | Root cause | Fix |
|---|---|---|
| 10–20 s input lag burst after stream start, then timeout | `LiSendMultiControllerEvent` was called on every PPAPI timestamp tick (~60 Hz) regardless of whether controller state changed, flooding the control stream queue | State-change filter (`StateChangedEnough()`) — events are only sent when buttons/axes actually change |
| Controller visible in Windows `joy.cpl` but zero input when stream started via TV remote | Proactive `LiSendControllerArrivalEvent` with `activeGamepadMask=0x0` caused Sunshine/ViGEm to create then immediately destroy the virtual controller (within 6 ms) | Removed proactive arrival entirely; the real arrival is sent on first PPAPI connection |
| Duplicate controller arrival events causing input drop | Race between `OnConnectionStarted` (main thread) and `InputThreadFunc` (pthread): `ResetGamepadState` cleared `s_arrivalSent` after the input thread had already sent it | Removed `ResetGamepadState` call from `OnConnectionStarted` |
| Pad navigation broken after stream termination | `Controller.startWatching()` was not called in the `streamTerminated` JS handler | Restored `Controller.startWatching()` + `isInGame = false` in `streamTerminated` |

See [`GAMEPAD_DEBUG_NOTES.md`](GAMEPAD_DEBUG_NOTES.md) for detailed root-cause analysis, [`GAMEPAD_EXPERIMENTS.md`](GAMEPAD_EXPERIMENTS.md) for experiment history, and [`GAMEPAD_TEST_PLAN.md`](GAMEPAD_TEST_PLAN.md) for test procedures.

---

## Building from source with Docker

The repo includes a ready-to-use build toolchain based on Docker, which bundles Tizen Studio 5.6 and the Samsung NaCL SDK. No local Tizen installation is required.

### Step 1 — Build the SDK image (once)

```bash
docker build -t moonlight-tizen-nacl-sdk:5.6 -f Dockerfile.sdk .
```

This downloads and installs Tizen Studio and the NaCL SDK (~2 GB). It only needs to be run once; subsequent builds use the cached image.

### Step 2 — Build and install on TV

```bash
./build-install.sh <TV_IP>
# or set the env var once:
export MOONLIGHT_TV_IP=192.168.1.50
./build-install.sh
```

Options:
- `--clean` — run `make clean` before building (clears the NaCL object file cache)

The script:
1. Compiles the NaCL module inside a Docker container (using the host source tree via bind-mount, so incremental builds are fast)
2. Translates the `.pexe` to ARM `.nexe`
3. Packages the Tizen `.wgt` file
4. Installs it on the TV via `sdb`

#### TV developer mode

Before installing, enable Developer Mode on the TV:
1. Open `Apps` panel → type `12345` on the remote → turn on Developer mode → enter your PC's IP → restart the TV.

---

## Debug logging infrastructure

The repo includes three tools to capture and correlate gamepad events across the TV→Sunshine→ViGEm pipeline:

### `log-server.py` — runs on your PC (Linux/Mac/Windows)

A minimal HTTP server that receives log lines from both the TV and the Windows side and writes them to a timestamped log file.

```bash
python3 log-server.py                          # listens on 0.0.0.0:8765
python3 log-server.py --port 9000              # custom port
python3 log-server.py --log-file my.log        # custom log file
```

Health check: `curl http://localhost:8765/health`

### Configuring the log server URL in the Moonlight app

The TV app sends logs via HTTP POST. To set the target URL **without rebuilding**:

1. Open the Moonlight app on the TV.
2. In the top toolbar, click the **`LOG: OFF`** button.
3. Enter the URL of your PC running `log-server.py`, e.g. `http://192.168.1.100:8765/log`.
4. Click **Save**. The button label changes to `LOG: <host>` to confirm.

The URL is stored in `localStorage` and persists across app restarts. Click **Disable** to turn logging off again.

You can also control which event sources are logged with the adjacent **`DBG:`** button (cycles between `EVENTS`, `ALL`, `NACL`, `OFF`).

### `win-gamepad-logger.py` — runs on Windows (with Sunshine / ViGEm)

Polls the XInput virtual controller at 1000 Hz and reports button press/release events with timestamps to the same log server. Used to measure end-to-end input latency from TV button press to Windows ViGEm event.

```bash
python win-gamepad-logger.py --server http://192.168.1.100:8765/log
python win-gamepad-logger.py --server http://192.168.1.100:8765/log --controller 1
```

Requires Python 3 and Windows (uses `xinput1_4.dll`). No additional packages needed.

### Reading the logs

See [`GAMEPAD_LOGGING.md`](GAMEPAD_LOGGING.md) for the full field glossary, expected log sequences, and useful grep patterns.

Quick reference:

```bash
# High-level session flow
grep -E "GAMEPAD_INPUT|GAMEPAD_STREAM|WIN_CONTROLLER" tv_pad_debug.log

# Arrival + controller connect lifecycle
grep -E "ARRIVAL|CONNECTED|DISCONNECTED" tv_pad_debug.log

# Count state events (should only fire on actual input changes)
grep -c "LiSendMultiControllerEvent" tv_pad_debug.log
```

---

https://github.com/OneLiberty/moonlight-tizen-nacl


### Note
As a non-developer with limited coding knowledge, I do my best to maintain the repository and address issues. If you encounter problems, please report them in the issue section. While I can't guarantee a solution, I will certainly investigate.
This project is delivered as a POC, don't expect good performances and a fully working environement. 

I've spent a lot of time trying to make this work for older TVs, hopefully someone that is much more talented than me can improve the app ! 

### Build from source 
- Install pepper SDK 
- Clone the repo and `make` inside the folder. 

I couldn't get the pepper SDK to generate a correct file for NaCl especially the .nmf so i provide a modified .nmf to use for samsung port.

The build process generate a pnacl (.pexe) file which we need to translate to nacl (.nexe) and to arm

- `pnacl-translate -arch arm moonlight-chrome.pexe -o moonlight-chrome-arm.nexe `

This can then be used to package the app for samsung accordingly (see the docker file)

### Install the app
1. **Enable Developer Mode on Samsung Smart TV**:
   - Navigate to `Apps` panel, enter `12345` on the remote, turn on `Developer mode`, input your PC's IP, and restart the TV.
2. **Install and Launch Docker Image**:
   - Install Docker Desktop — [Installation Guide](https://docs.docker.com/desktop/)
   - Run in Windows PowerShell:
     ```
     docker run -it --rm ghcr.io/oneliberty/moonlight-tizen-nacl:samsung_nacl
     ```
3. **Install the Application**:
   - Connect to the TV via Smart Development Bridge, replacing `YOUR_TV_IP` with your TV's IP:
     ```
     sdb connect YOUR_TV_IP
     sdb devices
     ```
   - Install the app:
     ```
     tizen install -n MoonlightNaCl.wgt
     ```
     If you have multiple TVs connected, specify which TV to install by using this command instead:
     ```
     tizen install -n MoonlightNaCl.wgt -t YOUR_DEVICE_ID
     ```
     where `YOUR_DEVICE_ID` is the last column shown in `sdb devices`, something like `UE65NU7400`.
   - `exit`

4. **(Optional) Disable Developer Mode**:
   - Revisit the `Apps` panel to turn off Developer mode and restart the TV.

Moonlight should now be available under `Recent Apps` on your Samsung Smart TV.

>[!NOTE]
> `sdb` comes with tizen studio, so alternatively you can install tizen studio and use `sdb` to install it. 

## Contributing
- Contributions are welcome! Fork the repo, create pull requests, or open issues. If you find the project useful, consider giving it a star!
- Issues are welcomed, i wasn't able to test the app myself and i know there still are a lot of bugs, don't hesitate to report them. 

## Credits
- Moonlight for Chrome OS is developed and maintained by [Moonlight Developers](https://github.com/moonlight-stream/moonlight-chrome)
- Dockerfile have been readapted by [pablojrl123](https://github.com/pablojrl123/moonlight-tizen-docker)
- Huge thanks to [Phazeee](https://github.com/MrPhaze62) for doing all the testing for this version. 
- Thanks to [henry2fa](https://github.com/henryfa2) for the discord and more. 
