# meshtastic-soil-sensor — the field node firmware

A fork of Meshtastic for RAK4631 soil sensors. Everything upstream is upstream; what we add
is a soil sensor, a private wire format, and a small command module so a node can be
reconfigured over LoRa without opening its solar case.

Cross-repo context — how a command travels from the phone to here and back — is in the
**Navamesh** repo's `CLAUDE.md`. This file is about this repo.

Branch: **`raw-adc-private-app`**.

## Push to `myfork`, never `origin`

`origin` is `Sarkors/meshtastic-soil-sensor` and is **read-only for us** — a push there
403s. Ours is `myfork` (`metadavi/meshtastic-soil-sensor`).

## What we added

| File | Does |
|---|---|
| `proto/navamesh/navamesh.proto` | the private wire format |
| `src/mesh/generated/navamesh/navamesh.pb.{h,cpp}` | generated, **committed** |
| `src/modules/NavameshCommand.{h,cpp}` | receives commands, applies them, acks |
| `src/modules/Telemetry/Sensor/AnalogSoilSensor.{h,cpp}` | reads the probe |
| `src/modules/TelemetryRelay.{h,cpp}` | sends the SoilReading |
| `bin/regen-navamesh-proto.sh` | regenerates the pb files (needs nanopb 0.4.9.1) |

The generated protobufs are committed deliberately, so a plain `pio run` needs no nanopb
toolchain. Only run the regen script if you change the `.proto` — and if you do, the
**Navamesh repo carries a byte-identical copy** at `src/navamesh/proto/navamesh.proto`.
Change both or the two ends stop agreeing.

Three portnums, deliberately separate rather than multiplexed onto one: **256**
SoilReading (node→Pi), **258** NavameshCommand (Pi→node), **259** NavameshAck (node→Pi).
The proto file explains why at length — the receiving module's portnum filter does all the
discrimination, so there is no decode-order rule and `SoilReading` stays byte-identical to
what deployed nodes already send.

## Building and flashing

```bash
pio run -e rak4631
```

Artifacts land in `.pio/build/rak4631/`:

- **`.zip`** — OTA/BLE DFU. This is what the Meshtastic app and nRF Connect want.
- **`.uf2`** — USB drag-and-drop onto the bootloader volume.
- `.hex` — SWD programmer.

Filenames carry the git short hash (`firmware-rak4631-2.7.20.a36db94.zip`), and **the same
hash appears in the firmware version string** the node reports — which is the only way to
tell what a node is actually running. Two builds hours apart with adjacent-looking hashes
cost a long debugging session here: `459b09e` predates `SET_LOCATION`, `a36db94` has it,
and a node flashed with the wrong one returns `ok=False` in a way indistinguishable from a
value being rejected.

**Flash is at 91.8%** (748,032 of 815,104 bytes), ~65 KB headroom. Worth checking after any
addition.

## Things that will bite

**Role defaults are applied on role change, not just at factory reset.**
`AdminModule.cpp` calls `installRoleDefaults()` when the role differs, so setting `SENSOR`
switches on `environment_measurement_enabled`, `gps_mode = NOT_PRESENT`,
`rebroadcast_mode = NONE`, `is_power_saving = false`, and an 8-hour interval.

**But the sensor list is fixed at boot.** `EnvironmentTelemetry.cpp` returns early at init
when environment telemetry is disabled, so `AnalogSoilSensor` is never registered and
`getEnvironmentTelemetry()` iterates an empty list forever. A node switched to `SENSOR`
**must be rebooted** before it reports anything. A node left in `CLIENT` acks commands and
broadcasts NodeInfo while never sending a reading — it looks perfectly healthy.

**The ack echoes the request, not a read-back.** `handleReceivedProtobuf()` sets
`appliedLatitudeI = cmd->latitude_i` after a successful apply, so the ack cannot disagree
with what was asked. A write that reports success but does not persist is undetectable
from the app — we hit exactly that on a node whose nodeDB was corrupted, which acked
`ok=True` while continuing to broadcast a position 2 km away. A factory reset fixed it.
Reading the stored position back before echoing would make this visible.

**`SET_LOCATION` must never be broadcast.** Every node would claim the same spot; the Pi
enforces this and the proto says so.

## Conventions

The comments here carry the reasoning, and several are load-bearing rather than
decoration: `applySetLocation()` records why it mirrors AdminModule's fixed-position path
instead of calling AdminModule (which would need an admin-channel session handshake — the
exact phone ceremony this command exists to avoid), and `applyTelemetryInterval()` records
why it deliberately avoids `MeshService::reloadConfig()` (it would reset radio config for a
telemetry change). Keep that style; do not compress them into restatements of the code.
