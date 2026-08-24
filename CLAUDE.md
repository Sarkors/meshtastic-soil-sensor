# meshtastic-soil-sensor — the field node firmware

A fork of Meshtastic for RAK4631 soil sensors. Everything upstream is upstream; what we add
is a soil sensor, a private wire format, and a small command module so a node can be
reconfigured over LoRa without opening its solar case.

Cross-repo context — how a command travels from the phone to here and back — is in the
**Navamesh** repo's `CLAUDE.md`. This file is about this repo.

Branch: **`raw-adc-private-app`**.

## Push to the metadavi fork, not the Sarkors upstream

Ours is **`metadavi/meshtastic-soil-sensor`**. `Sarkors/meshtastic-soil-sensor` is the
upstream we forked and is **read-only for us** — a push there 403s.

**Check the URL, not the remote name.** Remote names are local configuration and differ
per machine: one clone has both, with `myfork` = metadavi and `origin` = Sarkors; another
has only `origin` = metadavi, where pushing to `origin` is correct. A rule phrased as
"never push to origin" is wrong on the second machine.

```bash
git remote -v                       # confirm which name points at metadavi
git rev-parse --abbrev-ref @{u}     # what this branch already tracks
```

If the branch tracks the metadavi fork, plain `git pull` / `git push` is right.

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

**Flash is at 91.8%** (748,464 of 815,104 bytes as of `cff0bd52f`, ~65 KB headroom).
Worth checking after any addition.

## Things that will bite

**Flashing does not set the role. Nothing about a DFU flash provisions a node.**
The role lives in the saved config in LittleFS, which a `.uf2`/`.zip` flash does not
erase, so a node keeps whatever role it had — silently. `DEVICESTATE_CUR_VER` and
`DEVICESTATE_MIN_VER` are both `24` and this fork has never touched them, so no flash
between `459b09e`, `a36db94` and HEAD discards a saved config either.

The `SENSOR` default at `NodeDB.cpp:588` only applies where there is **no valid config to
load** — i.e. `factoryReset()`, or a first boot on erased prefs. `factoryReset()` then
calls `installRoleDefaults()`, so a reset on this firmware yields a fully provisioned
SENSOR. It has since `37274b4e7`, an ancestor of every current build, so a factory reset
here cannot produce CLIENT.

**That default is Sarkors', not ours** — `37274b4e7` is in `upstream/develop`, so nodes
first flashed with Sarkors firmware also came up SENSOR. Stock Meshtastic is what defaults
to `CLIENT`; do not read "upstream" here as Sarkors.

The likely history of a CLIENT node is therefore that it once ran **stock Meshtastic**
(where CLIENT is the default) and our firmware was flashed over the top. That also means
flashing alone will not clear `is_power_saving` on a node parked in deep sleep: only a
factory reset or an explicit role change re-applies the defaults.

**Role defaults are applied on role change, not just at factory reset.**
`AdminModule.cpp` calls `installRoleDefaults()` when the role differs, so setting `SENSOR`
switches on `environment_measurement_enabled`, `gps_mode = NOT_PRESENT`,
`rebroadcast_mode = NONE`, `is_power_saving = false`, and an 8-hour interval.

**But the sensor list is fixed at boot.** `EnvironmentTelemetry.cpp` returns early at init
when environment telemetry is disabled, so `AnalogSoilSensor` is never registered and
`getEnvironmentTelemetry()` iterates an empty list forever. A node switched to `SENSOR`
**must be rebooted** before it reports anything. A node left in `CLIENT` acks commands and
broadcasts NodeInfo while never sending a reading — it looks perfectly healthy.

**SENSOR with environment telemetry off is now self-repaired at boot** (`cff0bd52f`).
`loadFromDisk()` defaults `config` and `moduleConfig` **independently** and neither path
calls `installRoleDefaults()` — only `factoryReset()` does — while
`environment_measurement_enabled = true` is set in exactly one place in the file
(`installRoleDefaults`, SENSOR branch). So a corrupt or unreadable `moduleConfig` beside a
healthy config landed a node at role=SENSOR with the probe never registered: a node that
passes a role check and still reports nothing, which is worse than the CLIENT case. It is
reachable without a version bump — any `loadProto` failure does it. `loadFromDisk()` now
detects the pair, re-applies the role defaults and logs at warn level. Costs a stale
telemetry interval at worst, against a node that would otherwise stay silent until
someone opens its case.

**The ack used to echo the request rather than a read-back.** Fixed in `cff0bd52f`.
`handleReceivedProtobuf()` set `appliedLatitudeI = cmd->latitude_i`, so the ack could not
disagree with what was asked and a write that reported success without persisting was
undetectable from the app — we hit exactly that on a node whose nodeDB was corrupted,
which acked `ok=True` while continuing to broadcast a position 2 km away. A factory reset
fixed it.

`applySetLocation()` now re-fetches the node after writing and reports the stored
coordinates through out-params; a disagreement fails the ack while still carrying what is
actually stored, so the operator sees the node's answer instead of their own input. The
node is re-fetched deliberately rather than reusing the pointer just written through,
since a lookup returning the wrong entry or none is one of the failures worth catching.
A mismatch is **reported, not repaired** — retrying a write that just silently disagreed
would only produce the same ack again.

Note this catches a *storage* divergence, not a *broadcast* one. The `!0b9aed49` case in
the Pi's `TODO.md` — position persisted but old coordinates still broadcast — would still
need reproducing on a second node.

**`SET_LOCATION` must never be broadcast.** Every node would claim the same spot; the Pi
enforces this and the proto says so.

## Conventions

The comments here carry the reasoning, and several are load-bearing rather than
decoration: `applySetLocation()` records why it mirrors AdminModule's fixed-position path
instead of calling AdminModule (which would need an admin-channel session handshake — the
exact phone ceremony this command exists to avoid), and `applyTelemetryInterval()` records
why it deliberately avoids `MeshService::reloadConfig()` (it would reset radio config for a
telemetry change). Keep that style; do not compress them into restatements of the code.
