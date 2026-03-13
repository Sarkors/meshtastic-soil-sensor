# Configuration Guide

---

## Calibration

Calibration values are defined in `src/modules/Telemetry/Sensor/AnalogSoilSensor.h`. Update these before building to match your sensor and soil conditions.

### How to Calibrate

1. Flash the firmware as-is and open the serial monitor:
   ```bash
   pio device monitor --port COMX --baud 115200
   ```
2. Insert the sensor into **dry soil** and note the raw ADC value:
   ```
   [EnvironmentTelemetry] AnalogSoilSensor: raw ADC=XXXX, moisture=XX%
   ```
3. Insert the sensor into **wet/saturated soil** and note the raw ADC value
4. Update the defines in `AnalogSoilSensor.h` with your values
5. Rebuild and reflash

> Use real soil as your bounds — dry soil to wet/muddy soil. Do not use open air or open water as these produce a scale that doesn't reflect actual field conditions.

### RAK4631

```cpp
#if defined(RAK4630)
    #define ANALOG_SOIL_PIN     31      // AIN1 on RAK19007 = P0.31 — do not change
    #define ANALOG_SOIL_DRY     3040    // raw ADC in dry soil — update this
    #define ANALOG_SOIL_WET     1567    // raw ADC in wet/muddy soil — update this
    #define ANALOG_SOIL_BITS    4095
    #define ANALOG_SOIL_3V3_EN  34      // 3.3V rail enable — do not change
```

### Heltec v3

```cpp
#elif defined(HELTEC_V3)
    #define ANALOG_SOIL_PIN     7       // GPIO 7 — do not change
    #define ANALOG_SOIL_DRY     3500    // raw ADC in dry soil — update this
    #define ANALOG_SOIL_WET     1500    // raw ADC in wet/muddy soil — update this
    #define ANALOG_SOIL_BITS    4095
```

### Our Reference Readings (HD-38 sensor)

| Condition | RAK4631 ADC |
|-----------|-------------|
| Dry air | 4095 |
| Dry soil | 3040 |
| Average moist soil | ~2879 |
| Wet/muddy soil | 1567 |
| Open water | 849 |

---

## Changing the Target Channel

The relay sends soil readings to a channel named `navamesh` by default. To change this, find this line inside `sendTelemetry()` in `src/modules/Telemetry/EnvironmentTelemetry.cpp`:

```cpp
if (strcmp(channelFile.channels[i].settings.name, "navamesh") == 0) {
```

Replace `"navamesh"` with your channel name:

```cpp
if (strcmp(channelFile.channels[i].settings.name, "YOUR_CHANNEL_NAME") == 0) {
```

> If the named channel is not configured on the device the relay automatically falls back to the primary channel (channel 0).

---

## Telemetry Interval

Set after flashing via CLI. The minimum allowed interval is 60 seconds.

| Interval | Command value |
|----------|--------------|
| 1 minute (testing) | 60 |
| 15 minutes | 900 |
| 1 hour | 3600 |
| 3 hours (field deploy) | 10800 |

```bash
python -m meshtastic --port COMX --set telemetry.environment_update_interval 10800
```

---

## Adding Support for a New Board

Add a new `#elif` block in `AnalogSoilSensor.h`:

```cpp
#elif defined(YOUR_BOARD_DEFINE)
    #define ANALOG_SOIL_PIN     X       // ADC-capable GPIO pin
    #define ANALOG_SOIL_DRY     XXXX    // calibrate for your sensor
    #define ANALOG_SOIL_WET     XXXX
    #define ANALOG_SOIL_BITS    4095    // 4095 for 12-bit, 1023 for 10-bit ADC
```

Check your board's PlatformIO build flags to find the correct `#define` identifier for your target board.

