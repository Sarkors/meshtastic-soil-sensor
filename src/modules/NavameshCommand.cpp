#include "NavameshCommand.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h" // pb_encode_to_bytes
#include "mesh/TypeConversions.h" // ConvertToPositionLite
#if !MESHTASTIC_EXCLUDE_GPS
#include "modules/PositionModule.h" // sendOurPosition()
#endif

NavameshCommandModule *navameshCommandModule;

bool NavameshCommandModule::quietModeActive = false;

// The private channel. Commands may arrive on it (broadcast, PSK-encrypted) or PKI-encrypted on
// Ch=0 (unicast); see isAuthorizedSender(). Acks always go out on it.
#define NAVAMESH_COMMAND_CHANNEL_NAME "navamesh"

// Portnums. Cast at the assignment site because these are not members of the upstream PortNum
// enum, and protobufs/ is a pinned submodule we never modify. The wire carries a varint either
// way, so the cast is purely a C++ type formality.
#define NAVAMESH_COMMAND_PORTNUM ((meshtastic_PortNum)258)
#define NAVAMESH_ACK_PORTNUM ((meshtastic_PortNum)259)

// How often the thread checks its deadlines. Coarse on purpose: these are human-scale timers
// (minutes to days), so a 2 s granularity costs nothing and keeps the thread cheap.
#define NAVAMESH_POLL_INTERVAL_MS 2000

// Bluetooth maintenance window bounds, in minutes.
#define NAVAMESH_BLE_WINDOW_MIN_MINUTES 1
#define NAVAMESH_BLE_WINDOW_MAX_MINUTES 240
#define NAVAMESH_BLE_WINDOW_DEFAULT_MINUTES 15

// Every boot opens a window, whether or not anyone asked. This is the last-resort recovery path:
// if a node ever stops responding to commands, a power cycle still yields a connectable node.
#define NAVAMESH_BOOT_BLE_WINDOW_MINUTES 30

// Quiet mode bounds, in minutes. The ceiling is the anti-stranding guarantee: even if every
// QUIET_MODE_EXIT is lost, the node starts transmitting again on its own.
#define NAVAMESH_QUIET_MIN_MINUTES 1
#define NAVAMESH_QUIET_MAX_MINUTES 4320 // 3 days
#define NAVAMESH_QUIET_DEFAULT_MINUTES 1440

// Telemetry interval bounds, in seconds.
//
// The floor is 60 rather than something more conservative because 60 s is the cadence the soil
// calibration bench actually runs at, and requiring a USB cable or a BLE session to reach it
// would defeat the point of commanding intervals over the air at all.
//
// 60 s is ~480x the 8 h field default, so it is a bench setting, not a field one -- the operator
// UI deliberately offers no preset below 5 min. airTime->isTxAllowedChannelUtil() /
// isTxAllowedAirUtil() remain the real regulatory backstop; this bound is defence in depth.
#define NAVAMESH_INTERVAL_MIN_SECONDS 60
#define NAVAMESH_INTERVAL_MAX_SECONDS 86400

// Fixed position bounds, in degrees * 1e7. Rejected rather than clamped: a clamped coordinate is
// a different place on Earth, and silently relocating a node is worse than refusing to move it.
#define NAVAMESH_LATITUDE_I_LIMIT 900000000   // +/- 90 degrees
#define NAVAMESH_LONGITUDE_I_LIMIT 1800000000 // +/- 180 degrees

// Acks are jittered across this span. A broadcast command lands on all 18 nodes within the same
// few milliseconds; if they all replied at once the collisions would cost us most of the acks.
#define NAVAMESH_ACK_JITTER_MIN_MS 200
#define NAVAMESH_ACK_JITTER_MAX_MS 4000

NavameshCommandModule::NavameshCommandModule()
    : ProtobufModule("NavameshCommand", NAVAMESH_COMMAND_PORTNUM, &navamesh_NavameshCommand_msg),
      OSThread("NavameshCommand")
{
    // NOTE: boundChannel is deliberately NOT set here, even though it looks like the obvious fit.
    //
    // MeshModule::callModules() would then require the packet to arrive on the "navamesh"
    // channel -- but a unicast command to a node whose public key the sender knows is
    // PKI-encrypted and arrives on Ch=0, not on navamesh. boundChannel rejected exactly those
    // with "packet on wrong channel", silently dropping every per-node command from the Pi.
    // Found on hardware; USB-loopback testing cannot catch it because a locally injected
    // command has mp.from == 0, which bypasses the channel check entirely.
    //
    // isAuthorizedSender() below reimplements the check to accept both paths.
}

