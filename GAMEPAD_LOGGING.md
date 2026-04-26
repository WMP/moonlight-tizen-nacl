# Gamepad Logging Reference

## Log sources

| Source | Meaning |
|--------|---------|
| `NACL` | NaCl / PPAPI side — emitted by `DebugPostMessage()` → `pp::PostMessage` → JS `handleMessage` → `remotePadDebugLog('NACL', ...)` |
| `WIN`  | Windows side — from `win-gamepad-logger.py` via joy.cpl / XInput |
| `EVENT`| JS UI events logged by `window.remotePadDebugLog('EVENT', ...)` |

## Log fields glossary

| Field | Meaning |
|-------|---------|
| `CONNECTED slot=N` | Pad detected in PPAPI as connected (was not connected last poll) |
| `DISCONNECTED slot=N` | Pad dropped from PPAPI connected state |
| `DETECT slot=N mode=STD\|BT` | Layout detection result. STD = standard 17b/4ax, BT = Tizen BT experimental |
| `ARRIVAL_CHECK slot=N sent=0` | Emitted once before arrival is sent, confirming arrival is pending |
| `ARRIVAL_SENT slot=N controllerIndex=N mask=0xN result=N` | `LiSendControllerArrivalEvent` called; `result=0` means queued successfully |
| `ARRIVAL_SETTLE slot=N settle=20` | 20 poll cycles (~100 ms) settle window started after arrival |
| `LiSendMultiControllerEvent slot=N ... result=N` | State event sent to host; only logged when state changed or on first send |
| `GAMEPAD_INPUT ENABLED\|DISABLED reason=X` | JS called `setGamepadInputEnabled`; triggers full state reset |
| `GAMEPAD_STREAM ACTIVE\|INACTIVE reason=X` | Stream start/stop recorded |
| `GAMEPAD_STATE_RESET reason=X resetConnected=N` | All arrival/state/timestamp flags cleared |
| `WIN_CONTROLLER_CONNECTED` | Windows side: virtual ViGEm controller appeared in joy.cpl |
| `WIN_CONTROLLER_DISCONNECTED` | Windows side: virtual ViGEm controller removed |
| `pressed=X` / `released=X` | Windows side: button event on virtual controller |
| `controllerIndex` | Compact index of connected controllers (0 = first connected pad) |
| `activeGamepadMask` | Bitmask: bit N set = controller slot N is active |
| `flags` | buttonFlags bitmask (A_FLAG, B_FLAG, etc.) |
| `LT/RT` | Left/right trigger 0–255 |
| `LS/RS` | Left/right stick X,Y in range −32767..32767 |
| `mode=STD` | Standard layout (Xbox BT or USB) |
| `mode=BT` | Tizen BT experimental layout (should NOT appear for Xbox Wireless) |
| `sameTsCount` | How many consecutive polls had identical PPAPI timestamp |

## Key grep patterns

```bash
# High-level session flow
grep -E "GAMEPAD_INPUT|GAMEPAD_STREAM|GAMEPAD_STATE_RESET|WIN_CONTROLLER" tv_pad_debug.log

# Arrival lifecycle
grep -E "ARRIVAL_CHECK|ARRIVAL_SENT|ARRIVAL_SETTLE|PROACTIVE_ARRIVAL" tv_pad_debug.log

# Input events (state changes)
grep "LiSendMultiControllerEvent" tv_pad_debug.log

# Windows reactions
grep -E "WIN_CONTROLLER|pressed=|released=" tv_pad_debug.log

# Detect wrong mapping
grep "mode=BT" tv_pad_debug.log   # Should be empty for Xbox Wireless

# All NaCl pad events
grep "source=NACL" tv_pad_debug.log

# Count how often state events were sent (should be low at idle)
grep -c "LiSendMultiControllerEvent" tv_pad_debug.log
```

## Expected normal log sequence — stream started with pad

```
UI playGameMode                        # JS: stream launch initiated
ProgressMsg: Starting ...              # NaCl stages
GAMEPAD_STREAM ACTIVE                  # NaCl stream active
GAMEPAD_STATE_RESET reason=stream_thread_start
CONNECTED slot=0 id="Xbox Wireless..."
DETECT slot=0 mode=STD
ARRIVAL_CHECK slot=0 sent=0            # Only logged while pending
ARRIVAL_SENT slot=0 controllerIndex=0 mask=0x1 result=0
ARRIVAL_SETTLE slot=0 settle=20
WIN_CONTROLLER_CONNECTED               # Sunshine created virtual controller
LiSendMultiControllerEvent slot=0 ... # First state (all zeros)
LiSendMultiControllerEvent slot=0 flags=0x1000 ...  # A pressed
```

## Expected normal log sequence — stream started with TV remote

```
UI playGameMode
GAMEPAD_STREAM ACTIVE
GAMEPAD_STATE_RESET reason=stream_thread_start
(no CONNECTED yet — pad not pressed)
(user presses A on pad)
CONNECTED slot=0 id="Xbox Wireless..."
DETECT slot=0 mode=STD
ARRIVAL_CHECK slot=0 sent=0
ARRIVAL_SENT slot=0 mask=0x1 result=0
ARRIVAL_SETTLE slot=0 settle=20
WIN_CONTROLLER_CONNECTED
LiSendMultiControllerEvent slot=0 flags=0x1000 ...  # A pressed
```

## Red flags

- `WIN_CONTROLLER_CONNECTED` followed by `WIN_CONTROLLER_DISCONNECTED` within <100ms → proactive arrival with mask=0 (should not appear after EXP-004 removal)
- `ARRIVAL_SENT` appearing more than once for the same slot without an intervening `DISCONNECTED` → ResetGamepadState race
- `mode=BT` for Xbox Wireless Controller → wrong layout detection
- `LiSendMultiControllerEvent` appearing 60+ times per second at idle → state-change filter not working
- Long gap between `ARRIVAL_SENT` and first `WIN_CONTROLLER_CONNECTED` → Sunshine taking too long to create ViGEm device
