#pragma once
#include "SinglePortModule.h"
#include "configuration.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"

class TelemetryRelayModule : public SinglePortModule
{
  public:
    TelemetryRelayModule() : SinglePortModule("TelemetryRelay", meshtastic_PortNum_TELEMETRY_APP) {}

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // Accept locally generated packets as well as remote ones
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    }
};

extern TelemetryRelayModule *telemetryRelayModule;