bool NavameshCommandModule::isQuietModeActive()
{
    return quietModeActive;
}

/// Index of the "navamesh" channel, or -1 when it is absent. Callers must handle -1 rather than
/// silently falling back to channel 0, which would put commands and acks on whatever channel 0
/// happens to be -- possibly one the Pi is not listening on.
static int findNavameshChannel()
{
    for (uint8_t i = 0; i < channelFile.channels_count; i++) {
        if (channelFile.channels[i].role != meshtastic_Channel_Role_DISABLED &&
            strcmp(channelFile.channels[i].settings.name, NAVAMESH_COMMAND_CHANNEL_NAME) == 0)
            return (int)i;
    }
    return -1;
}

/**
 * May this packet command us?
 *
 * Two acceptable proofs, because Meshtastic encrypts unicast and broadcast differently:
 *
 *  - PKI: the sender encrypted to our public key, so it holds a private key we already know.
 *    This is what a unicast command from the Pi gateway looks like, and it is a STRONGER proof
 *    than the channel PSK -- it authenticates one sender rather than anyone holding a shared key.
 *  - The navamesh channel: a broadcast command cannot use PKI, so it is protected by that
 *    channel's PSK instead. Anything we could not decrypt never reaches this module at all.
 *
 * mp.from == 0 means the command was injected locally over USB or the phone API, which is
 * already a trusted path (physical or paired access to this node).
 */
static bool isAuthorizedSender(const meshtastic_MeshPacket &mp)
{
    if (mp.from == 0)
        return true;
    if (mp.pki_encrypted)
        return true;

    int navameshCh = findNavameshChannel();
    return navameshCh >= 0 && mp.channel == (uint8_t)navameshCh;
}

uint32_t NavameshCommandModule::applyBleWindow(uint32_t minutes)
{
    uint32_t clamped = minutes ? minutes : NAVAMESH_BLE_WINDOW_DEFAULT_MINUTES;
    if (clamped < NAVAMESH_BLE_WINDOW_MIN_MINUTES)
        clamped = NAVAMESH_BLE_WINDOW_MIN_MINUTES;
    if (clamped > NAVAMESH_BLE_WINDOW_MAX_MINUTES)
        clamped = NAVAMESH_BLE_WINDOW_MAX_MINUTES;

    bleWindowEndMs = millis() + clamped * 60000UL;
    if (bleWindowEndMs == 0)
        bleWindowEndMs = 1; // 0 is our "not armed" sentinel; a millis() wrap must not disarm us

    // Ask PowerFSM to turn the radio on. We never call setBluetoothEnable() ourselves so that
    // nbEnter()/darkEnter() remain the only places the BLE radio is touched.
    powerFSM.trigger(EVENT_BLE_WINDOW_REQUESTED);

    LOG_INFO("NavameshCommand: BLE window open for %u min", (unsigned)clamped);
    return clamped;
}

void NavameshCommandModule::closeBleWindow()
{
    bleWindowEndMs = 0;
    powerFSM.trigger(EVENT_BLE_WINDOW_EXPIRED);
    LOG_INFO("NavameshCommand: BLE window closed");
}

uint32_t NavameshCommandModule::applyTelemetryInterval(uint32_t seconds)
{
    uint32_t clamped = seconds;
    if (clamped < NAVAMESH_INTERVAL_MIN_SECONDS)
        clamped = NAVAMESH_INTERVAL_MIN_SECONDS;
    if (clamped > NAVAMESH_INTERVAL_MAX_SECONDS)
        clamped = NAVAMESH_INTERVAL_MAX_SECONDS;

    moduleConfig.telemetry.environment_update_interval = clamped;

    // Persist, but do NOT go through MeshService::reloadConfig(): that also calls
    // resetRadioConfig() and notifies configChanged observers, which reconfigures the radio
    // hardware. Nothing about a telemetry interval warrants disturbing the radio.
    //
    // Also deliberately NOT routed through AdminModule::handleSetModuleConfig(), which forces
    // shouldReboot = true for every telemetry change. EnvironmentTelemetryModule::runOnce()
    // re-reads this field live on each pass, so the new cadence applies on the next cycle with
    // no reboot at all -- which is the entire point of doing this over LoRa.
    nodeDB->saveToDisk(SEGMENT_MODULECONFIG);

    LOG_INFO("NavameshCommand: telemetry interval = %u s (live, no reboot)", (unsigned)clamped);
    return clamped;
}

