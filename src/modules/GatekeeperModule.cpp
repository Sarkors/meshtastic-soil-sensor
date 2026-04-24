// GatekeeperModule.cpp
// WisBlock RAK4631 "Gatekeeper" — gates power to the Heltec WiFi HaLow Dongle
// via the DFRobot Solar Manager EN pin. Listen on Channel 2, toggle IO1.

#include "GatekeeperModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "main.h"
#include <Arduino.h>

// ── Singleton ─────────────────────────────────────────────────────────────────
GatekeeperModule *gatekeeperModule;

// ── Constructor ───────────────────────────────────────────────────────────────

GatekeeperModule::GatekeeperModule()
    : SinglePortModule("gatekeeper", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("GatekeeperModule")
{
    // Force EN LOW at boot so the Heltec is definitely off even if the
    // WisBlock reboots mid-session. Without this, the Solar Manager's 5V rail
    // could stay energised if IO1 happens to float HIGH during reset.
    pinMode(GATEKEEPER_EN_PIN, OUTPUT);
    digitalWrite(GATEKEEPER_EN_PIN, LOW);

    LOG_INFO("GatekeeperModule: initialized, EN pin LOW (Heltec off)\n");
}

// ── Packet Handler ────────────────────────────────────────────────────────────

ProcessMessage GatekeeperModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Gate on Channel 2 only — ignore everything else.
    // mp.channel is 0-based: Primary=0, Secondary=1, Channel 2 = index 2.
    if (mp.channel != GATEKEEPER_CHANNEL) {
        return ProcessMessage::CONTINUE;
    }

    // Ignore packets we sent ourselves (loop-back protection).
    if (mp.from == nodeDB->getNodeNum()) {
        return ProcessMessage::CONTINUE;
    }

    // Decode the text payload from the raw byte buffer.
    // We avoid Arduino String(const char*, length) here because the nRF52
    // WString implementation doesn't have that constructor. Instead we
    // null-terminate into a stack buffer and use strcmp directly.
    const auto &d = mp.decoded;
    if (d.payload.size == 0) {
        return ProcessMessage::CONTINUE;
    }

    // Cap at 31 chars to stay safe on the stack; no valid command is longer.
    if (d.payload.size > 31) {
        return ProcessMessage::CONTINUE;
    }

    char buf[32];
    memcpy(buf, d.payload.bytes, d.payload.size);
    buf[d.payload.size] = '\0';

    // Trim a trailing newline or carriage return if present.
    size_t len = d.payload.size;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }

    LOG_INFO("GatekeeperModule: received '%s' on channel %d\n", buf, mp.channel);

    if (strcmp(buf, CMD_POWER_ON) == 0) {
        activateHeltec();
    } else if (strcmp(buf, CMD_POWER_OFF) == 0) {
        deactivateHeltec();
    } else {
        LOG_INFO("GatekeeperModule: unknown command, ignoring\n");
    }

    // CONTINUE — let the message still appear in the mesh/app normally.
    // Change to STOP if you want command packets silently swallowed.
    return ProcessMessage::CONTINUE;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void GatekeeperModule::activateHeltec()
{
    if (_heltecOn) {
        // Already on — just reset the timer so a repeat "Power On" extends
        // the on-time window rather than being silently ignored.
        LOG_INFO("GatekeeperModule: Heltec already on, timer reset\n");
        _activatedAtMs = millis();
        return;
    }

    // Pull EN HIGH → Solar Manager enables 5V regulated output →
    // Heltec receives power via the USB-C pigtail on the 5V header.
    digitalWrite(GATEKEEPER_EN_PIN, HIGH);
    _heltecOn      = true;
    _activatedAtMs = millis();

    LOG_INFO("GatekeeperModule: EN HIGH — Heltec ON (auto-off in %lu s)\n",
             (unsigned long)(HELTEC_MAX_ON_MS / 1000));
}

void GatekeeperModule::deactivateHeltec()
{
    if (!_heltecOn) {
        LOG_INFO("GatekeeperModule: Heltec already off\n");
        return;
    }

    // Pull EN LOW → Solar Manager cuts 5V rail → Heltec loses power.
    // Hard power cut is intentional — the HaLow dongle has no OS to corrupt.
    digitalWrite(GATEKEEPER_EN_PIN, LOW);
    _heltecOn = false;

    uint32_t onTimeSec = (millis() - _activatedAtMs) / 1000;
    LOG_INFO("GatekeeperModule: EN LOW — Heltec OFF (was on %lu s)\n",
             (unsigned long)onTimeSec);
}

// ── Watchdog Timer ────────────────────────────────────────────────────────────

int32_t GatekeeperModule::runOnce()
{
    // OSThread calls runOnce() repeatedly; return value = ms until next call.
    // We check every 30 s — tight enough to not overshoot the deadline badly,
    // light enough to keep the always-on current draw minimal.

    if (_heltecOn) {
        uint32_t elapsed = millis() - _activatedAtMs;

        if (elapsed >= HELTEC_MAX_ON_MS) {
            LOG_WARN("GatekeeperModule: max on-time reached, forcing Heltec off\n");
            deactivateHeltec();
        } else {
            uint32_t remainingSec = (HELTEC_MAX_ON_MS - elapsed) / 1000;
            LOG_INFO("GatekeeperModule: Heltec on, %lu s until auto-off\n",
                     (unsigned long)remainingSec);
        }
    }

    return 30 * 1000; // check again in 30 seconds
}