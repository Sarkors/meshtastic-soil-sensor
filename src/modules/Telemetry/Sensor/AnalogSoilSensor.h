#pragma once
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

// NOTE: there are deliberately NO calibration constants here.
// The node ships the raw averaged ADC count and the Raspberry Pi owns the
// raw -> moisture-% mapping, so calibration can change without reflashing
// deployed nodes. Do not reintroduce ANALOG_SOIL_DRY / ANALOG_SOIL_WET.

#if defined(RAK4630)
// RAK19007 + RAK4631
#define ANALOG_SOIL_PIN 31    // AIN1 = P0.31
#define ANALOG_SOIL_3V3_EN 34 // must be driven HIGH to power the sensor

#elif defined(HELTEC_V3)
#define ANALOG_SOIL_PIN 7 // GPIO 7

#else
#warning "AnalogSoilSensor: unknown board, using default pin 7"
#define ANALOG_SOIL_PIN 7
#endif

// Sampling profile - the only node-side knobs that remain.
#define ANALOG_SOIL_SETTLE_MS 100     // rail + sensor settle time after power-on
#define ANALOG_SOIL_DISCARD_SAMPLES 3 // throwaway reads to flush the SAADC
#define ANALOG_SOIL_SAMPLES 5         // reads that get averaged
#define ANALOG_SOIL_SAMPLE_GAP_MS 5   // delay between reads

class AnalogSoilSensor : public TelemetrySensor
{
  public:
    AnalogSoilSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual int32_t runOnce() override;

    /**
     * Pending-reading handshake with TelemetryRelayModule.
     *
     * Lifecycle: getMetrics() samples the ADC, averages, stores the raw count and
     * marks it pending. TelemetryRelayModule peeks it, encodes the protobuf, and
     * only calls consumeReading() once the packet has actually been handed to the
     * mesh. Peek and consume are deliberately split rather than folded into a
     * single take(): if encoding fails we must NOT drop the reading on the floor.
     *
     * Because consumeReading() clears the flag, an unrelated TELEMETRY_APP packet
     * looping back later (e.g. DeviceTelemetry's device_metrics broadcast) cannot
     * cause a stale ADC value to be retransmitted. A soil packet is only ever
     * produced by an actual fresh ADC measurement.
     */

    /// True when an ADC measurement has been taken but not yet transmitted.
    static bool hasPendingReading();

    /// Non-destructively read the pending raw ADC. Returns false if none is pending.
    static bool peekReading(uint16_t &rawAdcOut);

    /// Clear the pending reading. Call ONLY after the value has been successfully
    /// encoded and sent, so a failed encode leaves the reading available.
    static void consumeReading();

  private:
    /// Mean of ANALOG_SOIL_SAMPLES reads from the most recent getMetrics().
    /// RAW: never constrained, never mapped, never scaled.
    static uint16_t lastRawAdc;

    /// Set by getMetrics(); cleared by consumeReading().
    static bool readingPending;
};

#endif
