# Configuration Guide

## Sensor Calibration (Raspberry Pi side)

**The node performs no calibration.** It reads the HD-38, averages 5 ADC samples, and
transmits that raw count untouched. There are deliberately no `ANALOG_SOIL_DRY` /
`ANALOG_SOIL_WET` constants in the firmware any more — calibration lives entirely on the
Pi so it can be retuned without reflashing deployed nodes.

### How to Calibrate

1. Open the serial monitor (or watch the Pi's bridge log) and note the raw ADC in
   **dry soil**, then in **wet/muddy soil**.
2. On the Raspberry Pi, set the two values in `.env`:

   ```
   SOIL_ADC_DRY=3120    # reads as 0%
   SOIL_ADC_WET=1567    # reads as 100%
   ```

3. Restart the bridge — **no rebuild, no reflash**:

   ```
   cd /home/pi/Navamesh && docker compose up -d --build bridge
   ```

The raw ADC is always stored verbatim as `soil_raw`; only the derived `soil_percent` is
clamped to 0–100. Because both are stored, historical raw data can be re-derived against
a new curve at any time.

The curve itself is `adc_to_percent()` in `src/navamesh/calibration.py` in the
`Navamesh-main` repo.

### Node-side hardware settings

These are the only sensor knobs left in the firmware, in
`src/modules/Telemetry/Sensor/AnalogSoilSensor.h`:

| Macro | RAK4631 | Purpose |
|-------|---------|---------|
| `ANALOG_SOIL_PIN` | 31 | AIN1 = P0.31 |
| `ANALOG_SOIL_3V3_EN` | 34 | must be HIGH to power the sensor |
| `ANALOG_SOIL_SETTLE_MS` | 100 | rail + sensor settle after power-on |
| `ANALOG_SOIL_DISCARD_SAMPLES` | 3 | throwaway reads to flush the SAADC |
| `ANALOG_SOIL_SAMPLES` | 5 | reads that get averaged |
| `ANALOG_SOIL_SAMPLE_GAP_MS` | 5 | delay between reads |

---

## Wire Format

The reading is sent as a private protobuf, **not** as Meshtastic environment telemetry.
Meshtastic's `protobufs/` submodule is deliberately left untouched, so there is no risk of
colliding with a field number upstream assigns later.

`proto/navamesh/navamesh.proto`:

```proto
syntax = "proto3";
package navamesh;

message SoilReading {
  uint32 raw_adc         = 1;
  uint32 battery_percent = 2;
  uint32 battery_mv      = 3;
  uint32 uptime_seconds  = 4;
}
```

Sent on **PortNum 256 (PRIVATE_APP)**, broadcast on the `navamesh` channel.

To regenerate the nanopb classes after editing the `.proto`:

```
./bin/regen-navamesh-proto.sh
```

This requires nanopb 0.4.9.1 in the firmware root as `nanopb-0.4.9/` (the same
prerequisite as the stock `bin/regen-protos.sh`; that directory is gitignored). The script
writes only `src/mesh/generated/navamesh/` and never touches `protobufs/` or
`src/mesh/generated/meshtastic/`.

Decoder notes: proto3 omits zero-valued scalars, so absent fields must default to 0; and
the payload is encrypted with the `navamesh` channel PSK, so the gateway must be
provisioned on that channel to read it.

---

## Firmware Defaults

These settings are baked into the firmware and apply on every fresh flash or factory reset. They can be overridden via CLI without reflashing.

### Defaults in `src/mesh/NodeDB.cpp`

| Setting | Default | Location |
|---------|---------|----------|
| Device Role | SENSOR | `installDefaultConfig()` |
| Position Broadcast | 900s (15 min) | `initConfigIntervals()` |
| Environment Telemetry Enabled | true | `installRoleDefaults()` SENSOR block |
| Telemetry Interval | 10800s (3 hours) | `installRoleDefaults()` SENSOR block |
| GPS Mode | NOT_PRESENT | `installRoleDefaults()` SENSOR block |
| Channel Position Precision | 32 (full) | `installDefaultChannels()` |

### Defaults in `src/mesh/Default.h`

| Setting | Default | Notes |
|---------|---------|-------|
| Min Telemetry Interval | 60s | Cannot set interval below this via CLI |
| Min Broadcast Interval | 60s | Cannot set position broadcast below this |
| Min Node Info Broadcast | 60s | Cannot set node info broadcast below this |

---

## Changing Settings via CLI

Override any firmware default without reflashing. Settings persist across reboots and revert to firmware defaults on factory reset.

### Telemetry Interval

```
# Set to 60 seconds for testing
python -m meshtastic --port COMX --set telemetry.environment_update_interval 60

# Set to 3 hours for deployment
python -m meshtastic --port COMX --set telemetry.environment_update_interval 10800

# Verify
python -m meshtastic --port COMX --get telemetry
```

### Position Broadcast Interval

```
# Set to 5 minutes
python -m meshtastic --port COMX --set position.position_broadcast_secs 300

# Set to 15 minutes (default)
python -m meshtastic --port COMX --set position.position_broadcast_secs 900
```

### Channel Position Precision

```
# Full precision (recommended for field nodes)
python -m meshtastic --port COMX --ch-set module_settings.position_precision 32 --ch-index 0

# Default LongFast precision (coarse, ~1-2km grid)
python -m meshtastic --port COMX --ch-set module_settings.position_precision 13 --ch-index 0
```

---

## Navamesh Channel

The relay messages are sent to a private channel called `navamesh`. Every node that needs to send or receive soil readings must have this channel configured.

**Channel Name:** `navamesh`
**PSK:** Obtain from your team lead — do not commit to the repo.

### Adding via Meshtastic App

1. Connect to the node via Bluetooth
2. Settings → Channels → Add Channel
3. Name: `navamesh`
4. PSK: *(paste the key your team lead provides)*
5. Save

### Changing the Channel Name

The channel name is defined in `src/modules/TelemetryRelay.cpp`:

```cpp
#define RELAY_CHANNEL_NAME "navamesh"
```

Change `"navamesh"` to your preferred channel name, rebuild, and reflash.

> **Note:** if the named channel is not found, the relay falls back to the primary channel
> (index 0) and logs a warning. Because the reading is encrypted with the channel PSK,
> that fallback means readings go out on a channel the Pi may not be listening on — check
> for `channel 'navamesh' not found` in the serial log if data stops arriving.

---

## Setting Node Position

GPS mode defaults to NOT_PRESENT in the firmware, so the Fixed Position option is available immediately after flashing and factory reset.

1. Connect to the node via Bluetooth in the Meshtastic app
2. Go to **Position Config**
3. Toggle **Fixed Position** off, then back on
4. The app uses your phone's GPS to set the node's location
5. Save

The node stores this position and broadcasts it to the mesh every 15 minutes. To update the position (e.g. if the node is relocated), repeat step 3.

---

## Deployment Notes

### Factory Reset Required on Previously Flashed Nodes

If a node previously had Meshtastic firmware on it, the old saved config overrides the new firmware defaults. Always factory reset after flashing:

```
python -m meshtastic --port COMX --factory-reset
```

**How to verify defaults applied correctly:** Connect via BLE — if the role shows as SENSOR, the defaults are active. If it shows CLIENT or another role, factory reset is needed.

### LoRa Region Resets on Factory Reset

Factory reset clears the LoRa region setting. Re-set it after every factory reset:

```
python -m meshtastic --port COMX --set lora.region US
```

### Environment Metrics Greyed Out in App

The Environment Metrics section in the Meshtastic app stays greyed out **permanently**, and that is expected. The node deliberately writes no EnvironmentMetrics fields — the reading travels in its own `navamesh.SoilReading` protobuf on PortNum 256 instead.

The node still emits an otherwise-empty environment telemetry packet each cycle, because the local loopback of that packet is what triggers `TelemetryRelayModule`. It costs a few bytes of airtime and keeps the existing trigger chain intact.

### Navamesh Relay Only Works Without Phone Connected

When a phone is connected via BLE, telemetry goes directly to the phone and skips the mesh relay. The `Soil: XX% | Bat | Up` messages to the navamesh channel only fire when no phone is connected. This is correct behavior — disconnect your phone after setup and the relay will work on the next telemetry cycle.

### Deployment Order Per Node

1. **Flash** the firmware
2. **Factory reset** via CLI
3. **Set LoRa region**: `python -m meshtastic --port COMX --set lora.region US`
4. **Add navamesh channel** via the Meshtastic app
5. **Set position** via Fixed Position toggle in the app

### Verifying Position in Serial Monitor

After setting the position, you should see:

```
Set local position: lat=XXXXXXXX lon=XXXXXXXX
Send location with precision 32
Position packet: lat=XXXXXXXX lon=XXXXXXXX
```

If `precision 32` shows the same lat/lon as the `Set local position` line, full precision is working correctly.

### Verifying Position via CLI

```
python -m meshtastic --port COMX --info
```

Look for the `position` section — it should show your actual coordinates.
