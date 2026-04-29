# NavaMesh — Gatekeeper Node

## Why This Exists

For our NavaMesh project we need to keep hardware as cost-effective and low-powered as possible given the resource constraints in the Navajo region. Soil sensor nodes send telemetry over Meshtastic to an MQTT ingestor, which feeds InfluxDB and our Azure cloud setup.

The Gatekeeper node is the field access bridge — it lets a farmer or operator remotely trigger a Wi-Fi HaLow connection from anywhere on the mesh, without keeping a high-draw radio on 24/7.

---

## How It Works

A WisBlock RAK4631 runs continuously on ~5–10mA, listening on Meshtastic Channel 2. The Heltec HaLow Dongle stays completely unpowered until a trigger command arrives.

```
Remote node sends "Power On" on Channel 2
        ↓
WisBlock GatekeeperModule receives message
        ↓
IO1 pulled HIGH → Solar Manager EN pin HIGH → 5V rail enabled
        ↓
Heltec boots via USB-C pigtail, begins HaLow bridge
        ↓
"Power Off" received  OR  5-minute watchdog fires
        ↓
IO1 pulled LOW → 5V rail cut → Heltec off
```

<img width="906" height="571" alt="gatekeeperdiagram" src="https://github.com/user-attachments/assets/1145fc75-613f-4423-aa74-637ff27ae624" />

The 1.9W solar panel (~380mA in full sun) easily covers the WisBlock idle draw. Heltec on-time is kept brief to protect the energy balance during cloudy periods.

---

## Hardware

| Component | Part | Role |
|---|---|---|
| Controller | WisBlock RAK4631 (nRF52840 + SX1262) | Always-on listener, EN pin controller |
| Bridge | Heltec WiFi HaLow Dongle V2 | HaLow bridge, ~800mA peak |
| Power Manager | DFRobot Solar Power Manager 5V V1.1 | Solar MPPT, LiPo management, switched 5V rail |
| Enclosure | RAKBox-UO150x100x45-Solar | Weatherproof with integrated 1.9W panel |

---

## Wiring

### Solar Manager Blue Header (3-pin)

| Pin position | Label | Connect to |
|---|---|---|
| Top | GND | WisBlock GND |
| Middle | EN | WisBlock IO1 |
| Bottom | 5V | Heltec USB-C pigtail — red (VBUS) |

> **Remove the blue jumper** from the EN header before wiring IO1. With the jumper installed the 5V rail is always-on regardless of IO1 state.

> **Shared ground is mandatory.** Without the GND wire between the Solar Manager and WisBlock, IO1 has no reference and EN control does not work.

### Heltec USB-C Pigtail

Use only **red (VBUS)** and **black (GND)**. Tape off remaining wires.

| Wire | Connect to |
|---|---|
| Red (VBUS) | Solar Manager blue header — 5V (bottom) |
| Black (GND) | Solar Manager blue header — GND (top) |

### Full Wiring Summary

```
Solar Panel   ──→  Solar Manager  SOLAR IN  (screw terminal)
LiPo Battery  ──→  Solar Manager  BAT IN    (screw terminal or JST)

Solar Manager  USB-A out   ──→  WisBlock  USB power in    (always-on)
Solar Manager  EN  (mid)   ──→  WisBlock  IO1             (control signal)
Solar Manager  GND (top)   ──→  WisBlock  GND             (shared ground)
Solar Manager  5V  (bot)   ──→  Heltec    USB-C red wire  (switched power)
Solar Manager  GND (top)   ──→  Heltec    USB-C black wire
```

---

## Firmware

### GatekeeperModule

Located in `src/modules/GatekeeperModule.h` and `GatekeeperModule.cpp`, registered in `src/modules/Modules.cpp`.

**Tunable constants at the top of `GatekeeperModule.h`:**

```cpp
#define GATEKEEPER_CHANNEL   2                        // Meshtastic channel index (0-based)
#define HELTEC_MAX_ON_MS     (5UL * 60UL * 1000UL)   // Auto-shutoff — 5 minutes default
#define CMD_POWER_ON         "Power On"
#define CMD_POWER_OFF        "Power Off"
```

Adjust `HELTEC_MAX_ON_MS` based on mission length and available solar.

**Boot behaviour:** IO1 is forced LOW in the constructor before anything else runs — the Heltec is always off at startup, even after an unexpected WisBlock reset mid-session.

**Watchdog:** If "Power Off" is never received, the module automatically cuts power after `HELTEC_MAX_ON_MS` to prevent a net-negative energy day.

### Build and Flash

```powershell
# Always specify the rak4631 environment
# Running plain "pio run" also builds tbeam, which lacks RAK BSP pin definitions
pio run -e rak4631
pio run -e rak4631 -t upload
```

### Set LoRa Region After First Flash

```powershell
python -m meshtastic --port COM5 --set lora.region US
```

### Confirm the Module Loaded

Open the serial monitor before or immediately after reset:

```powershell
pio device monitor -p COM5 -b 115200
```

Press the reset button on the WisBlock. Look for this in the first few seconds of boot:

```
GatekeeperModule: initialized, EN pin LOW (Heltec off)
```

If you don't see it, check that `Modules.cpp` contains both the `#include` and the `new GatekeeperModule()` instantiation:

```powershell
Select-String "GatekeeperModule" src/modules/Modules.cpp
```

---

## Sending Commands

From any Meshtastic node on the same mesh, send a plain text message **on Channel 2**:

| Message | Effect |
|---|---|
| `Power On` | Enables 5V rail — Heltec boots |
| `Power Off` | Cuts 5V rail immediately |

Commands on any other channel are silently ignored. A repeat `Power On` while the Heltec is already running resets the auto-shutoff timer.

---

## Troubleshooting

**Heltec always on — doesn't respond to commands**
- Confirm the blue jumper is fully removed from the EN header
- Confirm IO1 DuPont is on the EN pin (middle), not the BAT pin (bottom)
- Confirm GND wire is connected between Solar Manager and WisBlock

**EN pin reads unexpected voltage**
- Always measure with black probe on the Solar Manager GND pin (top of blue header)
- Floating readings mean the shared ground wire is missing or not making contact

**GatekeeperModule init message missing from serial**
- The message fires early in boot — connect the monitor first, then press reset
- If still missing, run `Select-String "GatekeeperModule" src/modules/Modules.cpp` to confirm registration

**WB_IO1 not declared — build error**
- The Adafruit nRF52 BSP for RAK4631 doesn't define `WB_IO1` by name
- The `#ifndef WB_IO1 / #define WB_IO1 17` block in `GatekeeperModule.h` handles this — confirm it's present

---

## Branch Reference

| Branch | Hardware | Purpose |
|---|---|---|
| `develop` | RAK4631 | Soil sensor node firmware |
| `backhaul` | RAK4631 + SparkFun switch + Pi Zero 2W | Earlier field access design (Pi-based) |
| `gatekeeper` | RAK4631 + DFRobot Solar Manager + Heltec HaLow | Current field access design |

```powershell
git branch              # see current branch
git checkout gatekeeper # this branch
```
