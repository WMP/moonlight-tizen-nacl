# Gamepad Debug Notes

## Platform context

- Device: Samsung TV / Tizen / NaCl / Pepper PPAPI
- Host: Windows + Sunshine
- Controller: Xbox Wireless Controller (USB-C) over Bluetooth to TV
- Branch: `feat/update-moonlight-common-c`

## Confirmed controller shape (JS Web Gamepad API)

ID: `"Xbox Wireless Controller" (STANDARD GAMEPAD Vendor: 0x0000 Product: 0x0000)`  
`buttons=17`, `axes=4`

| Button | Index |
|--------|-------|
| A      | b0    |
| B      | b1    |
| X      | b2    |
| Y      | b3    |
| LB     | b4    |
| RB     | b5    |
| LT     | b6    |
| RT     | b7    |
| View   | b8    |
| Menu   | b9    |
| LS click | b10 |
| RS click | b11 |
| D-up   | b12   |
| D-down | b13   |
| D-left | b14   |
| D-right| b15   |
| Xbox   | b16   |
| LX     | a0    |
| LY     | a1    |
| RX     | a2    |
| RY     | a3    |

**Rule**: pad with `buttons=17 && axes=4` OR id containing `"Xbox Wireless Controller"` → use standard layout (`k_StandardGamepadButtonMapping`), never the Tizen BT layout.

## Problem 1 — Input lag + timeout (stream started with pad)

**Scenario**: Launch stream using the pad. Input reaches Windows (joy.cpl sees controller and button presses) but with 10–20 s lag. All buffered events arrive at once, then Moonlight disconnects.

**Root cause confirmed**: `LiSendMultiControllerEvent` was called on every PPAPI sample whose timestamp had changed, with no check whether the controller STATE itself changed. At ~60 Hz PPAPI updates, this sent up to 60 events/second even when the pad was sitting at center/neutral. The moonlight-common-c control stream queue filled up, causing a growing backlog that appeared as lag.

**Secondary cause**: Proactive arrival with `activeGamepadMask=0x0` (removed — see below).

## Problem 2 — No input when stream started with TV remote (pilot)

**Scenario**: Launch stream using TV remote. joy.cpl shows a virtual controller. No buttons or sticks are detected.

**Root cause confirmed**: A "proactive" `LiSendControllerArrivalEvent` call was made at stream start with `controllerNumber=0, activeGamepadMask=0x0`. This told Sunshine "arrival for slot 0 but 0 active controllers", which caused Sunshine/ViGEm to create then immediately destroy the virtual controller. Confirmed in `tv_pad_debug.log`:
```
WIN_CONTROLLER_CONNECTED  00:22:33.586
WIN_CONTROLLER_DISCONNECTED 00:22:33.592   (6 ms gap)
WIN_CONTROLLER_CONNECTED  00:22:36.207   (3 s later — real arrival)
```
The virtual controller from the proactive arrival was in a broken state. When the real arrival came, it created a new valid virtual controller, but any input sent before that landed in the void.

**Secondary cause**: Race condition — `OnConnectionStarted()` (main thread) called `ResetGamepadState(true)` after `InputThreadFunc` had already sent the arrival. This reset `s_arrivalSent[p] = false`, causing a second arrival to be sent with a fresh settle countdown, delaying input further.

## Problem 3 — Pad stops working in Moonlight menu after stream termination

**Scenario**: Stream ends (or fails to launch). Moonlight returns to menu. TV remote works but pad buttons no longer navigate the menu.

**Root cause**: `messages.js` `streamTerminated` handler called `showApps()` which called `showAppsMode()` → `Navigation.start()` (keyboard/remote nav restored) but did NOT call `Controller.startWatching()`. The gamepad polling interval (`setInterval`) was cleared by `playGameMode()` → `Controller.stopWatching()` and never restarted.

## Design decisions

### State-change filtering (Fix for Problem 1)

Added `ControllerState` struct and `s_lastSentState[4]` / `s_hasLastSentState[4]`. `LiSendMultiControllerEvent` is now only called when:
- `!s_hasLastSentState[p]` — first send after arrival/reset (always goes through to initialise ViGEm state)
- OR `StateChangedEnough(current, last)` — any field changed

`StateChangedEnough` uses exact comparison. This is safe because `ApplyDeadZone()` already zeroes small axis drift (threshold 0.1), and the resulting `short` values will be stable unless the user is actually moving the stick.

No keepalive interval is added. moonlight-common-c handles connection keepalive at the transport layer independently of controller events.

### Proactive arrival removed (Fix for Problem 2)

The proactive arrival with `mask=0x0` was harmful because Sunshine interprets `activeGamepadMask=0` as "no active controllers". Sunshine creates the ViGEm device and then immediately removes it. The real arrival (sent when the pad is actually detected in PPAPI) now always carries the correct `activeGamepadMask` and arrives without a prior conflicting event.

### OnConnectionStarted race (Fix for Problem 2)

Removed `ResetGamepadState(true, "connection_started_main")` from `OnConnectionStarted()`. The reset is already done in `ConnectionThreadFunc` before `InputThreadFunc` starts, so the main-thread reset was redundant and caused a race (resetting `s_arrivalSent` after InputThread had already sent it).

### Controller.startWatching() in streamTerminated (Fix for Problem 3)

`messages.js` now calls `Controller.startWatching()` and sets `isInGame = false` before calling `showApps()` when handling `streamTerminated`. This mirrors what `stopGame()` already did.

## ARRIVAL_CHECK debug throttle

`ARRIVAL_CHECK` was previously logged on every PPAPI-timestamp-changed sample (up to 200/s at 5ms InputThread interval). This produced hundreds of `pp::PostMessage` calls per second, flooding the NaCl→JS IPC pipe and contributing to lag. Now only logged when `!s_arrivalSent[p]` (before the first arrival per connection).
