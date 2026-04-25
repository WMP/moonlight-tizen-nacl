#include "moonlight.hpp"
#include "ppapi/c/ppb_gamepad.h"
#include <Limelight.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#define AXIS_DEAD_ZONE 0.1f

// Debug settings
#define GAMEPAD_DEBUG 1

// Send debug overlay message every N changed samples.
// 1 = very noisy, 5 = good for manual testing, 30 = quieter.
#define GAMEPAD_DEBUG_POST_EVERY 5

// Print full raw state every N changed samples to printf.
// This may not appear on your TV, but keep it as fallback.
#define GAMEPAD_DEBUG_FULL_EVERY 30

#define GAMEPAD_DEBUG_BUTTON_THRESHOLD 0.05f
#define GAMEPAD_DEBUG_AXIS_THRESHOLD 0.05f

static const unsigned short k_StandardGamepadButtonMapping[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    LB_FLAG, RB_FLAG,
    0, 0,
    BACK_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    SPECIAL_FLAG
};

static const unsigned short k_BTGamepadButtonMapping[] = {
    A_FLAG,       // btn0
    B_FLAG,       // btn1
    0,            // btn2 unused / unknown
    X_FLAG,       // btn3
    Y_FLAG,       // btn4
    0,            // btn5 unused / unknown
    0,            // btn6 right stick X in current Tizen BT guess
    0,            // btn7 right stick Y in current Tizen BT guess
    BACK_FLAG,    // btn8
    PLAY_FLAG,    // btn9
    LS_CLK_FLAG,  // btn10
    RS_CLK_FLAG,  // btn11
    UP_FLAG,      // btn12
    DOWN_FLAG,    // btn13
    LEFT_FLAG,    // btn14
    RIGHT_FLAG,   // btn15
    SPECIAL_FLAG  // btn16
};

static bool s_padSeen[4] = {false, false, false, false};
static bool s_isTizenBT[4] = {false, false, false, false};
static bool s_connectedLast[4] = {false, false, false, false};
static bool s_arrivalSent[4] = {false, false, false, false};
static int  s_arrivalSettleCount[4] = {0, 0, 0, 0};
// Tracks whether a proactive (pre-pad) arrival was sent at stream start.
// Not cleared by ResetGamepadState so the ViGEm slot stays warm.
static bool s_proactiveArrivalDone[4] = {false, false, false, false};
static bool s_inputActiveLast[4] = {false, false, false, false};
static bool s_gamepadInputEnabled = true;
static bool s_streamActive = false;
static unsigned int s_debugCounter[4] = {0, 0, 0, 0};
static unsigned int s_sameTimestampCount[4] = {0, 0, 0, 0};

static void DebugPostMessage(const std::string& msg);

static bool IsKnownStandardLayoutPad(const PP_GamepadSampleData& padData) {
    char idBuffer[sizeof(padData.id) + 1];
    memset(idBuffer, 0, sizeof(idBuffer));

    for (unsigned int i = 0; i < sizeof(padData.id); i++) {
        idBuffer[i] = padData.id[i];

        if (padData.id[i] == '\0') {
            break;
        }
    }

    // Xbox Wireless Controller over Bluetooth on this Samsung TV reports the
    // standard Web Gamepad layout with 17 buttons and 4 axes, so do not route
    // it through the experimental Tizen BT mapping. Keep that same treatment
    // for any controller that exposes the same standard 17/4 layout.
    return std::string(idBuffer).find("Xbox Wireless Controller") != std::string::npos ||
           (padData.buttons_length == 17 && padData.axes_length == 4);
}

static float ApplyDeadZone(float value) {
    if (value > -AXIS_DEAD_ZONE && value < AXIS_DEAD_ZONE) {
        return 0.0f;
    }

    return value;
}

static unsigned int GetSafeButtonCount(const PP_GamepadSampleData& padData) {
    const unsigned int buttonCapacity =
        sizeof(padData.buttons) / sizeof(padData.buttons[0]);

    return padData.buttons_length < buttonCapacity ?
        padData.buttons_length :
        buttonCapacity;
}

static unsigned int GetSafeAxisCount(const PP_GamepadSampleData& padData) {
    const unsigned int axisCapacity =
        sizeof(padData.axes) / sizeof(padData.axes[0]);

    return padData.axes_length < axisCapacity ?
        padData.axes_length :
        axisCapacity;
}

