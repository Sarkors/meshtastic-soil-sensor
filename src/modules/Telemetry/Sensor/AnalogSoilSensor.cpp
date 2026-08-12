#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "AnalogSoilSensor.h"
#include <Arduino.h>

uint16_t AnalogSoilSensor::lastRawAdc = 0;
bool AnalogSoilSensor::readingPending = false;

bool AnalogSoilSensor::hasPendingReading()
{
    return readingPending;
}

bool AnalogSoilSensor::peekReading(uint16_t &rawAdcOut)
{
    if (!readingPending)
        return false;
    rawAdcOut = lastRawAdc;
    return true;
}

void AnalogSoilSensor::consumeReading()
{
    readingPending = false;
}

AnalogSoilSensor::AnalogSoilSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "AnalogSoil") {}

bool AnalogSoilSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
#ifdef ANALOG_SOIL_3V3_EN
    pinMode(ANALOG_SOIL_3V3_EN, OUTPUT);
    digitalWrite(ANALOG_SOIL_3V3_EN, LOW); // keep rail off until a reading is needed
#endif
    pinMode(ANALOG_SOIL_PIN, INPUT);
    LOG_INFO("AnalogSoilSensor: init on GPIO %d", ANALOG_SOIL_PIN);
    status = 1;
    initialized = true;
    return true;
}

int32_t AnalogSoilSensor::runOnce()
{
    if (!initialized) {
        initDevice(nullptr, nullptr);
    }
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool AnalogSoilSensor::getMetrics(meshtastic_Telemetry *measurement)
{
#ifdef ANALOG_SOIL_3V3_EN
    digitalWrite(ANALOG_SOIL_3V3_EN, HIGH);
    delay(ANALOG_SOIL_SETTLE_MS); // wait for rail and sensor to stabilize
#endif

    // Discard the first few reads to flush the ADC
    for (int i = 0; i < ANALOG_SOIL_DISCARD_SAMPLES; i++) {
        analogRead(ANALOG_SOIL_PIN);
        delay(ANALOG_SOIL_SAMPLE_GAP_MS);
    }

    // Average ANALOG_SOIL_SAMPLES readings for stability
    uint32_t total = 0;
    for (int i = 0; i < ANALOG_SOIL_SAMPLES; i++) {
        total += (uint32_t)analogRead(ANALOG_SOIL_PIN);
        delay(ANALOG_SOIL_SAMPLE_GAP_MS);
    }
    uint16_t raw = (uint16_t)(total / ANALOG_SOIL_SAMPLES);

#ifdef ANALOG_SOIL_3V3_EN
    digitalWrite(ANALOG_SOIL_3V3_EN, LOW); // power down rail after reading
#endif

    // RAW ONLY. No constrain(), no map(), no scaling. The Raspberry Pi owns
    // calibration. `raw` is never reassigned between here and transmission.
    lastRawAdc = raw;
    readingPending = true;

    LOG_INFO("AnalogSoilSensor: raw ADC = %u", (unsigned)raw);

    // Deliberately writes NO EnvironmentMetrics field. The raw count has no home in
    // the stock protobuf (soil_moisture is a uint8_t percent -- int_size:8 in
    // telemetry.options, so it cannot hold a 0..4095 count) and we refuse to derive
    // a percentage on the node.
    //
    // We still return true so EnvironmentTelemetryModule::getEnvironmentTelemetry()
    // reports valid and sendTelemetry() emits its TELEMETRY_APP broadcast -- the
    // local loopback of that broadcast is what drives TelemetryRelayModule. The
    // resulting EnvironmentMetrics submessage is empty; that is the deliberate cost
    // of preserving the existing trigger chain without restructuring the module.
    return true;
}

#endif
