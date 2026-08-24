#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "navamesh/navamesh.pb.h" // private Navamesh wire format

// Receives navamesh.NavameshCommand on PortNum 258 and acts on it, then acknowledges on
// PortNum 259. This is what lets the field crew change a sealed, solar-cased node without
// opening it: open a Bluetooth maintenance window, retune the telemetry interval, park a
// node in quiet mode, or set the fixed position it reports to the mesh.
//
// Why a separate portnum rather than sharing 256 with SoilReading: the portnum filter in
// SinglePortModule::wantPacket() then does all the message-type discrimination for us. Sharing
// 256 would mean every node decoding its neighbours' broadcast SoilReadings as candidate
// commands and needing a discriminator field to tell them apart. Only 256 and 257 are assigned
// upstream (_PortNum_MAX is 511), so 258/259 cannot collide with a stock portnum.
//
// Trust model: a command is accepted if it was PKI-encrypted to this node (a unicast from the Pi
// gateway, which proves the sender holds a private key we know), OR arrived on the "navamesh"
// channel (a broadcast, protected by that channel's PSK), OR was injected locally over USB/BLE.
// See isAuthorizedSender(). A monotonic command_id then guards against replay.
//
// Do NOT collapse this back to boundChannel: PKI unicasts arrive on Ch=0, not on navamesh, and
// boundChannel silently dropped every one of them.
class NavameshCommandModule : public ProtobufModule<navamesh_NavameshCommand>, private concurrency::OSThread
{
  public:
    NavameshCommandModule();

    /**
     * True while the node is parked in quiet mode and must not transmit.
     *
     * Static so the three transmit chokepoints can consult it without taking a dependency on
     * this module's instance existing (it is registered unconditionally, but the telemetry and
     * position modules are compiled under their own MESHTASTIC_EXCLUDE_* guards).
     */
    static bool isQuietModeActive();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, navamesh_NavameshCommand *cmd) override;
    virtual int32_t runOnce() override;

  private:
    /// Clamp and apply. Each returns the value actually applied, for the ack.
    uint32_t applyBleWindow(uint32_t minutes);
    uint32_t applyTelemetryInterval(uint32_t seconds);
    uint32_t applyQuietModeEnter(uint32_t minutes);
    void applyQuietModeExit();

    /**
     * Store a fixed position. Returns false without touching anything if the coordinates are
     * out of range or 0/0, so a node keeps a good position rather than adopting a bad one.
     *
     * Unlike the others there is no "value actually applied" to report: coordinates are stored
     * verbatim or refused, never clamped. A clamped latitude would be a different place.
     */
    // storedLatitudeI/storedLongitudeI receive the position read back out of the
    // nodeDB after the write, so the ack can report what the node holds rather than
    // echoing the request it cannot disagree with. Both may be null.
    bool applySetLocation(int32_t latitudeI, int32_t longitudeI,
                          int32_t *storedLatitudeI = nullptr, int32_t *storedLongitudeI = nullptr);

    /// Stage an ack. Deliberately deferred and jittered -- see the implementation.
    /// latitudeI/longitudeI are echoed for SET_LOCATION and left at 0 by every other command.
    /// wasBroadcast selects the reply spread: a slot derived from this node's id for a
    /// broadcast command (so n nodes do not answer at once), the tight random window for a
    /// unicast. It is passed explicitly rather than inferred from `dest`, because the
    /// unsolicited acks -- boot announce, quiet self-expiry -- are addressed to broadcast
    /// while being nobody's reply, and must not be spread as though they were.
    void queueAck(NodeNum dest, uint32_t commandId, navamesh_NavameshCommandType type, bool ok,
                  uint32_t appliedValue, int32_t latitudeI = 0, int32_t longitudeI = 0,
                  bool wasBroadcast = false);
    void sendQueuedAckIfDue(uint32_t now);

    void closeBleWindow();
    void checkQuietModeExpiry(uint32_t now);

    static bool quietModeActive;

    /// millis() deadlines. 0 means "not armed".
    uint32_t bleWindowEndMs = 0;
    uint32_t quietModeEndMs = 0;

    /**
     * Replay guard. A captured command could otherwise be rebroadcast verbatim by anyone in
     * radio range, since the payload is not signed. Commands must arrive with a strictly
     * increasing command_id.
     */
    uint32_t lastAcceptedCommandId = 0;

    /**
     * The verdict for lastAcceptedCommandId, retained so an exact duplicate can be re-acked
     * instead of dropped. The Pi retries when it does not hear an ack, and on a lossy unicast
     * link that is common; dropping the retry would make an applied command look like a dead node.
     */
    navamesh_NavameshCommandType lastAckType = navamesh_NavameshCommandType_NAVAMESH_COMMAND_UNKNOWN;
    bool lastAckOk = false;
    uint32_t lastAppliedValue = 0;
    int32_t lastAppliedLatitudeI = 0;
    int32_t lastAppliedLongitudeI = 0;
    /// Retained with the rest of the verdict so a re-ack of a duplicate spreads the same way
    /// the original did -- a retried broadcast is still a broadcast.
    bool lastWasBroadcast = false;

    /**
     * Single pending ack slot. Control traffic is human-paced, so a queue would be dead weight.
     * The tradeoff: back-to-back commands to one node drop all but the last ACK. The actions
     * themselves still all apply -- only the acknowledgement is lost.
     */
    bool ackPending = false;
    uint32_t ackDueMs = 0;
    NodeNum ackDest = 0;
    uint32_t ackCommandId = 0;
    navamesh_NavameshCommandType ackType = navamesh_NavameshCommandType_NAVAMESH_COMMAND_UNKNOWN;
    bool ackOk = false;
    uint32_t ackAppliedValue = 0;
    int32_t ackAppliedLatitudeI = 0;
    int32_t ackAppliedLongitudeI = 0;

    /// Set once the boot-time maintenance window has been armed by the first runOnce().
    bool bootWindowArmed = false;
};

extern NavameshCommandModule *navameshCommandModule;