static bool HasMappedInput(
    int buttonFlags,
    unsigned char leftTrigger,
    unsigned char rightTrigger,
    short leftStickX,
    short leftStickY,
    short rightStickX,
    short rightStickY)
{
    return buttonFlags != 0 ||
           leftTrigger != 0 ||
           rightTrigger != 0 ||
           leftStickX != 0 ||
           leftStickY != 0 ||
           rightStickX != 0 ||
           rightStickY != 0;
}

static bool SendControllerArrivalEventWithLog(
    unsigned int slot,
    short controllerIndex,
    short activeGamepadMask,
    const char* reason)
{
    int arrResult = LiSendControllerArrivalEvent(
        controllerIndex,
        activeGamepadMask,
        LI_CTYPE_XBOX,
        0xFFFF,
        LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE);

    bool sent = arrResult == 0;

#if GAMEPAD_DEBUG
    std::ostringstream arrSs;
    arrSs << "ARRIVAL_SENT"
          << " slot=" << slot
          << " controllerIndex=" << controllerIndex
          << " mask=0x" << std::hex << static_cast<unsigned int>(activeGamepadMask)
          << " result=" << std::dec << arrResult
          << " sent=" << (sent ? 1 : 0)
          << " reason=" << (reason != NULL ? reason : "unknown")
          << " stream=" << (s_streamActive ? 1 : 0)
          << " input=" << (s_gamepadInputEnabled ? 1 : 0);
    DebugPostMessage(arrSs.str());

    printf("[PADDBG] %s\n", arrSs.str().c_str());
    fflush(stdout);
#else
    (void)slot;
    (void)reason;
#endif

    return sent;
}

static void DebugPostMessage(const std::string& msg) {
#if GAMEPAD_DEBUG
    if (g_Instance != NULL) {
        pp::Var response(std::string("padDebug: ") + msg);
        g_Instance->PostMessage(response);
    }
#else
    (void)msg;
#endif
}

#if GAMEPAD_DEBUG

static std::string GetPadIdString(const PP_GamepadSampleData& padData) {
    char idBuffer[sizeof(padData.id) + 1];
    memset(idBuffer, 0, sizeof(idBuffer));

    for (unsigned int i = 0; i < sizeof(padData.id); i++) {
        idBuffer[i] = padData.id[i];

        if (padData.id[i] == '\0') {
            break;
        }
    }

    idBuffer[sizeof(padData.id)] = '\0';
    return std::string(idBuffer);
}

static void DebugPrintfPadId(unsigned int p, const PP_GamepadSampleData& padData) {
    std::string id = GetPadIdString(padData);

    printf("[PADDBG] slot=%u id=\"%s\"\n", p, id.c_str());
    fflush(stdout);
}

