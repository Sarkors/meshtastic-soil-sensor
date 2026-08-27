#include "TelemetryRelay.h"
#include "MeshService.h"
#include "NavameshCommand.h" // isQuietModeActive()
#include "NodeDB.h"
#include "PowerStatus.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h" // pb_encode_to_bytes

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
#include "Telemetry/Sensor/AnalogSoilSensor.h"
#endif

TelemetryRelayModule *telemetryRelayModule;

// Name of the channel to relay telemetry to
#define RELAY_CHANNEL_NAME "navamesh"

// How often the thread checks whether AnalogSoilSensor has produced a reading.
//
// This is a lightweight flag check, NOT a sensor measurement interval. The ADC is sampled
// only by EnvironmentTelemetryModule on the configured telemetry.environment_update_interval;
// this value only bounds how long a finished reading waits before it goes out, and how
// quickly a failed send is retried.
#define RELAY_POLL_INTERVAL_MS 1000

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
// upstream. Polled far more often than the ~49.7 day millis() wrap, so a wrap is never missed.
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

int32_t TelemetryRelayModule::runOnce()
{
#if MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
    return disable();
#else
    // Cheap flag check. Nothing to do until AnalogSoilSensor::getMetrics() has run, which
    // happens on the configured environment telemetry interval and nowhere else.
    if (AnalogSoilSensor::hasPendingReading())
        sendPendingReading(); // leaves the reading pending if it fails, so we retry below

    return RELAY_POLL_INTERVAL_MS;
#endif
}

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR
bool TelemetryRelayModule::sendPendingReading()
{
    // Quiet mode suppresses RF egress only. Returning false leaves the reading pending, so the
    // freshest value goes out on the first poll after quiet mode ends rather than being lost.
    // One guard here covers both the authoritative SoilReading and the debug text below.
    if (NavameshCommandModule::isQuietModeActive())
        return false;

    // Peek does not clear the pending flag; we consume only after a successful send.
    uint16_t rawAdcSample = 0;
    if (!AnalogSoilSensor::peekReading(rawAdcSample))
        return false;

    const uint32_t rawAdc = rawAdcSample;
    const uint8_t batPercent = powerStatus->getBatteryChargePercent();
    // getBatteryVoltageMv() is signed and defaults to -1 when unknown; clamp before the
    // unsigned cast so "unknown" cannot become 4294967295 on the wire.
    const int batMvSigned = powerStatus->getBatteryVoltageMv();
    const uint32_t batMv = (batMvSigned > 0) ? (uint32_t)batMvSigned : 0;
    const uint32_t uptimeSecs = relayUptimeSeconds();

    const uint8_t channelIndex = findChannelByName(RELAY_CHANNEL_NAME);

    // ---- 1. Authoritative packet: navamesh.SoilReading on PortNum 256 ---------------
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
        LOG_WARN("TelemetryRelay: packet pool exhausted, retry SoilReading next poll");
        return false; // reading stays pending
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
        LOG_ERROR("TelemetryRelay: SoilReading encode failed, retry next poll");
        packetPool.release(pbPkt);
        return false; // reading stays pending
    }
    service->sendToMesh(pbPkt, RX_SRC_LOCAL, true);

    // The reading has now been encoded and handed to the mesh. Consume it so the same
    // value can never be transmitted twice; only a fresh ADC measurement re-arms us.
    AnalogSoilSensor::consumeReading();

    // ---- 2. Human-readable debug line (optional, best effort) -----------------------
    // Deliberately after consumeReading(): the authoritative packet is already away, so a
    // failure here must not cause the reading to be resent on the next poll.
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
        return true; // the authoritative packet already went out
    }
    txt->to = NODENUM_BROADCAST;
    txt->channel = channelIndex;
    txt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    txt->decoded.payload.size = strlen(msg);
    memcpy(txt->decoded.payload.bytes, msg, txt->decoded.payload.size);
    txt->priority = meshtastic_MeshPacket_Priority_DEFAULT;
    service->sendToMesh(txt, RX_SRC_LOCAL, true);

    return true;
}
#endif
