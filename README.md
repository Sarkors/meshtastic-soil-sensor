# Meshtastic Soil Moisture Sensor Firmware

Custom Meshtastic firmware for direct analog soil moisture sensing on LoRa mesh nodes. Built for agricultural field deployments — no intermediate microcontroller required. Sensor readings are transmitted as structured telemetry over the mesh and relayed as human-readable text messages to a dedicated channel.

All firmware patches are already applied in this repo. Clone, build, flash, and configure.

---

## What This Does

* Reads an analog soil moisture sensor (HD-38) directly on the radio node
* Transmits soil moisture percentage as Meshtastic environment telemetry
* Automatically relays readings as a text message to a configurable channel (default: `navamesh`)
* Includes battery percentage, voltage, and uptime in relay messages
* Broadcasts node position to the mesh for GIS mapping
* Works standalone with no phone or laptop required after initial configuration

---

## Firmware Defaults (Baked In)

These settings are applied automatically on every fresh flash — no CLI commands needed:

| Setting | Value | Why |
|---------|-------|-----|
| Device Role | SENSOR | Standalone operation, no phone required |
| Environment Telemetry | Enabled | Soil moisture readings active |
| Telemetry Interval | 10800s (3 hours) | Balance between data freshness and battery life |
| Position Broadcast | 900s (15 minutes) | Keeps node visible on the mesh map |
| Position Precision | 32 (full accuracy) | Exact GPS coordinates, no rounding |
| GPS Mode | NOT_PRESENT | No hardware GPS — position set from phone via Fixed Position |

To temporarily override any setting for testing (e.g. faster telemetry), use the CLI:

```
python -m meshtastic --port COMX --set telemetry.environment_update_interval 60
```

Settings revert to firmware defaults on factory reset.

---

## Supported Hardware

| Hardware | Status |
|----------|--------|
| RAK19007 + RAK4631 (nRF52840) | ✅ Confirmed working |
| Heltec WiFi LoRa 32 v3 (ESP32-S3) | ✅ Confirmed working |

### Required Components

* RAK19007 WisBlock Base Board + RAK4631 Core Module, **or** Heltec WiFi LoRa 32 v3
* HD-38 soil moisture sensor (VCC, GND, D0, A0)
* 3.3V power supply or solar + battery management system

---

## Wiring

### RAK4631 (RAK19007 Base Board)

| HD-38 Pin | RAK19007 Pin |
|-----------|--------------|
| VCC | IO2/3V3 |
| GND | GND |
| A0 | AIN1 (P0.31) |
| D0 | Not connected |

### Heltec v3

| HD-38 Pin | Heltec Pin |
|-----------|------------|
| VCC | 3.3V |
| GND | GND |
| A0 | GPIO 7 |
| D0 | Not connected |

> **Note:** Solder the A0 connection directly. Friction-fit header pins are unreliable for ADC readings.

---

## Setup

### 1. Clone This Repo

```
git clone https://github.com/Sarkors/meshtastic-soil-sensor.git
cd meshtastic-soil-sensor
git submodule update --init --recursive
```

### 2. Install PlatformIO