uint32_t NavameshCommandModule::applyQuietModeEnter(uint32_t minutes)
{
    uint32_t clamped = minutes ? minutes : NAVAMESH_QUIET_DEFAULT_MINUTES;
    if (clamped < NAVAMESH_QUIET_MIN_MINUTES)
        clamped = NAVAMESH_QUIET_MIN_MINUTES;
    if (clamped > NAVAMESH_QUIET_MAX_MINUTES)
        clamped = NAVAMESH_QUIET_MAX_MINUTES;

    quietModeActive = true;
    quietModeEndMs = millis() + clamped * 60000UL;
    if (quietModeEndMs == 0)
        quietModeEndMs = 1;

    LOG_INFO("NavameshCommand: quiet mode on, auto-resume in %u min", (unsigned)clamped);
    return clamped;
}

bool NavameshCommandModule::applySetLocation(int32_t latitudeI, int32_t longitudeI,
                                             int32_t *storedLatitudeI, int32_t *storedLongitudeI)
{
    if (latitudeI < -NAVAMESH_LATITUDE_I_LIMIT || latitudeI > NAVAMESH_LATITUDE_I_LIMIT ||
        longitudeI < -NAVAMESH_LONGITUDE_I_LIMIT || longitudeI > NAVAMESH_LONGITUDE_I_LIMIT) {
        LOG_WARN("NavameshCommand: refusing out-of-range position %d, %d", (int)latitudeI, (int)longitudeI);
        return false;
    }

    // 0/0 is a real place, but nothing in the Navajo region is within 2000 km of it. Every 0/0
    // we will ever see is a sender that had no fix and sent its zero-initialised struct anyway.
    if (latitudeI == 0 && longitudeI == 0) {
        LOG_WARN("NavameshCommand: refusing 0/0 position (sender had no GPS fix)");
        return false;
    }

    // This mirrors AdminModule's set_fixed_position handler exactly, which is the path the
    // Meshtastic app's "Fixed Position" toggle takes over BLE. Same four writes, same order.
    // Deliberately not routed through AdminModule itself: reaching it would need an AdminMessage
    // wrapped in the admin channel's session-key handshake, which is precisely the phone-shaped
    // ceremony this command exists to avoid.
    meshtastic_Position pos = meshtastic_Position_init_default;
    pos.has_latitude_i = true;
    pos.latitude_i = latitudeI;
    pos.has_longitude_i = true;
    pos.longitude_i = longitudeI;
    // No altitude: the sender is a phone, and phone altitude is unreliable enough that storing it
    // would be worse than leaving it unset. Nothing downstream reads it.
    pos.location_source = meshtastic_Position_LocSource_LOC_MANUAL;

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!node) {
        LOG_ERROR("NavameshCommand: no NodeInfoLite for ourselves, cannot set position");
        return false;
    }
    node->has_position = true;
    node->position = TypeConversions::ConvertToPositionLite(pos);
    nodeDB->setLocalPosition(pos);
    config.position.fixed_position = true;

    // AdminModule saves these same two segments with shouldReboot = false. A position is one of
    // the few config changes that genuinely applies live: MeshService reads node->position on
    // every outbound position packet, so the next broadcast already carries the new coordinates.
    nodeDB->saveToDisk(SEGMENT_NODEDATABASE | SEGMENT_CONFIG);

    // Broadcast immediately rather than waiting out the 15-minute position interval, so the Pi
    // can confirm on the map that the node moved. Suppressed while quiet mode is active, which is
    // correct: the ack still carries the coordinates, so nothing is lost but the map update.
    //
    // Guarded the same way AdminModule's set_fixed_position guards it. The stored position above
    // is the part that matters and it lands either way; this only skips the eager broadcast.
#if !MESHTASTIC_EXCLUDE_GPS
    if (positionModule)
        positionModule->sendOurPosition();
#endif

    // Read the position back out of the nodeDB rather than echoing the request.
    //
    // The ack used to report whatever was asked for, so it could not disagree with the
    // command -- which makes a write that succeeds and does not persist completely
    // undetectable from the app. That is not hypothetical: a node whose nodeDB had been
    // corrupted acked ok=True and went on broadcasting a position 2 km away, and only a
    // factory reset fixed it. An ack that echoes its own input can only ever confirm
    // that the packet arrived.
    //
    // Re-fetching the node is deliberate: reusing the `node` pointer from above would
    // re-read the same struct this function just wrote, and a lookup that returns the
    // wrong entry -- or none -- is exactly one of the failures worth catching.
    meshtastic_NodeInfoLite *readback = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!readback || !readback->has_position) {
        LOG_ERROR("NavameshCommand: position did not persist (no position after write)");
        return false;
    }
    if (storedLatitudeI)
        *storedLatitudeI = readback->position.latitude_i;
    if (storedLongitudeI)
        *storedLongitudeI = readback->position.longitude_i;

    // Reported, not repaired. A mismatch here means the nodeDB is not storing what it is
    // told, and the honest thing is to fail the ack and let the coordinates in it show
    // the operator what the node actually holds -- retrying a write that just silently
    // disagreed would only produce the same ack again.
    if (readback->position.latitude_i != latitudeI || readback->position.longitude_i != longitudeI) {
        LOG_ERROR("NavameshCommand: position readback disagrees: asked %d,%d stored %d,%d",
                  (int)latitudeI, (int)longitudeI,
                  (int)readback->position.latitude_i, (int)readback->position.longitude_i);
        return false;
    }

    LOG_INFO("NavameshCommand: fixed position = %d, %d (live, no reboot, readback ok)",
             (int)latitudeI, (int)longitudeI);
    return true;
}

