# Meshtastic Soil Moisture Sensor Firmware

Custom Meshtastic firmware for direct analog soil moisture sensing on LoRa mesh nodes. Built for agricultural field deployments — no intermediate microcontroller required. Sensor readings are transmitted as structured telemetry over the mesh and relayed as human-readable text messages to a dedicated channel.

All firmware patches are already applied in this repo. Clone, build, flash, and configure.

---

## What This Does

- Reads an analog soil moisture sensor (HD-38) directly on the radio node
- Transmits soil moisture percentage as Meshtastic environment telemetry
- Automatically relays readings as a text message to a configurable channel (default: `navamesh`)
- Works standalone with no phone or laptop required after initial configuration

---

## Supported Hardware

| Hardware | Status |
|----------|--------|
| RAK19007 + RAK4631 (nRF52840) | ✅ Confirmed working |
| Heltec WiFi LoRa 32 v3 (ESP32-S3) | ✅ Confirmed working |

### Required Components
- RAK19007 WisBlock Base Board + RAK4631 Core Module, **or** Heltec WiFi LoRa 32 v3
- HD-38 soil moisture sensor (VCC, GND, D0, A0)
- 3.3V power supply or solar + battery management system

---

## Wiring

### RAK4631 (RAK19007 Base Board)
| HD-38 Pin | RAK19007 Pin |
|-----------|-------------|
| VCC | 3V3 |
| GND | GND |
| A0 | AIN1 (P0.31) |
| D0 | Not connected |

### Heltec v3
| HD-38 Pin | Heltec Pin |
|-----------|-----------|
| VCC | 3.3V |
| GND | GND |
| A0 | GPIO 7 |
| D0 | Not connected |

> **Note:** Solder the A0 connection directly. Friction-fit header pins are unreliable for ADC readings.

---

## Setup

### 1. Clone This Repo

```bash
git clone https://github.com/Sarkors/meshtastic-soil-sensor.git
cd meshtastic-soil-sensor
git submodule update --init --recursive
```

### 2. Install PlatformIO

Install [VS Code](https://code.visualstudio.com/) and the PlatformIO extension, or install the CLI:

```bash
pip install platformio
```

### 3. Calibrate

Before building, update the calibration values in `src/modules/Telemetry/Sensor/AnalogSoilSensor.h` to match your sensor and soil conditions. See [CONFIGURATION.md](CONFIGURATION.md) for instructions.

### 4. Build

**RAK4631:**
```bash
pio run -e rak4631
```

**Heltec v3:**
```bash
pio run -e heltec-v3
```

### 5. Flash

**RAK4631:**
```bash
pio run -e rak4631 --target upload --upload-port COMX
```

**Heltec v3:**
```bash
pio run -e heltec-v3 --target upload --upload-port COMX
```

Replace `COMX` with your device's COM port. On Linux/Mac use `/dev/ttyUSBx` or `/dev/tty.usbserialx`.

---

## Post-Flash Configuration

Run these commands after flashing. Close the serial monitor first — it blocks the COM port.

```bash
# Set device role to SENSOR (required for standalone operation without a phone)
python -m meshtastic --port COMX --set device.role SENSOR

# Set telemetry interval in seconds — 10800 = 3 hours
python -m meshtastic --port COMX --set telemetry.environment_update_interval 10800

# Enable environment measurement
python -m meshtastic --port COMX --set telemetry.environment_measurement_enabled true

# Verify settings stuck
python -m meshtastic --port COMX --get telemetry
```

### Add the Navamesh Channel

In the Meshtastic app on your phone:
1. Settings → Channels → Add Channel
2. Set Name: `navamesh`
3. Set PSK: *(obtain from your team lead — do not share publicly)*
4. Save

This must be done on every node that needs to send or receive soil readings.

---

## Verifying It Works

Open the serial monitor:
```bash
pio device monitor --port COMX --baud 115200
```

You should see:
```
[EnvironmentTelemetry] AnalogSoilSensor: raw ADC=XXXX, moisture=XX%
[EnvironmentTelemetry] TelemetryRelay: sending 'Soil: XX%' to channel 1
```

On any phone connected to the mesh with the navamesh channel configured, you should see `Soil: XX%` messages arriving at your set interval.

---

## Project Architecture

```
Soil Sensor (HD-38)
      │ analog voltage
      ▼
RAK4631 / Heltec v3
  ├─ ADC reading → moisture %
  ├─ Environment telemetry packet → LoRa mesh (primary channel)
  └─ Text message 'Soil: XX%' → LoRa mesh (navamesh channel)
      │
      ▼
Router Node (WisBlock)
  └─ Rebroadcasts packets across mesh
      │
      ▼
Any phone on navamesh channel
  └─ Receives 'Soil: XX%' messages
```

---

## License

Based on [Meshtastic firmware](https://github.com/meshtastic/firmware) (GPL-3.0). Custom additions in this repo are released under GPL-3.0.