static void DebugConnection(unsigned int p, const PP_GamepadSampleData& padData) {
    std::ostringstream ss;

    ss << "CONNECTED"
       << " slot=" << p
       << " id=\"" << GetPadIdString(padData) << "\""
       << " ts=" << static_cast<unsigned long long>(padData.timestamp)
       << " buttons=" << padData.buttons_length
       << " axes=" << padData.axes_length
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static void DebugDisconnection(unsigned int p) {
    std::ostringstream ss;

    ss << "DISCONNECTED"
       << " slot=" << p
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static void DebugDetection(unsigned int p, const PP_GamepadSampleData& padData, bool isTizenBT) {
    float ax0 = 0.0f;
    float ax1 = 0.0f;

    if (padData.axes_length > 0) {
        ax0 = padData.axes[0];
    }

    if (padData.axes_length > 1) {
        ax1 = padData.axes[1];
    }

    std::ostringstream ss;

    ss << "DETECT"
       << " slot=" << p
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " ax0=" << ax0
       << " ax1=" << ax1
       << " buttons=" << padData.buttons_length
       << " axes=" << padData.axes_length
       << " id=\"" << GetPadIdString(padData) << "\"";

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static void DebugWaitingForAxes(unsigned int p, const PP_GamepadSampleData& padData) {
    std::ostringstream ss;

    ss << "WAIT_AXES"
       << " slot=" << p
       << " buttons=" << padData.buttons_length
       << " axes=" << padData.axes_length
       << " ts=" << static_cast<unsigned long long>(padData.timestamp);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static bool HasActiveRawInput(const PP_GamepadSampleData& padData) {
    const unsigned int buttonCount = GetSafeButtonCount(padData);
    const unsigned int axisCount = GetSafeAxisCount(padData);

    for (unsigned int i = 0; i < buttonCount; i++) {
        if (padData.buttons[i] > GAMEPAD_DEBUG_BUTTON_THRESHOLD) {
            return true;
        }
    }

    for (unsigned int i = 0; i < axisCount; i++) {
        if (padData.axes[i] > GAMEPAD_DEBUG_AXIS_THRESHOLD ||
            padData.axes[i] < -GAMEPAD_DEBUG_AXIS_THRESHOLD) {
            return true;
        }
    }

    return false;
}

static std::string BuildRawInputSummary(const PP_GamepadSampleData& padData) {
    std::ostringstream ss;
    bool first = true;
    const unsigned int buttonCount = GetSafeButtonCount(padData);
    const unsigned int axisCount = GetSafeAxisCount(padData);

    for (unsigned int i = 0; i < buttonCount; i++) {
        if (padData.buttons[i] > GAMEPAD_DEBUG_BUTTON_THRESHOLD) {
            if (!first) {
                ss << " ";
            }

            ss << "b" << i << "=" << padData.buttons[i];
            first = false;
        }
    }

    for (unsigned int i = 0; i < axisCount; i++) {
        if (padData.axes[i] > GAMEPAD_DEBUG_AXIS_THRESHOLD ||
            padData.axes[i] < -GAMEPAD_DEBUG_AXIS_THRESHOLD) {
            if (!first) {
                ss << " ";
            }

            ss << "a" << i << "=" << padData.axes[i];
            first = false;
        }
    }

    if (first) {
        ss << "-";
    }

    return ss.str();
}

static void DebugTimestampRepeat(
    unsigned int p,
    const PP_GamepadSampleData& padData,
    bool isTizenBT,
    unsigned int repeatCount)
{
    std::ostringstream ss;

    ss << "SAME_TS"
       << " slot=" << p
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " count=" << repeatCount
       << " ts=" << static_cast<unsigned long long>(padData.timestamp)
       << " raw=" << BuildRawInputSummary(padData);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static void DebugTimestampRecovered(
    unsigned int p,
    const PP_GamepadSampleData& padData,
    bool isTizenBT,
    unsigned int repeatCount)
{
    std::ostringstream ss;

    ss << "TS_RECOVERED"
       << " slot=" << p
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " previousSameTsCount=" << repeatCount
       << " ts=" << static_cast<unsigned long long>(padData.timestamp)
       << " raw=" << BuildRawInputSummary(padData);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
}

static void DebugRawStatePrintf(unsigned int p, const PP_GamepadSampleData& padData, bool isTizenBT) {
    const unsigned int buttonCount = GetSafeButtonCount(padData);
    const unsigned int axisCount = GetSafeAxisCount(padData);

    printf("[PADDBG] slot=%u raw begin mode=%s timestamp=%llu buttons=%u axes=%u\n",
        p,
        isTizenBT ? "BT" : "STD",
        static_cast<unsigned long long>(padData.timestamp),
        padData.buttons_length,
        padData.axes_length);

    DebugPrintfPadId(p, padData);

    for (unsigned int i = 0; i < buttonCount; i++) {
        printf("[PADDBG] slot=%u button[%u]=%.6f\n",
            p,
            i,
            padData.buttons[i]);
    }

    for (unsigned int i = 0; i < axisCount; i++) {
        printf("[PADDBG] slot=%u axis[%u]=%.6f\n",
            p,
            i,
            padData.axes[i]);
    }

    printf("[PADDBG] slot=%u raw end\n", p);
    fflush(stdout);
}

static void DebugActiveOverlay(
    unsigned int p,
    const PP_GamepadSampleData& padData,
    bool isTizenBT,
    int buttonFlags,
    unsigned char leftTrigger,
    unsigned char rightTrigger,
    short leftStickX,
    short leftStickY,
    short rightStickX,
    short rightStickY)
{
    std::ostringstream ss;
    const unsigned int buttonCount = GetSafeButtonCount(padData);
    const unsigned int axisCount = GetSafeAxisCount(padData);

    ss << "slot=" << p
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " btns=" << padData.buttons_length
       << " axes=" << padData.axes_length
       << " flags=0x" << std::hex << buttonFlags << std::dec
       << " LT=" << static_cast<unsigned int>(leftTrigger)
       << " RT=" << static_cast<unsigned int>(rightTrigger)
       << " LS=(" << leftStickX << "," << leftStickY << ")"
       << " RS=(" << rightStickX << "," << rightStickY << ")";

    bool hasActiveInput = false;

    for (unsigned int i = 0; i < buttonCount; i++) {
        if (padData.buttons[i] > GAMEPAD_DEBUG_BUTTON_THRESHOLD) {
            ss << " b" << i << "=" << padData.buttons[i];
            hasActiveInput = true;
        }
    }

    for (unsigned int i = 0; i < axisCount; i++) {
        if (padData.axes[i] > GAMEPAD_DEBUG_AXIS_THRESHOLD ||
            padData.axes[i] < -GAMEPAD_DEBUG_AXIS_THRESHOLD) {
            ss << " a" << i << "=" << padData.axes[i];
            hasActiveInput = true;
        }
    }

    if (hasActiveInput) {
        DebugPostMessage(ss.str());
    }
}

static void DebugFullRawRemote(unsigned int p, const PP_GamepadSampleData& padData, bool isTizenBT) {
    std::ostringstream ss;
    const unsigned int buttonCount = GetSafeButtonCount(padData);
    const unsigned int axisCount = GetSafeAxisCount(padData);

    ss << "RAW slot=" << p
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " btns=" << padData.buttons_length
       << " axes=" << padData.axes_length;

    for (unsigned int i = 0; i < buttonCount; i++) {
        ss << " b" << i << "=" << padData.buttons[i];
    }

    for (unsigned int i = 0; i < axisCount; i++) {
        ss << " a" << i << "=" << padData.axes[i];
    }

    DebugPostMessage(ss.str());
}

static void DebugMappedPrintf(
    unsigned int p,
    short controllerIndex,
    short activeGamepadMask,
    bool isTizenBT,
    int buttonFlags,
    unsigned char leftTrigger,
    unsigned char rightTrigger,
    short leftStickX,
    short leftStickY,
    short rightStickX,
    short rightStickY)
{
    printf("[PADDBG] slot=%u mapped controllerIndex=%d activeMask=0x%04x mode=%s "
           "flags=0x%08x LT=%u RT=%u LS=(%d,%d) RS=(%d,%d)\n",
        p,
        controllerIndex,
        static_cast<unsigned int>(activeGamepadMask),
        isTizenBT ? "BT" : "STD",
        static_cast<unsigned int>(buttonFlags),
        static_cast<unsigned int>(leftTrigger),
        static_cast<unsigned int>(rightTrigger),
        leftStickX,
        leftStickY,
        rightStickX,
        rightStickY);

    fflush(stdout);
}

static void DebugMappedRemote(
    unsigned int p,
    short controllerIndex,
    short activeGamepadMask,
    bool isTizenBT,
    int buttonFlags,
    unsigned char leftTrigger,
    unsigned char rightTrigger,
    short leftStickX,
    short leftStickY,
    short rightStickX,
    short rightStickY,
    unsigned int sameTimestampCount)
{
    bool hasMappedInput = HasMappedInput(
        buttonFlags,
        leftTrigger,
        rightTrigger,
        leftStickX,
        leftStickY,
        rightStickX,
        rightStickY);

    if (!hasMappedInput && sameTimestampCount == 0) {
        return;
    }

    std::ostringstream ss;

    ss << "MAPPED"
       << " slot=" << p
       << " controllerIndex=" << controllerIndex
       << " activeMask=0x" << std::hex << static_cast<unsigned int>(activeGamepadMask)
       << " flags=0x" << static_cast<unsigned int>(buttonFlags) << std::dec
       << " mode=" << (isTizenBT ? "BT" : "STD")
       << " LT=" << static_cast<unsigned int>(leftTrigger)
       << " RT=" << static_cast<unsigned int>(rightTrigger)
       << " LS=(" << leftStickX << "," << leftStickY << ")"
       << " RS=(" << rightStickX << "," << rightStickY << ")"
       << " sameTsCount=" << sameTimestampCount
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());
}

#endif

void MoonlightInstance::ResetGamepadState(bool resetConnectedState, const char* reason) {
    for (int i = 0; i < 4; i++) {
        s_arrivalSent[i] = false;
        s_arrivalSettleCount[i] = 0;
        s_inputActiveLast[i] = false;
        s_debugCounter[i] = 0;
        s_sameTimestampCount[i] = 0;
        m_LastPadTimestamps[i] = 0.0;

        if (resetConnectedState) {
            s_padSeen[i] = false;
            s_isTizenBT[i] = false;
            s_connectedLast[i] = false;
        }
    }

#if GAMEPAD_DEBUG
    std::ostringstream ss;

    ss << "GAMEPAD_STATE_RESET"
       << " reason=" << (reason != NULL ? reason : "unknown")
       << " resetConnected=" << (resetConnectedState ? 1 : 0)
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
#else
    (void)reason;
#endif
}

void MoonlightInstance::SetGamepadInputEnabledState(bool enabled, const char* reason) {
    m_GamepadInputEnabled = enabled;
    s_gamepadInputEnabled = enabled;
    ResetGamepadState(true, enabled ? "input_enabled" : "input_disabled");

#if GAMEPAD_DEBUG
    std::ostringstream ss;

    ss << "GAMEPAD_INPUT " << (enabled ? "ENABLED" : "DISABLED")
       << " reason=" << (reason != NULL ? reason : "unknown")
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
#else
    (void)reason;
#endif
}

void MoonlightInstance::SetGamepadStreamState(bool active, const char* reason) {
    s_streamActive = active;

    if (!active) {
        // Clear proactive flags when stream ends so next stream starts fresh.
        for (int i = 0; i < 4; i++) {
            s_proactiveArrivalDone[i] = false;
        }
    }

#if GAMEPAD_DEBUG
    std::ostringstream ss;

    ss << "GAMEPAD_STREAM " << (active ? "ACTIVE" : "INACTIVE")
       << " reason=" << (reason != NULL ? reason : "unknown")
       << " stream=" << (s_streamActive ? 1 : 0)
       << " input=" << (s_gamepadInputEnabled ? 1 : 0);

    DebugPostMessage(ss.str());

    printf("[PADDBG] %s\n", ss.str().c_str());
    fflush(stdout);
#else
    (void)reason;
#endif
}

static short GetActiveGamepadMask(PP_GamepadsSampleData& gamepadData) {
    short controllerIndex = 0;
    short activeGamepadMask = 0;

    for (unsigned int p = 0; p < gamepadData.length; p++) {
        PP_GamepadSampleData& padData = gamepadData.items[p];

        if (!padData.connected) {
            continue;
        }

        activeGamepadMask |= (1 << controllerIndex);
        controllerIndex++;
    }

    return activeGamepadMask;
}

void MoonlightInstance::PollGamepads() {
    PP_GamepadsSampleData gamepadData;
    short controllerIndex = 0;
    short activeGamepadMask;

    m_GamepadApi->Sample(pp_instance(), &gamepadData);
    activeGamepadMask = GetActiveGamepadMask(gamepadData);

    // Send a proactive arrival for slot 0 at stream start (before any pad
    // connects) so ViGEm is fully initialised when the pad later shows up.
    // Sunshine can handle a "re-arrival" for a slot that is already registered,
    // so the later real arrival (sent when the pad connects) simply refreshes
    // the entry rather than triggering a new device creation mid-stream.
    if (s_streamActive && !s_proactiveArrivalDone[0]) {
        s_proactiveArrivalDone[0] = true;
        int proactiveResult = LiSendControllerArrivalEvent(
            0, 0x0, LI_CTYPE_XBOX, 0xFFFF,
            LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE);

#if GAMEPAD_DEBUG
        std::ostringstream preSs;
        preSs << "PROACTIVE_ARRIVAL slot=0 result=" << proactiveResult
              << " stream=" << (s_streamActive ? 1 : 0)
              << " input=" << (s_gamepadInputEnabled ? 1 : 0);
        DebugPostMessage(preSs.str());
        printf("[PADDBG] %s\n", preSs.str().c_str());
        fflush(stdout);
#endif
    }

    for (unsigned int p = 0; p < gamepadData.length; p++) {
        PP_GamepadSampleData& padData = gamepadData.items[p];
        const unsigned int safeButtonCount = GetSafeButtonCount(padData);
        const unsigned int safeAxisCount = GetSafeAxisCount(padData);

        if (!padData.connected) {
            if (p < 4 && s_connectedLast[p]) {
#if GAMEPAD_DEBUG
                DebugDisconnection(p);
#endif
            }

            if (p < 4) {
                s_padSeen[p] = false;
                s_isTizenBT[p] = false;
                s_connectedLast[p] = false;
                s_arrivalSent[p] = false;
                s_arrivalSettleCount[p] = 0;
                s_inputActiveLast[p] = false;
                s_debugCounter[p] = 0;
                s_sameTimestampCount[p] = 0;
                m_LastPadTimestamps[p] = 0.0;
            }

            // Do NOT increment controllerIndex for disconnected slots.
            // controllerIndex is compact index of connected controllers.
            continue;
        }

        if (p < 4 && !s_connectedLast[p]) {
            s_padSeen[p] = false;
            s_isTizenBT[p] = false;
            s_debugCounter[p] = 0;
            s_sameTimestampCount[p] = 0;
            s_arrivalSent[p] = false;
            s_arrivalSettleCount[p] = 0;
            s_inputActiveLast[p] = false;
            m_LastPadTimestamps[p] = 0.0;
            s_connectedLast[p] = true;

#if GAMEPAD_DEBUG
            DebugConnection(p, padData);
#endif
        }

        // Layout detection.
        //
        // Current heuristic:
        // - Standard USB usually reports left stick center around 0.0 / 0.0
        // - Some Tizen BT layouts report left stick center around 1.0 / 1.0
        if (p < 4 && !s_padSeen[p]) {
            if (safeAxisCount >= 2) {
                s_padSeen[p] = true;
                s_isTizenBT[p] = false;

                if (IsKnownStandardLayoutPad(padData)) {
                    // Xbox BT on this TV is exposed as a standard 17-button,
                    // 4-axis Web Gamepad device, so keep standard mapping.
                    s_isTizenBT[p] = false;
                }

#if GAMEPAD_DEBUG
                DebugDetection(p, padData, s_isTizenBT[p]);
#endif
            }
            else {
#if GAMEPAD_DEBUG
                DebugWaitingForAxes(p, padData);
#endif
            }
        }

        bool isTizenBT = false;

        if (p < 4) {
            isTizenBT = s_isTizenBT[p];
        }

        if (p < 4) {
            // Skip if the timestamp hasn't changed — no new data from Pepper.
            // ResetGamepadState() clears m_LastPadTimestamps so a fresh stream
            // always processes the first sample.
            bool sameTimestamp =
                padData.timestamp != 0.0 &&
                padData.timestamp == m_LastPadTimestamps[p];

#if GAMEPAD_DEBUG
            if (sameTimestamp) {
                s_sameTimestampCount[p]++;

                if (HasActiveRawInput(padData) ||
                    s_sameTimestampCount[p] <= 3 ||
                    s_sameTimestampCount[p] % 25 == 0) {
                    DebugTimestampRepeat(
                        p,
                        padData,
                        isTizenBT,
                        s_sameTimestampCount[p]);
                }
            }
            else if (s_sameTimestampCount[p] > 0) {
                DebugTimestampRecovered(
                    p,
                    padData,
                    isTizenBT,
                    s_sameTimestampCount[p]);
                s_sameTimestampCount[p] = 0;
            }
#else
            if (!sameTimestamp) {
                s_sameTimestampCount[p] = 0;
            }
#endif

            m_LastPadTimestamps[p] = padData.timestamp;
            s_debugCounter[p]++;

            if (sameTimestamp) {
                controllerIndex++;
                continue;
            }
        }

        int buttonFlags = 0;
        unsigned char leftTrigger = 0;
        unsigned char rightTrigger = 0;
        short leftStickX = 0;
        short leftStickY = 0;
        short rightStickX = 0;
        short rightStickY = 0;

        const unsigned short* mapping = isTizenBT ?
            k_BTGamepadButtonMapping :
            k_StandardGamepadButtonMapping;

        size_t mappingSize = isTizenBT ?
            sizeof(k_BTGamepadButtonMapping) / sizeof(k_BTGamepadButtonMapping[0]) :
            sizeof(k_StandardGamepadButtonMapping) / sizeof(k_StandardGamepadButtonMapping[0]);

        if (isTizenBT) {
            // Experimental Tizen Bluetooth layout.

            for (unsigned int i = 0; i < safeButtonCount && i < mappingSize; i++) {
                if (mapping[i] && padData.buttons[i] > 0.5f) {
                    buttonFlags |= mapping[i];
                }
            }

            // Left stick: axes offset by 1.0, range 0..2, center near 1.0.
            if (safeAxisCount >= 2) {
                float lx = padData.axes[0] - 1.0f;
                float ly = padData.axes[1] - 1.0f;

                leftStickX = static_cast<short>(ApplyDeadZone(lx) * 0x7FFF);
                leftStickY = static_cast<short>(-ApplyDeadZone(ly) * 0x7FFF);
            }

            // Right stick: guessed packed layout in button[6] and button[7].
            // Range 0..1, center near 0.5, remap to -1..1.
            if (safeButtonCount > 7) {
                float rx = (padData.buttons[6] - 0.5f) * 2.0f;
                float ry = (padData.buttons[7] - 0.5f) * 2.0f;

                rightStickX = static_cast<short>(ApplyDeadZone(rx) * 0x7FFF);
                rightStickY = static_cast<short>(-ApplyDeadZone(ry) * 0x7FFF);
            }

            // Do not guess LB/RB/LT/RT yet.
            // Use overlay to find whether they appear as button[N] or axis[N].

        } else {
            // Standard USB layout.
            for (unsigned int i = 0; i < safeButtonCount && i < mappingSize; i++) {
                if (i == 6) {
                    leftTrigger = static_cast<unsigned char>(padData.buttons[i] * 0xFF);
                    continue;
                }

                if (i == 7) {
                    rightTrigger = static_cast<unsigned char>(padData.buttons[i] * 0xFF);
                    continue;
                }

                if (mapping[i] && padData.buttons[i] > 0.5f) {
                    buttonFlags |= mapping[i];
                }
            }

            if (safeAxisCount >= 2) {
                leftStickX = static_cast<short>(ApplyDeadZone(padData.axes[0]) * 0x7FFF);
                leftStickY = static_cast<short>(-ApplyDeadZone(padData.axes[1]) * 0x7FFF);
            }

            if (safeAxisCount >= 4) {
                rightStickX = static_cast<short>(ApplyDeadZone(padData.axes[2]) * 0x7FFF);
                rightStickY = static_cast<short>(-ApplyDeadZone(padData.axes[3]) * 0x7FFF);
            }
        }

#if GAMEPAD_DEBUG
        if (p < 4) {
            if (s_debugCounter[p] % GAMEPAD_DEBUG_POST_EVERY == 1) {
                DebugActiveOverlay(
                    p,
                    padData,
                    isTizenBT,
                    buttonFlags,
                    leftTrigger,
                    rightTrigger,
                    leftStickX,
                    leftStickY,
                    rightStickX,
                    rightStickY);
            }

            if (s_debugCounter[p] % GAMEPAD_DEBUG_FULL_EVERY == 1) {
                DebugRawStatePrintf(p, padData, isTizenBT);
                DebugFullRawRemote(p, padData, isTizenBT);
                DebugMappedPrintf(
                    p,
                    controllerIndex,
                    activeGamepadMask,
                    isTizenBT,
                    buttonFlags,
                    leftTrigger,
                    rightTrigger,
                    leftStickX,
                    leftStickY,
                    rightStickX,
                    rightStickY);
            }

            if (s_debugCounter[p] % GAMEPAD_DEBUG_POST_EVERY == 1) {
                DebugMappedRemote(
                    p,
                    controllerIndex,
                    activeGamepadMask,
                    isTizenBT,
                    buttonFlags,
                    leftTrigger,
                    rightTrigger,
                    leftStickX,
                    leftStickY,
                    rightStickX,
                    rightStickY,
                    s_sameTimestampCount[p]);
            }
        }
#endif

        if (m_GamepadInputEnabled) {
            bool hasMappedInput = HasMappedInput(
                buttonFlags,
                leftTrigger,
                rightTrigger,
                leftStickX,
                leftStickY,
                rightStickX,
                rightStickY);
            bool inputEdge = false;

            if (p < 4) {
                inputEdge = hasMappedInput && !s_inputActiveLast[p];
#if GAMEPAD_DEBUG
                std::ostringstream checkSs;
                checkSs << "ARRIVAL_CHECK"
                        << " slot=" << p
                        << " controllerIndex=" << controllerIndex
                        << " sent=" << (s_arrivalSent[p] ? 1 : 0)
                        << " settle=" << s_arrivalSettleCount[p]
                        << " hasInput=" << (hasMappedInput ? 1 : 0)
                        << " inputEdge=" << (inputEdge ? 1 : 0)
                        << " mask=0x" << std::hex << static_cast<unsigned int>(activeGamepadMask)
                        << std::dec
                        << " stream=" << (s_streamActive ? 1 : 0)
                        << " input=" << (s_gamepadInputEnabled ? 1 : 0);
                DebugPostMessage(checkSs.str());
#endif
            }

            // Send arrival as soon as stream is active and pad is connected,
            // BEFORE any state events. After arrival we hold off state events
            // for ~100ms (20 poll cycles) to let Sunshine/ViGEm finish creating
            // the virtual controller before we drive its state.
            if (p < 4 && s_streamActive && !s_arrivalSent[p]) {
                const bool arrivalJustSent = SendControllerArrivalEventWithLog(
                    p,
                    controllerIndex,
                    activeGamepadMask,
                    "pre_state");

                s_arrivalSent[p] = arrivalJustSent;
                if (arrivalJustSent) {
                    // 20 polls × 5ms = ~100ms settle window.
                    // LiSendControllerArrivalEvent already queues one zero-state
                    // MC event internally, so no explicit prime needed here.
                    s_arrivalSettleCount[p] = 20;

#if GAMEPAD_DEBUG
                    std::ostringstream settleSs;
                    settleSs << "ARRIVAL_SETTLE"
                             << " slot=" << p
                             << " settle=" << s_arrivalSettleCount[p]
                             << " stream=" << (s_streamActive ? 1 : 0)
                             << " input=" << (s_gamepadInputEnabled ? 1 : 0);
                    DebugPostMessage(settleSs.str());

                    printf("[PADDBG] %s\n", settleSs.str().c_str());
                    fflush(stdout);
#endif
                }
            }

            // Decrement settle counter and skip state events until it reaches 0.
            if (p < 4 && s_arrivalSettleCount[p] > 0) {
                s_arrivalSettleCount[p]--;
                controllerIndex++;
                continue;
            }

            int liResult = LiSendMultiControllerEvent(controllerIndex, activeGamepadMask,
                buttonFlags,
                leftTrigger,
                rightTrigger,
                leftStickX,
                leftStickY,
                rightStickX,
                rightStickY);
#if GAMEPAD_DEBUG
            if (hasMappedInput || liResult != 0) {
                std::ostringstream sendSs;
                sendSs << "LiSendMultiControllerEvent"
                       << " slot=" << p
                       << " controllerIndex=" << controllerIndex
                       << " mask=0x" << std::hex << static_cast<unsigned int>(activeGamepadMask)
                       << " flags=0x" << static_cast<unsigned int>(buttonFlags) << std::dec
                       << " LT=" << static_cast<unsigned int>(leftTrigger)
                       << " RT=" << static_cast<unsigned int>(rightTrigger)
                       << " LS=(" << leftStickX << "," << leftStickY << ")"
                       << " RS=(" << rightStickX << "," << rightStickY << ")"
                       << " phase=state"
                       << " result=" << liResult
                       << " stream=" << (s_streamActive ? 1 : 0)
                       << " input=" << (s_gamepadInputEnabled ? 1 : 0);
                DebugPostMessage(sendSs.str());

                printf("[PADDBG] %s\n", sendSs.str().c_str());
                fflush(stdout);
            }
#endif

            if (p < 4) {
                s_inputActiveLast[p] = hasMappedInput;
            }
        }

        controllerIndex++;
    }
}

void MoonlightInstance::ClControllerRumble(unsigned short controllerNumber,
    unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    const float weakMagnitude = static_cast<float>(highFreqMotor) / static_cast<float>(UINT16_MAX);
    const float strongMagnitude = static_cast<float>(lowFreqMotor) / static_cast<float>(UINT16_MAX);

    std::ostringstream ss;
    ss << controllerNumber << "," << weakMagnitude << "," << strongMagnitude;

    pp::Var response(std::string("controllerRumble: ") + ss.str());
    g_Instance->PostMessage(response);
}
