#pragma once
#include "SinglePortModule.h"
#include "configuration.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "navamesh/navamesh.pb.h" // private Navamesh wire format, PortNum 256

// The module listens on TELEMETRY_APP because that is its *trigger* - it snoops the
// loopback of this node's own outgoing environment telemetry. The port it *sends* on
// is independent (PRIVATE_APP for the structured reading, TEXT_MESSAGE_APP for the
// human-readable debug line), because the packets are hand-built rather than
// allocated via SinglePortModule::allocDataPacket().
//
// wantPacket() matching only TELEMETRY_APP is load-bearing: Router::sendLocal() calls
// handleReceived() synchronously for broadcasts, so both of our sends re-enter
// MeshModule::callModules. Neither PRIVATE_APP nor TEXT_MESSAGE_APP can re-enter this
// module, so there is no recursion. Never add a module that wantPacket()s PRIVATE_APP
// and sends in response.
class TelemetryRelayModule : public SinglePortModule
{
  public:
    TelemetryRelayModule() : SinglePortModule("TelemetryRelay", meshtastic_PortNum_TELEMETRY_APP)
    {
        loopbackOk = true; // Must be set to receive this node's own outgoing telemetry packets
    }

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // Accept locally generated packets as well as remote ones
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
    }
};

extern TelemetryRelayModule *telemetryRelayModule;