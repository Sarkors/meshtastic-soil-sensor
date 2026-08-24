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
| `proto/navamesh/navamesh.options` | nanopb field sizing; firmware-side only, NOT copied to the Pi |
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
enforces this and the proto says so. The consequence worth knowing: it is the one command
with **no broadcast fallback**, so it inherits whatever reliability the link has.

On the bench that reliability was poor, and the reason is worth recording because it looks
like a firmware fault and is not. Commands and acks were dropped intermittently in *both*
directions: one broadcast was applied on the node (confirmed over serial) while its ack
never reached the gateway, and two unicasts never reached the node at all. It flapped rather
than settled -- a `setloc` succeeded and the next command 30 s later did not.

Not weak signal, the opposite: `rx_rssi` was **-10 to -16 dBm** where usable LoRa runs -40
to -120, i.e. three nodes and the gateway sitting on one desk, some on a shortened interval,
with command traffic on top. Near-field desense plus collisions. Do not read bench
unicast loss as a mesh problem; nodes spread across a farm are the normal case.

The operational rule that follows: a `setloc` timeout means *unknown*, not *failed*. Retry,
and confirm with `position` or `map <id>` rather than inferring from a missing ack.

## Reporting the firmware version (built 2026-08-24)

Every node says what build it runs, because nothing else in the system could: `meshtastic_User`
(what NodeInfo carries) has no version field, and `DeviceMetadata.firmware_version` is produced
only at `PhoneAPI.cpp:300` (local serial/BLE) and `AdminModule.cpp:1234` (admin-channel request,
needs the session handshake and `admin_channel_enabled`). A remote node never broadcasts it.

Three carriers, in order of how the Pi usually learns it:

1. **A boot announce, unsolicited.** `runOnce()`'s first tick queues
   `queueAck(NODENUM_BROADCAST, 0, GET_FIRMWARE_INFO, ...)` — `command_id 0` marks it
   unsolicited, the same marker quiet-mode self-expiry uses. A reflash always reboots, so this
   is exactly as fresh as the value can ever be, for one packet. **Jittered 5-35 s**
   (`NAVAMESH_BOOT_ANNOUNCE_*`), far wider than the 4 s ack jitter, because the case that
   matters is a fleet power-cycle or a rollout booting 18 nodes at once with no operator pacing
   them. The floor is not zero: it also holds the announce back until the radio has settled,
   since this is a once-per-boot packet with no retry.
2. **Every ack — load-bearing, not opportunistic.** See below: the announce is unrepeated, so
   this is what recovers a node the Pi missed. Free, since acks are already sent, and it also
   covers the case that started this: an `ok=False` from a node whose build predates a handler
   is indistinguishable from a value the handler rejected, which cost two diagnoses on
   2026-08-21.
3. **`GET_FIRMWARE_INFO` (type 6), on request.** Applies nothing; the ack is the whole response.
   The one safe probe — every other command mutates the node you are asking about. Safe to
   broadcast. Rarely needed, given (1).

**It carries the full version string, not a 4-byte hash** — `char firmware_version[20]`, sized
by `proto/navamesh/navamesh.options` (nanopb emits a `pb_callback_t` without it). Decided
2026-08-24 against an earlier note preferring the hash: that note's own airtime argument was
withdrawn in the same commit that made it ("negligible against the packet's fixed cost"), and
the ~11 bytes it saves buy nothing on a packet sent once per reboot. What the string buys is
that it is byte-identical to what the Meshtastic app shows, what serial prints, and the `.zip`
filename that was flashed — no conversion between the thing you flashed and the thing you are
reading. It also keeps `2.7.20`, which a bare git hash drops and which changes on an upstream
rebase.

**It is an operator observation, not a farmer feature.** Nothing about it reaches the app: no
button, and the gateway's farmer-facing `HELP_TEXT` does not mention it. A farmer needs
DRY/DAMP/WET. The operator's surfaces are `navamesh-cmd fwinfo`, the gateway's `firmware` and
`ophelp` verbs, and `mesh_nodes.metadata->>'firmware_version'`. `test_operator_surface.py` in
the Navamesh repo pins that separation.

**The ack refresh is load-bearing, not a nice-to-have.** The boot announce is a *single
unacknowledged broadcast on a lossy medium*: if the Pi is down when it fires, or it collides
with seventeen siblings during a fleet power-cycle, nothing ever repeats it — and the field
fleet runs **2 to 11.7 days between reboots**, so the Pi would stay blind about exactly the
node a rollout needs to account for. Because every ack carries the version, *any* command
refreshes it, which is how an operator resolves an unknown node without waiting for a reboot.
`fwinfo` is that with nothing else attached.

Note reboots are **not only reflashes** — power cycles, brownouts and watchdog resets all
trigger one. Harmless, since the announce is idempotent, but "we heard a version" does not
mean "this node was just flashed".

The Pi keeps **"never announced" distinguishable from a recorded value** rather than
defaulting to something that reads like an answer: `firmware_version` is NULL, and the
gateway's `firmware` view lists those nodes under "Not reported yet" instead of guessing.
Its knowledge is honestly "as of last boot or last command". `soil_raw IS NULL` independently
answers "flashed at all", which is a different question and still the right one for a
legacy → new rollout.

Flash cost: **+224 bytes**, 91.8% either way.

## Verified on the bench, 2026-08-23

**The legacy → new migration preserves config.** This was the open worry: `loadFromDisk()`
discards a saved `channelFile` whose version is below `DEVICESTATE_MIN_VER`, and
`installDefaultChannels()` does `memset(&channelFile, 0, ...)` -- which would silently wipe
the `navamesh` secondary at index 1 and its PSK, leaving a node that looks healthy and is
deaf to every command.

Tested rather than argued: a node was flashed to `efb7db11d` (the last percentage-text
commit, matching what the field nodes run), confirmed to hold `role: SENSOR` and `navamesh`
as SECONDARY at index 1, then flashed forward to the deployment build. Both survived intact,
`deviceStateVersion` 24 throughout. Consistent with the source: every legacy-era commit here
already carries `DEVICESTATE_CUR_VER 24`, so the discard branch cannot fire.

**The ack readback's disagreement branch is proven, not just compiled.** A genuinely
corrupted nodeDB cannot be conjured, so a throwaway build stored `latitudeI + 1000`. A
`setloc` to 26.291000 came back `ok=False` carrying `applied_lat 26.2911`, and a serial read
confirmed the node really held `262911000` -- the ack reported what was stored, not what was
asked. Reverting and reflashing returned `ok=True` with `262865000` exactly.

To repeat it: offset `pos.latitude_i` in `applySetLocation()` before the nodeDB write, build,
flash, send a `setloc`, then `git checkout` the file and reflash. The instrumented build
carries the **same version string** as the real one, since a dirty tree does not change the
git hash -- keep the artifact out of `dist/` and delete it afterwards.

## Conventions

The comments here carry the reasoning, and several are load-bearing rather than
decoration: `applySetLocation()` records why it mirrors AdminModule's fixed-position path
instead of calling AdminModule (which would need an admin-channel session handshake — the
exact phone ceremony this command exists to avoid), and `applyTelemetryInterval()` records
why it deliberately avoids `MeshService::reloadConfig()` (it would reset radio config for a
telemetry change). Keep that style; do not compress them into restatements of the code.
