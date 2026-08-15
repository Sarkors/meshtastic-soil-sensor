#pragma once
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "navamesh/navamesh.pb.h" // private Navamesh wire format, PortNum 256

// Drains raw ADC readings produced by AnalogSoilSensor and broadcasts them as a
// navamesh.SoilReading on PortNum 256 (PRIVATE_APP), plus a human-readable debug line.
//
// This is an OSThread rather than a packet handler. It used to trigger off the local
// loopback of this node's own outgoing TELEMETRY_APP packet (loopbackOk = true), which
// made soil delivery depend on a long chain succeeding every cycle: sendTelemetry() had
// to take the mesh branch rather than the phone branch, Router::sendLocal() had to
// recognise the broadcast, perhapsDecode() had to succeed, port filtering had to not drop
// it, and callModules() had to dispatch. Any one of those breaking silently stopped soil
// data while the node otherwise looked healthy. Polling a pending flag removes the entire
// chain from the critical path.
//
// The thread does NOT sample the ADC. Sampling stays with EnvironmentTelemetryModule on
// the user-configured telemetry.environment_update_interval; this thread only drains what
// that sampling has already produced.
class TelemetryRelayModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    TelemetryRelayModule()
        : SinglePortModule("TelemetryRelay", meshtastic_PortNum_PRIVATE_APP), OSThread("TelemetryRelay")
    {
    }

  protected:
    virtual int32_t runOnce() override;

  private:
    /**
     * Broadcast the pending reading, if there is one.
     *
     * Returns true when the authoritative SoilReading was encoded and handed to the mesh.
     * On any failure the reading is deliberately left pending so the next poll retries it.
     */
    bool sendPendingReading();
};

extern TelemetryRelayModule *telemetryRelayModule;
