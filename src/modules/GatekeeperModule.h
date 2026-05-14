#pragma once

// GatekeeperModule.h
// Controls the DFRobot Solar Manager EN pin to power the Heltec WiFi HaLow
// Dongle on and off. Single-pin HIGH/LOW pattern — simpler than the old
// SparkFun BTN-pulse approach in PowerTriggerModule.

#include "mesh/SinglePortModule.h"   // lives at src/mesh/SinglePortModule.h
#include "concurrency/OSThread.h"    // for the on-time watchdog timer

// ── Pin & Channel Config ──────────────────────────────────────────────────────

// WB_IO1 = RAK4631 P0.17 = Arduino pin 17.
// WB_IO2 = P1.02 = pin 34 (used by AnalogSoilSensor for sensor power gating).
// The Adafruit nRF52 BSP variant for RAK4631 doesn't always define WB_IO1 by
// name, so we define it here if needed. #ifndef means if a future BSP version
// adds it officially, this line is skipped with no conflict.
#ifndef WB_IO1
#define WB_IO1 17
#endif

// IO1 on the WisBlock base board → EN pin on DFRobot Solar Manager 5V.
// HIGH = 5V rail enabled (Heltec on).  LOW = 5V rail off (Heltec off).
#define GATEKEEPER_EN_PIN WB_IO1

// Only respond to commands arriving on this channel index (0-based).
// Channel 2 per Jacob's spec.
#define GATEKEEPER_CHANNEL 2

// Maximum time (ms) to leave the Heltec powered before auto-shutoff.
// Keeps energy balance positive during low-sun periods.
#define HELTEC_MAX_ON_MS (10UL * 60UL * 1000UL)   // 10 minutes default

// ── Command Strings ───────────────────────────────────────────────────────────

#define CMD_POWER_ON  "Power On"
#define CMD_POWER_OFF "Power Off"

// ── Module Class ──────────────────────────────────────────────────────────────

class GatekeeperModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    GatekeeperModule();

  protected:
    // Called by Meshtastic for every received TEXT_MESSAGE packet.
    // ProcessMessage::CONTINUE = let other modules see it too.
    // ProcessMessage::STOP     = consume and swallow the packet.
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // OSThread callback — runs the on-time watchdog on a fixed interval.
    virtual int32_t runOnce() override;

  private:
    void activateHeltec();
    void deactivateHeltec();

    bool     _heltecOn      = false;
    uint32_t _activatedAtMs = 0;
};

extern GatekeeperModule *gatekeeperModule;