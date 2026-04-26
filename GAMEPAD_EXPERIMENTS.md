# Gamepad Experiments

---

## EXP-001 — Add gamepad debug infrastructure and fix input forwarding

- **Date**: 2026-04-24 (commit a70b624)
- **Branch/commit**: `gamepadNaCLTest` / a70b624
- **What changed**: Added `GAMEPAD_DEBUG` infrastructure, raw state logging, ARRIVAL_SENT logging, fixed `m_GamepadInputEnabled` forwarding from JS.
- **Hypothesis**: NaCl was not sending gamepad events at all because `m_GamepadInputEnabled` was always false.
- **Result**: Input started reaching Windows. Confirmed via joy.cpl.
- **Conclusion**: KEEP. Foundation for all further work.
- **Status**: ✅ kept

---

## EXP-002 — Default gamepad input to enabled

- **Date**: 2026-04-24 (commit 15ea740, later reverted: 7d4b58d)
- **Branch/commit**: `gamepadNaCLTest`
- **What changed**: `m_GamepadInputEnabled` initialised to `true` instead of `false`.
- **Hypothesis**: Input would work without needing JS to explicitly enable it.
- **Result**: Worked, but caused unexpected behaviour in some scenarios.
- **Conclusion**: REVERTED (7d4b58d). The JS-side `applyGamepadInputSetting()` call at module load is the correct mechanism.
- **Status**: ❌ reverted

---

## EXP-003 — Controller arrival event

- **Date**: 2026-04-24 (commit 42ed962)
- **Branch/commit**: `gamepadNaCLTest` / 42ed962
- **What changed**: Added `LiSendControllerArrivalEvent` call before first `LiSendMultiControllerEvent`, with error logging.
- **Hypothesis**: Sunshine/ViGEm requires an explicit arrival before it creates a virtual controller. Without it, controller appears in joy.cpl but input is ignored.
- **Result**: Controller appears correctly in joy.cpl.
- **Conclusion**: KEEP. Arrival event is mandatory.
- **Status**: ✅ kept

---

## EXP-004 — Proactive arrival with mask=0 at stream start

- **Date**: 2026-04-25 (commit 7f08c59)
- **Branch/commit**: `fix/pilot` / 7f08c59
- **What changed**: Added a proactive `LiSendControllerArrivalEvent(0, 0x0, ...)` call at stream start before any pad connects, to pre-warm ViGEm for the remote-start scenario.
- **Hypothesis**: If stream starts with TV remote before any pad is connected, Sunshine has no virtual controller ready. Pre-registering slot 0 with mask=0 would allow quicker pad takeover.
- **Result**: FAILED. Confirmed in `tv_pad_debug.log`:
  - `WIN_CONTROLLER_CONNECTED` at 00:22:33.586
  - `WIN_CONTROLLER_DISCONNECTED` at 00:22:33.592 (6ms later)
  - Real controller appeared only 3 s later at 00:22:36.207
  - Sunshine interprets `activeGamepadMask=0x0` as "no active controllers" and removes the virtual controller immediately after creating it.
- **Conclusion**: REMOVED. Proactive arrival with mask=0 is harmful. The real arrival sent when the pad is detected in PPAPI already carries the correct mask.
- **Status**: ❌ removed (2026-04-26)

---

## EXP-005 — State-change filtering for LiSendMultiControllerEvent

- **Date**: 2026-04-26
- **Branch/commit**: `feat/update-moonlight-common-c` (current, not yet committed)
- **What changed**:
  - Added `ControllerState` struct and `s_lastSentState[4]` / `s_hasLastSentState[4]`.
  - `LiSendMultiControllerEvent` is now only called when state differs from last sent, or on the first send after arrival.
  - Added `StateChangedEnough()` using exact field comparison (safe because deadzone already handles small axis drift).
- **Hypothesis**: `LiSendMultiControllerEvent` was called on every PPAPI timestamp-changed sample (~60 Hz) regardless of state. This flooded the control stream queue, causing the 10–20 s input lag burst seen in Problem 1.
- **Result**: PENDING — awaiting test on TV.
- **Conclusion**: PENDING
- **Status**: 🔲 pending test

---

## EXP-006 — Remove ResetGamepadState race in OnConnectionStarted

- **Date**: 2026-04-26
- **Branch/commit**: `feat/update-moonlight-common-c` (current)
- **What changed**: Removed `ResetGamepadState(true, "connection_started_main")` from `OnConnectionStarted()`.
- **Hypothesis**: `OnConnectionStarted` was called from the main thread after `InputThreadFunc` had already started and possibly sent an arrival. The reset cleared `s_arrivalSent[p] = false`, causing a duplicate arrival to be sent.
- **Result**: PENDING
- **Status**: 🔲 pending test

---

## EXP-007 — Restore Controller.startWatching() on streamTerminated

- **Date**: 2026-04-26
- **Branch/commit**: `feat/update-moonlight-common-c` (current)
- **What changed**: `messages.js` `streamTerminated` handler now calls `Controller.startWatching()` and sets `isInGame = false` before calling `showApps()`.
- **Hypothesis**: After stream termination, `playGameMode()` had stopped JS gamepad polling via `Controller.stopWatching()`. The `streamTerminated` handler called `showApps()` which restored keyboard/remote navigation via `Navigation.start()` but did not restart the gamepad polling interval.
- **Result**: PENDING
- **Status**: 🔲 pending test

---

## EXP-008 — ARRIVAL_CHECK debug throttle

- **Date**: 2026-04-26
- **Branch/commit**: `feat/update-moonlight-common-c` (current)
- **What changed**: `ARRIVAL_CHECK` debug log is now only emitted when `!s_arrivalSent[p]` (before arrival is sent). Previously logged on every PPAPI-timestamp-changed sample.
- **Hypothesis**: At 200 Hz InputThread, ~200 `pp::PostMessage` calls/second for ARRIVAL_CHECK alone were flooding the NaCl→JS IPC pipe and potentially contributing to lag.
- **Result**: PENDING
- **Status**: 🔲 pending test
