#include "GPIOPinTestModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"

// ── Edit these only ────────────────────────────────────────────────────────

#define PIN_A               17      // RAK19007: WB_IO1 → SparkFun BTN (active-low)
#define PIN_A_MESSAGE       "PI_ON"

#define PIN_B               34      // RAK19007: WB_IO2 → SparkFun OFF (active-high)
#define PIN_B_MESSAGE       "PI_OFF"

#define BTN_PULSE_MS        100     // how long to pull BTN low (ms)

// ──────────────────────────────────────────────────────────────────────────

GPIOPinTestModule *gpioPinTestModule;

static uint32_t lastHandledId = 0;

GPIOPinTestModule::GPIOPinTestModule()
    : SinglePortModule("GPIOPinTest", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
    pinMode(PIN_A, OUTPUT);
    pinMode(PIN_B, OUTPUT);
    digitalWrite(PIN_A, HIGH);  // BTN idle = HIGH (not pressed)
    digitalWrite(PIN_B, LOW);   // OFF idle = LOW (not cutting power)

    LOG_INFO("GPIOPinTest: ready");
    LOG_INFO("GPIOPinTest: send '%s' to pulse GPIO %d (WB_IO1/BTN) LOW", PIN_A_MESSAGE, PIN_A);
    LOG_INFO("GPIOPinTest: send '%s' to drive GPIO %d (WB_IO2/OFF) HIGH", PIN_B_MESSAGE, PIN_B);
}

ProcessMessage GPIOPinTestModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.id == lastHandledId) {
        LOG_DEBUG("GPIOPinTest: dropping duplicate packet id=0x%08x", mp.id);
        return ProcessMessage::CONTINUE;
    }

    const size_t len = mp.decoded.payload.size;
    if (len == 0 || len >= meshtastic_Constants_DATA_PAYLOAD_LEN)
        return ProcessMessage::CONTINUE;

    char text[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    memcpy(text, mp.decoded.payload.bytes, len);
    text[len] = '\0';

    LOG_DEBUG("GPIOPinTest: received '%s' on channel %d from 0x%08x",
              text, mp.channel, mp.from);

    lastHandledId = mp.id;

    if (strcmp(text, PIN_A_MESSAGE) == 0) {
        LOG_INFO("GPIOPinTest: PI_ON — pulsing BTN (IO1) LOW for %dms", BTN_PULSE_MS);
        digitalWrite(PIN_B, LOW);       // ensure OFF is not asserted
        digitalWrite(PIN_A, LOW);       // pull BTN low — SparkFun sees button press
        delay(BTN_PULSE_MS);
        digitalWrite(PIN_A, HIGH);      // release BTN
        LOG_INFO("GPIOPinTest: BTN pulse complete");
    }
    else if (strcmp(text, PIN_B_MESSAGE) == 0) {
        LOG_INFO("GPIOPinTest: PI_OFF — asserting OFF (IO2) HIGH");
        digitalWrite(PIN_A, HIGH);      // ensure BTN is not pressed
        digitalWrite(PIN_B, HIGH);      // assert OFF — SparkFun cuts power
        LOG_INFO("GPIOPinTest: OFF asserted");
    }
    else {
        LOG_DEBUG("GPIOPinTest: no match for '%s'", text);
    }

    return ProcessMessage::CONTINUE;
}