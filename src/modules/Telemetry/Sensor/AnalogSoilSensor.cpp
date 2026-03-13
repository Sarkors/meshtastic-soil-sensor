#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "AnalogSoilSensor.h"
#include <Arduino.h>

AnalogSoilSensor::AnalogSoilSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "AnalogSoil") {}

bool AnalogSoilSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
#ifdef ANALOG_SOIL_3V3_EN
    // Enable 3.3V peripheral rail on RAK boards
    pinMode(ANALOG_SOIL_3V3_EN, OUTPUT);
    digitalWrite(ANALOG_SOIL_3V3_EN, HIGH);
    delay(100);
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
    // Discard first 3 reads to flush ADC
    for (int i = 0; i < 3; i++) {
        analogRead(ANALOG_SOIL_PIN);
        delay(5);
    }

    // Average 5 readings for stability
    int total = 0;
    for (int i = 0; i < 5; i++) {
        total += analogRead(ANALOG_SOIL_PIN);
        delay(5);
    }
    int raw = total / 5;



raw = constrain(raw, ANALOG_SOIL_WET, ANALOG_SOIL_DRY);
    int moisture = map(raw, ANALOG_SOIL_DRY, ANALOG_SOIL_WET, 0, 100);

    LOG_INFO("AnalogSoilSensor: raw ADC=%d, moisture=%d%%", raw, moisture);

    measurement->variant.environment_metrics.has_soil_moisture = true;
    measurement->variant.environment_metrics.soil_moisture = (uint32_t)moisture;

    return true;
}

#endif