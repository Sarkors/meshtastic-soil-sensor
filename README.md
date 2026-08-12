# Meshtastic Soil Moisture Sensor Firmware

Custom Meshtastic firmware for direct analog soil moisture sensing on LoRa mesh nodes. Built for agricultural field deployments — no intermediate microcontroller required. Sensor readings are transmitted as structured telemetry over the mesh and relayed as human-readable text messages to a dedicated channel.

All firmware patches are already applied in this repo. Clone, build, flash, and configure.

---

## What This Does

* Reads an analog soil moisture sensor (HD-38) directly on the radio node
* Transmits the **raw averaged ADC count** (mean of 5 samples) as a `navamesh.SoilReading`
  protobuf on PortNum 256 (PRIVATE_APP) — **the node performs no calibration**
* The Raspberry Pi owns the raw-ADC → moisture-% mapping, so calibration can be retuned
  without reflashing deployed nodes
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

### 3. Build

**RAK4631:**

```
pio run -e rak4631
```

**Heltec v3:**

```
pio run -e heltec-v3
```

> **Calibration is no longer a build-time step.** The node ships the raw ADC; set
> `SOIL_ADC_DRY` / `SOIL_ADC_WET` in the Raspberry Pi's `.env` instead.
> See [CONFIGURATION.md](CONFIGURATION.md).

### 4. Flash

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

**Note on environment telemetry in the app:** The Environment Metrics section stays greyed out **permanently**. This is expected, not a fault — the node deliberately writes no EnvironmentMetrics fields now that the raw ADC travels in its own `navamesh.SoilReading` protobuf. The node still emits an (empty) environment telemetry packet because its local loopback is what triggers the relay.

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
[EnvironmentTelemetry] AnalogSoilSensor: raw ADC = 2871
[EnvironmentTelemetry] Send packet to mesh
TelemetryRelay: raw_adc=2871 bat=72% 3850mV up=12120s -> ch 1 (navamesh) port 256
TelemetryRelay: sending 'ADC: 2871 | Bat: 72% (3.85V) | Up: 3h 22m' to channel 1
```

Note there is **no moisture percentage in the serial log** — that is expected. The node
no longer computes one.

On any phone connected to the mesh with the navamesh channel configured, you should see relay messages arriving at your set interval.

---

## Wire Format

Each reading cycle broadcasts on the `navamesh` channel:

**1. `navamesh.SoilReading` on PortNum 256 (PRIVATE_APP) — authoritative.**
Defined in `proto/navamesh/navamesh.proto`; nanopb classes are generated into
`src/mesh/generated/navamesh/` by `./bin/regen-navamesh-proto.sh`.

| Field | # | Type |
|-------|---|------|
| `raw_adc` | 1 | `uint32` |
| `battery_percent` | 2 | `uint32` |
| `battery_mv` | 3 | `uint32` |
| `uptime_seconds` | 4 | `uint32` |

This is a **private** message. Meshtastic's own `protobufs/` submodule is never modified,
so there is no risk of colliding with a field number upstream assigns later.

Two decoder notes: proto3 omits zero-valued scalars from the wire, so absent fields must
default to 0; and the payload is encrypted with the `navamesh` channel PSK, so the
receiving gateway must be provisioned on that channel.

**2. A debug text message** (see below). Useful on a phone, but the protobuf is the
measurement of record.

## Relay Message Format

```
ADC: 2871 | Bat: 72% (3.85V) | Up: 3h 22m     (on battery)
ADC: 2871 | Bat: USB | Up: 3h 22m              (USB powered)
```

---

## Project Architecture

```
Soil Sensor (HD-38)
      │ analog voltage
      ▼
RAK4631 / Heltec v3          ← performs NO calibration
  ├─ ADC read ×5 → average → RAW count (never constrained, never mapped)
  ├─ navamesh.SoilReading protobuf → LoRa mesh (navamesh ch, PortNum 256)
  ├─ Text relay 'ADC: XXXX | Bat | Up' → LoRa mesh (debug)
  └─ Position broadcast → LoRa mesh (every 15 min)
      │
      ▼
Router Node (WisBlock)
  └─ Rebroadcasts packets across mesh
      │
      ▼
Raspberry Pi gateway         ← sole owner of calibration
  ├─ decodes SoilReading, stores raw ADC verbatim
  ├─ applies SOIL_ADC_DRY / SOIL_ADC_WET curve → soil_percent
  └─ writes BOTH soil_raw and soil_percent to Influx / Postgres / cloud
      │
      ▼
Any phone on navamesh channel
  └─ Receives debug relay messages + sees node on map
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

## Raw ADC Reference (HD-38, RAK4631)

Input for **Pi-side** calibration — these are no longer compiled into the firmware.

| Condition | Raw ADC |
|-----------|---------|
| Dry air | 4095 |
| Dry soil | 3120 |
| Moist soil | ~2879 |
| Wet/muddy | 1567 |
| Open water | 849 |

**ADC transfer function.** The RAK4631 samples at 12-bit resolution
(`analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS)` = 12) against an `AR_INTERNAL_3_0`
reference (`variants/nrf52840/rak4631/variant.h`), so:

```
volts ≈ raw_adc × 3.0 / 4096
```

The reading **saturates at 4095 for any input ≥ 3.0 V**, which is why dry air reads 4095.
Anyone fitting a new curve needs this.

Set `SOIL_ADC_DRY` / `SOIL_ADC_WET` in the Raspberry Pi's `.env` and restart the bridge —
no reflash required.

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