Install [VS Code](https://code.visualstudio.com/) and the PlatformIO extension, or install the CLI:

```
pip install platformio
```

### 3. Calibrate

Before building, update the calibration values in `src/modules/Telemetry/Sensor/AnalogSoilSensor.h` to match your sensor and soil conditions. See [CONFIGURATION.md](CONFIGURATION.md) for instructions.

### 4. Build

**RAK4631:**

```
pio run -e rak4631
```

**Heltec v3:**

```
pio run -e heltec-v3
```

### 5. Flash

**RAK4631:**

```
pio run -e rak4631 --target upload --upload-port COMX
```

**Heltec v3:**

```
pio run -e heltec-v3 --target upload --upload-port COMX
```

Replace `COMX` with your device's COM port. On Linux/Mac use `/dev/ttyUSBx` or `/dev/tty.usbserialx`.

---

## Post-Flash Configuration

The firmware comes preconfigured with all necessary defaults. Only two manual steps are needed per node:

### Important: Factory Reset After Flashing

If the node previously had Meshtastic firmware on it, the old saved config will override the new firmware defaults. **Always factory reset after flashing** to ensure the new defaults take effect:

```
python -m meshtastic --port COMX --factory-reset
```

The node will reboot with all firmware defaults applied. Do this **before** adding channels or setting position.

**How to verify defaults applied correctly:** Connect via BLE in the Meshtastic app — if the node's role shows as SENSOR, the defaults are active. If it shows CLIENT or any other role, factory reset is needed.

**Note on environment telemetry in the app:** The Environment Metrics section will be greyed out in the app until the node sends its first telemetry reading after boot. This is normal — wait for the first telemetry cycle (connect via BLE to trigger one immediately) and the section will become active.

### Deployment Order Per Node

1. **Flash** the firmware
2. **Factory reset** via CLI
3. **Set LoRa region** (resets on factory reset): `python -m meshtastic --port COMX --set lora.region US`
4. **Add navamesh channel** via the Meshtastic app
5. **Set position** via Fixed Position toggle in the app

### 1. Add the Navamesh Channel

In the Meshtastic app on your phone:

1. Connect to the node via Bluetooth
2. Settings → Channels → Add Channel
3. Set Name: `navamesh`
4. Set PSK: *(obtain from your team lead — do not share publicly)*
5. Save

This must be done on every node that needs to send or receive soil readings.

### 2. Set the Node's GPS Position

Since these nodes don't have a physical GPS module, the position is set from your phone. GPS mode defaults to NOT_PRESENT in the firmware, so the Fixed Position option is available immediately.

1. Connect to the node via Bluetooth in the Meshtastic app
2. Go to Position Config
3. Toggle **Fixed Position** off, then back on
4. The app grabs your phone's current GPS and pushes it to the node
5. Save

The node will now broadcast that position to the mesh every 15 minutes. To update the position later (e.g. if the node is moved), just repeat step 4.

### Optional: Override Telemetry Interval for Testing

```
# Set to 60 seconds for quick testing
python -m meshtastic --port COMX --set telemetry.environment_update_interval 60

# Set back to 3 hours for deployment
python -m meshtastic --port COMX --set telemetry.environment_update_interval 10800
```

---

## Verifying It Works

Open the serial monitor:

```
pio device monitor --port COMX --baud 115200
```

**Important:** Disconnect your phone from the node before checking. Telemetry only relays to the navamesh channel when no phone is connected via BLE (when a phone is connected, telemetry goes directly to the phone instead of the mesh).

You should see:

```
[EnvironmentTelemetry] AnalogSoilSensor: raw ADC=XXXX, moisture=XX%
[EnvironmentTelemetry] Send packet to mesh
TelemetryRelay: sending 'Soil: XX% | Bat: 72% (3.85V) | Up: 3h 22m' to channel 1
```

On any phone connected to the mesh with the navamesh channel configured, you should see relay messages arriving at your set interval.

---

## Relay Message Format

```
Soil: 45% | Bat: 72% (3.85V) | Up: 3h 22m     (on battery)
Soil: 45% | Bat: USB | Up: 3h 22m              (USB powered)
```

---

## Project Architecture

```
Soil Sensor (HD-38)
      │ analog voltage
      ▼
RAK4631 / Heltec v3
  ├─ ADC reading → moisture %
  ├─ Environment telemetry packet → LoRa mesh
  ├─ Text relay 'Soil: XX% | Bat | Up' → LoRa mesh (navamesh channel)
  └─ Position broadcast → LoRa mesh (every 15 min)
      │
      ▼
Router Node (WisBlock)
  └─ Rebroadcasts packets across mesh
      │
      ▼
Any phone on navamesh channel
  └─ Receives relay messages + sees node on map
```

---

## Branch Reference

| Branch | Purpose | Hardware |
|--------|---------|----------|
| `develop` | Soil sensor nodes | RAK4631 or Heltec v3 with HD-38 sensor |
| `backhaul` | Backhaul power control | RAK4631 + SparkFun + Pi Zero 2W |

Always confirm you are on the correct branch before building:

```
git branch
git checkout develop    # for sensor nodes
git checkout backhaul   # for backhaul nodes
```

---

## Calibration Reference (HD-38, RAK4631)

| Condition | Raw ADC |
|-----------|---------|
| Dry air | 4095 |
| Dry soil | 3040 |
| Moist soil | ~2879 |
| Wet/muddy | 1567 |
| Open water | 849 |

See [CONFIGURATION.md](CONFIGURATION.md) for how to update these values.

# Video Setup Walkthroughs 
## Node Flashing and Meshtastic Configuration 
https://youtu.be/9rfFNOcalU4
## What to do with received Soil Sensor Nodes
https://youtu.be/JMJ5QePX9k8


---

## Key Lessons Learned

* **Solder your ADC connections** — friction-fit pins give unreliable readings
* **Disconnect phone to test mesh relay** — telemetry goes to phone instead of mesh when BLE is connected
* **`min_default_telemetry_interval_secs` in `Default.h`** — enforces a minimum interval floor (set to 60s)
* **Channel precision controls GPS accuracy** — default LongFast precision (13) rounds to ~1-2km grid cells; set to 32 for exact coordinates
* **`channels.getByName()` returns a channel object not an index** — use manual loop through `channelFile.channels[]`

---

## License

Based on [Meshtastic firmware](https://github.com/meshtastic/firmware) (GPL-3.0). Custom additions in this repo are released under GPL-3.0.