void NavameshCommandModule::applyQuietModeExit()
{
    quietModeActive = false;
    quietModeEndMs = 0;
    LOG_INFO("NavameshCommand: quiet mode off, transmitting again");
}

void NavameshCommandModule::checkQuietModeExpiry(uint32_t now)
{
    if (!quietModeActive || quietModeEndMs == 0)
        return;
    if ((int32_t)(now - quietModeEndMs) < 0)
        return;

    LOG_WARN("NavameshCommand: quiet mode self-expired (no QUIET_MODE_EXIT was ever heard)");
    applyQuietModeExit();

    // Tell the Pi unsolicited, so a node that recovered on its own does not look like a node
    // that is still silently parked. command_id 0 marks this as unsolicited.
    queueAck(NODENUM_BROADCAST, 0, navamesh_NavameshCommandType_QUIET_MODE_EXIT, true, 0);
}

bool NavameshCommandModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, navamesh_NavameshCommand *cmd)
{
    if (!cmd)
        return false;

    if (!isAuthorizedSender(mp)) {
        LOG_WARN("NavameshCommand: refusing command from 0x%x (ch %d, pki %d)", (unsigned)mp.from,
                 (int)mp.channel, (int)mp.pki_encrypted);
        return true; // handled: swallow it rather than letting other modules see a command
    }

    // command_id 0 is reserved for unsolicited acks, so a command may never use it.
    if (cmd->command_id == 0) {
        LOG_WARN("NavameshCommand: rejecting command with command_id=0");
        return true;
    }

    // A duplicate is almost always the Pi retrying because it never heard our ack (LoRa is lossy
    // and acks are unicast). Re-send the previous verdict rather than dropping: the command was
    // already applied, and staying silent would make every retry look like a dead node.
    if (cmd->command_id == lastAcceptedCommandId) {
        LOG_INFO("NavameshCommand: duplicate command_id=%u, re-acking previous result",
                 (unsigned)cmd->command_id);
        queueAck(mp.from, lastAcceptedCommandId, lastAckType, lastAckOk, lastAppliedValue,
                 lastAppliedLatitudeI, lastAppliedLongitudeI);
        return true;
    }

    // Anything older is a replay. There is no signature on this payload, so without this guard a
    // captured packet could be rebroadcast verbatim by anyone in radio range.
    if (cmd->command_id < lastAcceptedCommandId) {
        LOG_WARN("NavameshCommand: rejecting stale command_id=%u (last accepted %u)",
                 (unsigned)cmd->command_id, (unsigned)lastAcceptedCommandId);
        return true;
    }

    uint32_t applied = 0;
    int32_t appliedLatitudeI = 0;
    int32_t appliedLongitudeI = 0;
    bool ok = true;

    switch (cmd->command_type) {
    case navamesh_NavameshCommandType_BLE_WINDOW:
        applied = applyBleWindow(cmd->duration_minutes);
        break;
    case navamesh_NavameshCommandType_SET_TELEMETRY_INTERVAL:
        applied = applyTelemetryInterval(cmd->interval_seconds);
        break;
    case navamesh_NavameshCommandType_QUIET_MODE_ENTER:
        applied = applyQuietModeEnter(cmd->duration_minutes);
        break;
    case navamesh_NavameshCommandType_QUIET_MODE_EXIT:
        applyQuietModeExit();
        break;
    case navamesh_NavameshCommandType_SET_LOCATION:
        // The applied coordinates come back from the nodeDB, not from the request, so
        // the ack reports what the node holds rather than what it was told. On failure
        // they carry whatever was actually stored, which is what makes a disagreement
        // visible from the app instead of looking like a rejected value.
        ok = applySetLocation(cmd->latitude_i, cmd->longitude_i, &appliedLatitudeI, &appliedLongitudeI);
        break;
    default:
        LOG_WARN("NavameshCommand: unknown command_type=%d", (int)cmd->command_type);
        ok = false;
        break;
    }

    lastAcceptedCommandId = cmd->command_id;
    lastAckType = cmd->command_type;
    lastAckOk = ok;
    lastAppliedValue = applied;
    lastAppliedLatitudeI = appliedLatitudeI;
    lastAppliedLongitudeI = appliedLongitudeI;

    queueAck(mp.from, cmd->command_id, cmd->command_type, ok, applied, appliedLatitudeI, appliedLongitudeI);
    return true;
}

