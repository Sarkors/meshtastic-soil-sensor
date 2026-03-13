#pragma once
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

#if defined(RAK4630)
    // RAK19007 + RAK4631 settings
    #define ANALOG_SOIL_PIN     31      // AIN1 on RAK19007
    #define ANALOG_SOIL_DRY     3120   // 14-bit ADC, calibrate these
    #define ANALOG_SOIL_WET     1567    // 14-bit ADC, calibrate these
    #define ANALOG_SOIL_3V3_EN  34      // must pull HIGH to power sensor
    #define ANALOG_SOIL_BITS    16383   // 14-bit max value

#elif defined(HELTEC_V3)
    // Heltec v3 settings
    #define ANALOG_SOIL_PIN     7       // GPIO 7
    #define ANALOG_SOIL_DRY     3500    // 12-bit ADC
    #define ANALOG_SOIL_WET     1500    // 12-bit ADC
    #define ANALOG_SOIL_BITS    4095    // 12-bit max value

#else
    #warning "AnalogSoilSensor: unknown board, using default pin 7"
    #define ANALOG_SOIL_PIN     7
    #define ANALOG_SOIL_DRY     3500
    #define ANALOG_SOIL_WET     1500
    #define ANALOG_SOIL_BITS    4095
#endif

class AnalogSoilSensor : public TelemetrySensor
{
  public:
    AnalogSoilSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual int32_t runOnce() override;
};

#endif