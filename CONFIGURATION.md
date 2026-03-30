# Configuration Guide

## Sensor Calibration

The calibration values determine how raw ADC readings are converted to moisture percentages. These are set in `src/modules/Telemetry/Sensor/AnalogSoilSensor.h`.

### How to Calibrate

1. Flash the firmware and open the serial monitor
2. Stick the sensor in **dry soil** — note the raw ADC value
3. Stick the sensor in **wet/muddy soil** — note the raw ADC value
4. Update the values in `AnalogSoilSensor.h`:

**For RAK4631:**

```cpp
#if defined(RAK4630)
    #define ANALOG_SOIL_PIN     31      // AIN1 = P0.31
    #define ANALOG_SOIL_DRY     3040    // ← your dry soil reading
    #define ANALOG_SOIL_WET     1567    // ← your wet soil reading
    #define ANALOG_SOIL_BITS    4095
    #define ANALOG_SOIL_3V3_EN  34
```

**For Heltec v3:**

```cpp
#elif defined(HELTEC_V3)
    #define ANALOG_SOIL_PIN     7
    #define ANALOG_SOIL_DRY     3500    // ← your dry soil reading
    #define ANALOG_SOIL_WET     1500    // ← your wet soil reading
    #define ANALOG_SOIL_BITS    4095
```

5. Rebuild and reflash after updating values.

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

The channel name is defined in `src/modules/Telemetry/EnvironmentTelemetry.cpp` in the relay block:

```cpp
if (strcmp(channelFile.channels[i].settings.name, "navamesh") == 0) {
```

Change `"navamesh"` to your preferred channel name, rebuild, and reflash.

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

The Environment Metrics section in the Meshtastic app will be greyed out until the node sends its first telemetry reading. This is normal. To trigger it immediately, connect your phone via BLE — telemetry fires right away when a phone connects. After the first reading, the section becomes active.

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
