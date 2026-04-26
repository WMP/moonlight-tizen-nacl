# Gamepad Test Plan

## Test A — Stream started with pad (Problem 1 regression)

**Goal**: No input lag, no timeout.

**Procedure**:
1. Launch Moonlight on TV.
2. Navigate the app list using the Xbox pad.
3. Press A to launch a game/app.
4. Wait for stream to start (loading spinner disappears, video visible).
5. Press A, B, move left stick, move right stick, press D-pad.
6. Check Windows `joy.cpl`.

**Expected**:
- joy.cpl shows controller present immediately at stream start (within ~200ms).
- Button presses and stick movement appear in joy.cpl with ≤100ms lag (no buffering burst).
- Moonlight does NOT disconnect within 60 s of idle input.
- Log contains exactly ONE `WIN_CONTROLLER_CONNECTED` event, no `WIN_CONTROLLER_DISCONNECTED` before the user manually disconnects.

**Logs to check**:
```
grep "ARRIVAL_SENT\|WIN_CONTROLLER\|GAMEPAD_INPUT\|GAMEPAD_STREAM" tv_pad_debug.log
```
- Expect single `ARRIVAL_SENT` with `mask=0x1`, followed by `WIN_CONTROLLER_CONNECTED`.
- No `PROACTIVE_ARRIVAL`.
- `LiSendMultiControllerEvent` log entries should only appear when a button/axis actually changed.

---

## Test B — Stream started with TV remote (Problem 2 regression)

**Goal**: Pad works immediately after connecting during an active stream.

**Procedure**:
1. Launch Moonlight on TV.
2. Navigate using the TV remote (IR), NOT the pad.
3. Launch a game/app with the remote.
4. Wait for stream to start.
5. Press A on the Xbox pad.
6. Check Windows `joy.cpl`.

**Expected**:
- joy.cpl shows controller appear after first button press.
- Button presses and stick movement work immediately (within ~200ms of first press).
- Log shows single `WIN_CONTROLLER_CONNECTED` (no prior connect/disconnect flap).

**Logs to check**:
```
grep "ARRIVAL_SENT\|WIN_CONTROLLER\|CONNECTED\|DISCONNECTED" tv_pad_debug.log
```
- Expect: pad CONNECTED → ARRIVAL_SENT → ARRIVAL_SETTLE → then input flows.
- No `WIN_CONTROLLER_CONNECTED` before user presses the pad.

---

## Test C — Reconnect during active stream

**Goal**: Pad disconnects and reconnects without stream restart.

**Procedure**:
1. Start stream (Test A or B).
2. Verify input works in joy.cpl.
3. Turn off the Xbox pad (hold Xbox button → power off).
4. Wait 3–5 s.
5. Turn the pad back on.
6. Press A.

**Expected**:
- Log shows DISCONNECTED → CONNECTED → ARRIVAL_SENT → ARRIVAL_SETTLE.
- After reconnect, joy.cpl shows pad and responds to buttons.
- No stream disconnect/timeout.

---

## Test D — Failed launch / return to menu (Problem 3 regression)

**Goal**: Pad menu navigation works after stream termination.

**Procedure**:
1. Launch Moonlight.
2. Navigate with pad, launch a game/app.
3. Wait for stream to terminate (force quit on Windows side, or close the app).
4. Moonlight returns to app list.
5. Use pad to navigate the app list (up/down, A to select).

**Expected**:
- Pad navigates the menu immediately after returning.
- No need to restart Moonlight.
- Log shows `streamTerminated` event followed by JS-side `Controller.startWatching` restored.

---

## Test E — USB regression

**Goal**: USB-connected pad still works correctly.

**Procedure**:
1. Connect Xbox pad via USB cable.
2. Launch Moonlight.
3. Navigate menu with pad.
4. Launch stream.
5. Verify joy.cpl shows controller and responds to input.

**Expected**:
- Menu navigation works with USB pad.
- Stream input works with USB pad.
- No Tizen BT mapping applied (verify mode=STD in logs, not mode=BT).

---

## Log grep reference

```bash
# Key events
grep -E "GAMEPAD_INPUT|CONNECTED|DISCONNECTED|ARRIVAL|MAPPED|LiSendMulti|STREAM" tv_pad_debug.log

# Confirm no proactive arrival
grep "PROACTIVE_ARRIVAL" tv_pad_debug.log

# Count send events (should only appear on state changes)
grep -c "LiSendMultiControllerEvent" tv_pad_debug.log

# Windows side events
grep "WIN_CONTROLLER\|pressed=\|released=" tv_pad_debug.log
```
