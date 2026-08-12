#include "TelemetryRelay.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerStatus.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h" // pb_encode_to_bytes / pb_decode_from_bytes
#include <pb_decode.h>

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#include "Telemetry/Sensor/AnalogSoilSensor.h"
#endif

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
    // NOTE: the reading is encrypted with the channel PSK, so falling back to the
    // primary channel also means it leaves on a channel the Pi may not be listening on.
    LOG_WARN("TelemetryRelay: channel '%s' not found, falling back to primary", name);
    return 0; // fall back to primary if not found
}

// Wrap-safe uptime. DeviceTelemetryModule::getUptimeSeconds() is protected and its wrap
// counter only advances from inside that module, so we keep our own rather than editing
// upstream. Sampled once per telemetry cycle (<= 3 h), far below the ~49.7 day millis()
// wrap, so a wrap can never be missed in practice.
static uint32_t uptimeWrapCount = 0;
static uint32_t uptimeLastMs = 0;

static uint32_t relayUptimeSeconds()
{
    uint32_t now = millis();
    if (now < uptimeLastMs)
        uptimeWrapCount++;
    uptimeLastMs = now;
    return (0xFFFFFFFF / 1000) * uptimeWrapCount + (now / 1000);
}

ProcessMessage TelemetryRelayModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
    return ProcessMessage::CONTINUE;
#else
    // Only relay our own outgoing telemetry, not packets from other nodes
    if (mp.from != nodeDB->getNodeNum())
        return ProcessMessage::CONTINUE;

    // Decode the telemetry payload
    meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Telemetry_msg, &telemetry)) {
        return ProcessMessage::CONTINUE;
    }

    // Only the environment variant is our trigger. DeviceTelemetryModule also broadcasts
    // on TELEMETRY_APP (device_metrics / local_stats) and must not fire the relay.
    if (telemetry.which_variant != meshtastic_Telemetry_environment_metrics_tag)
        return ProcessMessage::CONTINUE;

    // The reading no longer travels inside the telemetry payload - the node writes no
    // EnvironmentMetrics field at all. Peek it from the sensor instead. Peeking does not
    // clear the pending flag; we only consume it after the packet is actually sent, so a
    // failed encode does not silently discard a measurement. If nothing is pending this
    // is an unrelated environment-telemetry loopback and must not produce a soil packet.
    uint16_t rawAdcSample = 0;
    if (!AnalogSoilSensor::peekReading(rawAdcSample))
        return ProcessMessage::CONTINUE;

    const uint32_t rawAdc = rawAdcSample;
    const uint8_t batPercent = powerStatus->getBatteryChargePercent();
    // getBatteryVoltageMv() is signed and defaults to -1 when unknown; clamp before the
    // unsigned cast so "unknown" cannot become 4294967295 on the wire.
    const int batMvSigned = powerStatus->getBatteryVoltageMv();
    const uint32_t batMv = (batMvSigned > 0) ? (uint32_t)batMvSigned : 0;
    const uint32_t uptimeSecs = relayUptimeSeconds();

    const uint8_t channelIndex = findChannelByName(RELAY_CHANNEL_NAME);

    // ---- 1. Authoritative packet: navamesh.SoilReading on PortNum 256 ---------------
    // Sent first so that if the TX queue is tight, the packet the Pi needs wins.
    navamesh_SoilReading reading = navamesh_SoilReading_init_zero;
    reading.raw_adc = rawAdc;
    reading.battery_percent = batPercent;
    reading.battery_mv = batMv;
    reading.uptime_seconds = uptimeSecs;

    LOG_INFO("TelemetryRelay: raw_adc=%u bat=%u%% %umV up=%us -> ch %d (%s) port %d", (unsigned)rawAdc,
             (unsigned)batPercent, (unsigned)batMv, (unsigned)uptimeSecs, channelIndex, RELAY_CHANNEL_NAME,
             meshtastic_PortNum_PRIVATE_APP);

    meshtastic_MeshPacket *pbPkt = router->allocForSending();
    if (!pbPkt) {
        LOG_WARN("TelemetryRelay: packet pool exhausted, dropping SoilReading");
        return ProcessMessage::CONTINUE; // reading stays pending
    }
    pbPkt->to = NODENUM_BROADCAST;
    pbPkt->channel = channelIndex;
    pbPkt->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    pbPkt->decoded.want_response = false;
    pbPkt->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    pbPkt->decoded.payload.size = pb_encode_to_bytes(pbPkt->decoded.payload.bytes, sizeof(pbPkt->decoded.payload.bytes),
                                                     &navamesh_SoilReading_msg, &reading);
    if (pbPkt->decoded.payload.size == 0) {
        // pb_encode_to_bytes logs and returns 0 on failure (it does not panic, despite
        // its header comment). Release the slot and leave the reading pending.
        LOG_ERROR("TelemetryRelay: SoilReading encode failed, dropping");
        packetPool.release(pbPkt);
        return ProcessMessage::CONTINUE;
    }
    service->sendToMesh(pbPkt, RX_SRC_LOCAL, true);

    // The reading has now been encoded and handed to the mesh. Consume it so that a
    // later unrelated TELEMETRY_APP loopback cannot retransmit this same stale ADC
    // value - only a fresh ADC measurement can produce another soil packet.
    AnalogSoilSensor::consumeReading();

    // ---- 2. Human-readable debug line (same transport as before, new content) -------
    char batStr[20];
    if (powerStatus->getIsCharging() || batPercent > 100) {
        snprintf(batStr, sizeof(batStr), "USB");
    } else {
        snprintf(batStr, sizeof(batStr), "%u%% (%.2fV)", (unsigned)batPercent, batMv / 1000.0f);
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "ADC: %u | Bat: %s | Up: %uh %um", (unsigned)rawAdc, batStr,
             (unsigned)(uptimeSecs / 3600), (unsigned)((uptimeSecs % 3600) / 60));

    LOG_INFO("TelemetryRelay: sending '%s' to channel %d (%s)", msg, channelIndex, RELAY_CHANNEL_NAME);

    meshtastic_MeshPacket *txt = router->allocForSending();
    if (!txt) {
        LOG_WARN("TelemetryRelay: packet pool exhausted, dropping debug text");
        return ProcessMessage::CONTINUE;
    }
    txt->to = NODENUM_BROADCAST;
    txt->channel = channelIndex;
    txt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    txt->decoded.payload.size = strlen(msg);
    memcpy(txt->decoded.payload.bytes, msg, txt->decoded.payload.size);
    txt->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    service->sendToMesh(txt, RX_SRC_LOCAL, true);

    return ProcessMessage::CONTINUE;
#endif
}
