#include "TelemetryRelay.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include <pb_decode.h>

TelemetryRelayModule *telemetryRelayModule;

// Name of the channel to relay telemetry to
#define RELAY_CHANNEL_NAME "navamesh"

static uint8_t findChannelByName(const char *name)
{
    for (uint8_t i = 0; i < channelFile.channels_count; i++) {
        if (channelFile.channels[i].role != meshtastic_Channel_Role_DISABLED &&
            strcmp(channelFile.channels[i].settings.name, name) == 0) {
            LOG_INFO("TelemetryRelay: found channel '%s' at index %d", name, i);
            return i;
        }
    }
    LOG_WARN("TelemetryRelay: channel '%s' not found, falling back to primary", name);
    return 0; // fall back to primary if not found
}

ProcessMessage TelemetryRelayModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Only relay our own outgoing telemetry, not packets from other nodes
    if (mp.from != nodeDB->getNodeNum())
        return ProcessMessage::CONTINUE;

    // Decode the telemetry payload
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size,
                              &meshtastic_Telemetry_msg, &telemetry)) {
        return ProcessMessage::CONTINUE;
    }

    // Only handle environment metrics
    if (telemetry.which_variant != meshtastic_Telemetry_environment_metrics_tag)
        return ProcessMessage::CONTINUE;

    const auto &env = telemetry.variant.environment_metrics;

    // Build the message string
    char msg[64];
    if (env.has_soil_moisture) {
        snprintf(msg, sizeof(msg), "Soil: %u%%", env.soil_moisture);
    } else {
        return ProcessMessage::CONTINUE;
    }

    // Find the target channel
    uint8_t channelIndex = findChannelByName(RELAY_CHANNEL_NAME);

    LOG_INFO("TelemetryRelay: sending '%s' to channel %d (%s)", msg, channelIndex, RELAY_CHANNEL_NAME);

    // Send as text message on target channel
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = NODENUM_BROADCAST;
    p->channel = channelIndex;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    p->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    service->sendToMesh(p, RX_SRC_LOCAL, true);

    return ProcessMessage::CONTINUE;
}