void NavameshCommandModule::queueAck(NodeNum dest, uint32_t commandId, navamesh_NavameshCommandType type,
                                     bool ok, uint32_t appliedValue, int32_t latitudeI, int32_t longitudeI)
{
    ackPending = true;
    ackDest = dest;
    ackCommandId = commandId;
    ackType = type;
    ackOk = ok;
    ackAppliedValue = appliedValue;
    ackAppliedLatitudeI = latitudeI;
    ackAppliedLongitudeI = longitudeI;
    ackDueMs = millis() + random(NAVAMESH_ACK_JITTER_MIN_MS, NAVAMESH_ACK_JITTER_MAX_MS);
}

void NavameshCommandModule::sendQueuedAckIfDue(uint32_t now)
{
    if (!ackPending || (int32_t)(now - ackDueMs) < 0)
        return;

    navamesh_NavameshAck ack = navamesh_NavameshAck_init_zero;
    ack.command_id = ackCommandId;
    ack.command_type = ackType;
    ack.ok = ackOk;
    ack.applied_value = ackAppliedValue;
    ack.applied_latitude_i = ackAppliedLatitudeI;
    ack.applied_longitude_i = ackAppliedLongitudeI;

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        // Leave ackPending set so the next tick retries. Matches the retry-on-exhaustion approach
        // TelemetryRelayModule uses for readings.
        LOG_WARN("NavameshCommand: packet pool exhausted, retry ack next tick");
        return;
    }

    // Acks always ride the navamesh channel. The command may have arrived PKI-encrypted on
    // Ch=0, and the Pi's bridge only subscribes to the private channel -- echoing mp.channel
    // would put the ack somewhere nobody is listening. Fail closed if the channel is missing.
    int ackCh = findNavameshChannel();
    if (ackCh < 0) {
        LOG_ERROR("NavameshCommand: no '%s' channel, dropping ack", NAVAMESH_COMMAND_CHANNEL_NAME);
        ackPending = false;
        packetPool.release(p);
        return;
    }

    ackPending = false;
    // Broadcast rather than unicast back to the sender, for two reasons found on hardware:
    //
    //  1. A locally-injected command (USB or the phone app) arrives with mp.from == 0, so
    //     unicasting to it produced "Packet received with to: of 0!" and the ack was
    //     dropped outright.
    //  2. Even with a real sender, a unicast goes down the PKI path and Meshtastic refuses
    //     the DM when the destination's public key is not yet known
    //     ("refusing to send legacy DM", Error=39). Acks would then vanish silently, which
    //     is precisely the "did my command land?" ambiguity this ack exists to remove.
    //
    // The Pi correlates by command_id, not by addressing, so a broadcast on the private
    // channel serves it just as well and costs one packet either way.
    p->to = NODENUM_BROADCAST;
    p->channel = (uint8_t)ackCh;
    p->decoded.portnum = NAVAMESH_ACK_PORTNUM;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &navamesh_NavameshAck_msg, &ack);
    if (p->decoded.payload.size == 0) {
        LOG_ERROR("NavameshCommand: ack encode failed, dropping");
        packetPool.release(p);
        return;
    }

    LOG_INFO("NavameshCommand: ack id=%u type=%d ok=%d applied=%u", (unsigned)ackCommandId, (int)ackType, (int)ackOk,
             (unsigned)ackAppliedValue);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

int32_t NavameshCommandModule::runOnce()
{
    uint32_t now = millis();

    // Open a window on the first tick after boot. Doing it here rather than in the constructor
    // keeps it off the setup path, where PowerFSM may not be built yet.
    if (!bootWindowArmed) {
        bootWindowArmed = true;
        applyBleWindow(NAVAMESH_BOOT_BLE_WINDOW_MINUTES);
    }

    if (bleWindowEndMs != 0 && (int32_t)(now - bleWindowEndMs) >= 0)
        closeBleWindow();

    checkQuietModeExpiry(now);
    sendQueuedAckIfDue(now);

    return NAVAMESH_POLL_INTERVAL_MS;
}
