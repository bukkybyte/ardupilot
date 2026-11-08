# AP_PackAuth: battery-pack authentication for ArduPilot

A battery pack has to prove it is genuine before the flight controller will arm. Each pack also
carries a small record (serial, set, activation date, use count, max count) that you can read and
the aircraft can read and enforce.

This folder is **everything**: the Cube module, the pack firmware, the bench-test sketch, the
tools and this guide. It lives in `<ardupilot>/libraries/AP_PackAuth/` and is committed to your
ArduPilot fork; only the key stays out of git. (The build compiles only the `*.cpp` files directly
in this folder, so the sketches and tools below it are never part of the firmware.)

Built and tested on: **ArduPlane 4.6.3**, **CubeOrange** on a Hex Kore carrier, **Arduino Nano**
as the pack, **Arduino Uno** as a bench test master, I2C at 100 kHz. Packs were tested at 0x0B and 0x0C.

## What it does

- **Genuine-pack check.** The Cube sends a random challenge; the pack answers with SipHash-2-4 over
  (challenge + its record) using a shared 16-byte key. No key, no valid answer.
- **Not bypassable.** The check sits inside `AP_Arming::arm()`, so `ARMING_CHECK=0`, force-arm and
  `ARMING_REQUIRE=0` do not skip it. It does not depend on any `BATT_*` parameter.
- **Fresh proof.** Each pack is re-challenged every 3 s while disarmed, so an unplugged, tampered or
  swapped pack is noticed within seconds. Nothing touches the bus while armed.
- **Pack record.** serial (unique per pack), set id (shared by one set), activation date, use count,
  max count. A pack is refused once `count >= max_count`.
- **Use counting.** One use is added when the vehicle disarms after being armed at least
  `AP_PACKAUTH_MIN_FLIGHT_S` (default 30 s; 0 = every arm). The report is an authenticated, one-shot
  message, so it cannot be forged or replayed.
- **Sets.** Every required pack must carry the same set id.
- **1 to 5 packs per aircraft.** `ARMING_PKA_NUM` says how many; an optional signature (made with the
  key, bound to the Cube) stops anyone changing that number without the key.

## How it works

```
 PACK (Nano / MCU)                    I2C                 FLIGHT CONTROLLER (Cube)
 +---------------------+       +-------------+      +--------------------------------+
 | EEPROM record       | <---> | SDA, SCL    | <--> | AP_PackAuth thread (own task)  |
 |  serial, set, date  |       | 100 kHz     |      |  per pack: read INFO, send     |
 |  count, max         |       +-------------+      |  nonce, poll, verify, report   |
 | answers challenges  |                            |  use after a flight            |
 | counts reported uses|                            | AP_Arming::arm() -> gate_check |
 +---------------------+                            +--------------------------------+
```

### I2C protocol v2 (`smartpack_proto.h`, identical on every device)

| Reg | Dir | Content |
|---|---|---|
| 0x70 NONCE | write 8 | random challenge |
| 0x71 STATUS | read 1 | 0 idle, 1 busy, 2 ready (the pack never clock-stretches) |
| 0x72 RESPONSE | read 8 | `SipHash24(key, nonce + info[16])` |
| 0x73 INFO | read 16 | the pack record (below) |
| 0x74 USE | write 8 | `SipHash24(key, 0x55 + nonce + info[16])`, valid once, for the last challenge only |

INFO (little endian): `serial u16 | set_id u32 | activated u32 (YYYYMMDD) | count u16 | max_count u16 | flags u8 | reserved u8`.
All zeros while the pack is not provisioned.

### When arming is refused

| Message (`PreArm: ...`) | Meaning |
|---|---|
| `Pack auth: starting / task stalled / thread failed` | the authenticator thread is not running healthily |
| `Pack auth: bad pack address config` | `ARMING_PKA_ADDR + NUM - 1` is past 0x77 |
| `Pack auth: pack count not signed` | signed build: `ARMING_PKA_NUM` has no valid signature for this Cube |
| `Pack N: pack not found / no answer on I2C` | nothing answers at that slot's address |
| `Pack N: nonce write failed / pack not responding` | bus error / pack too slow |
| `Pack N: wrong response` | wrong key, tampered or counterfeit pack |
| `Pack N: pack not provisioned` | genuine pack with no serial, set or max count yet |
| `Pack N: life used up` | `count >= max_count` |
| `Pack N: other set` | pack N's set id differs from pack 1's |
| `Pack N: proof is stale` | last good answer is older than 8 s |

Other GCS lines: `PackAuth: N pack(s) required`, `Pack 1: #4660 set 100 use 12/500` (pack accepted),
`Pack 1: use 13/500` (flight counted), `Pack 1: use not saved` (5 tries failed).

## Folder contents

```
libraries/AP_PackAuth/
  AP_PackAuth.cpp/.h, AP_PackAuth_config.h   the Cube module (the only sources the build compiles)
  smartpack_proto.h                          the protocol header: single source of truth
  smartpack_key.h.example                    public dev key, as a template
  smartpack_key.h                            YOUR key - created by setup.sh, never committed (.gitignore)
  setup.sh                                   key / sync / test / install / sign   (bash setup.sh ...)
  nano/smartpack_nano/                       pack firmware (Arduino Nano) + pack-side logic
  uno/uno_multi_master/                      bench test master (Uno) for 1-5 packs
  tools/                                     apply_patch.py  sign_config.py  gen_key.py  protocol_test.cpp
  tools/sim/                                 runs the REAL module against a simulated pack, fake clock
```

## Setting up

### A. Your fork already has this on a branch (normal case)

```bash
git clone --recurse-submodules -b packauth https://github.com/<you>/ardupilot ~/ardupilot
cd ~/ardupilot/libraries/AP_PackAuth
cp /safe/place/smartpack_key.h .      # your key (the same one every pack uses); or: bash setup.sh key
bash setup.sh sync                    # key + protocol header into the Nano and Uno sketch folders
bash setup.sh test                    # host tests: must end with ALL TESTS PASSED
cd ~/ardupilot && ./waf configure --board CubeOrange && ./waf plane
```
Flash `build/CubeOrange/bin/arduplane.apj` (Mission Planner > Install Firmware > Load custom firmware).

### B. Add it to a clean ArduPilot tree

```bash
cd ~/ardupilot                         # Plane-4.6.3 checkout (anchors validated on that version)
unzip AP_PackAuth.zip -d libraries/   # or copy the folder to libraries/AP_PackAuth
cd libraries/AP_PackAuth
bash setup.sh key                      # random key (replaces the public dev key)
bash setup.sh install --dry-run        # shows what will be edited
bash setup.sh install                  # patches 4 files; run it again any time, it never doubles an edit
cd ~/ardupilot && ./waf configure --board CubeOrange && ./waf plane
```
`install` options: `--board <name>` (repeatable, default CubeOrange), `--unsigned` (plain `ARMING_PKA_NUM`,
no signature), `--release` (debug text off), `--dry-run`. It also recognises the same edits made by hand.
If an anchor is not found it makes no edit and prints the snippet to add yourself (exit code 2).
`bash setup.sh install /path/to/other/tree` copies this whole folder into another tree first.

### C. Commit and push to your fork

```bash
cd ~/ardupilot && git checkout -b packauth        # or the branch you already use
git add libraries/AP_PackAuth libraries/AP_Arming Tools/ardupilotwaf/ardupilotwaf.py \
        libraries/AP_HAL_ChibiOS/hwdef/CubeOrange/hwdef.dat
git status --short      # only smartpack_key.h.example may appear - no smartpack_key.h anywhere
git commit -m "AP_PackAuth: pack authentication" && git push -u origin packauth
```
Keep a copy of `smartpack_key.h` somewhere safe (password manager, encrypted drive): it is the only
thing the fork does not have.

## Packs, wiring, Cube

**Pack (Nano).** Open `nano/smartpack_nano` in the Arduino IDE (Nano, ATmega328P), upload, open the serial
monitor at 115200 and provision each pack once:

```
n 4660          serial (unique for every pack)
g 100           set id (the same for every pack of one set)
d 20261031      activation date YYYYMMDD
m 500           max count
a 12            I2C address (decimal, 8..119), applied after a reset; default 0x0B
p               print the record
```
Other commands: `c <count>` set the count (service/testing), `v <mV>`, `i <mA>`, `t` tamper (wrong response).

**Wiring** (Nano is 5 V, the Cube 3.3 V): SDA/SCL through a BSS138-type I2C level shifter (LV = 3.3 V,
HV = 5 V, common GND), pull-ups on both sides, 4.7-10 kOhm. Identify SDA/SCL on the carrier with a meter
(GND and +5 V are the other two pins). Never feed a 5 V line into the Cube side.

**Cube.** With the packs connected, read the boot messages: `PackAuth: count not signed, detecting`, then
`PackAuth: N pack(s) detected` and `PackAuth cfg-id: <24 hex>`. Sign the count for that Cube:

```bash
bash setup.sh sign <cfg-id> <N> > cube1.param
```
Load `cube1.param` in Mission Planner (Full Parameter List > Load from file > Write Params) and reboot.
You should see `PackAuth: N pack(s) required`, and the pack gate passes.

**Addresses.** Pack N is at `ARMING_PKA_ADDR + N - 1` (default 11 = 0x0B). Keep the range clear of other
devices on the bus (0x0C/0x0E are common compass addresses; 0x50-0x54 is usually free). `ARMING_PKA_BUS` -1
scans every bus for each pack; set it to the bus number once you know it. To replace a pack, give the spare
the dead pack's address (`a <addr>`), same set, unique serial. The Cube needs nothing: it sees the new serial
and proves the slot again within seconds.

## Configuration

Parameters (only in builds with `AP_PACKAUTH_PARAMS_ENABLED 1`):

| Parameter | Default | Meaning |
|---|---|---|
| `ARMING_PKA_BUS` | -1 | I2C bus of the packs; -1 = scan all buses per pack (reboot) |
| `ARMING_PKA_ADDR` | 11 | address of pack 1; pack N = ADDR + N - 1 (reboot) |
| `ARMING_PKA_NUM` | 1 | packs that must authenticate, 1..5 (reboot) |
| `ARMING_PKA_S0..S3` | 0 | signature of NUM for this Cube (signed builds only; from `sign`) |

Build switches (hwdef.dat `define NAME value`, or `-D`):

| Switch | Default | Meaning |
|---|---|---|
| `AP_PACKAUTH_ENABLED` | 0 | compile the module (the installer sets 1 for the board) |
| `AP_PACKAUTH_CFG_SIGNED` | 0 | require a signed pack count (installer default 1) |
| `AP_PACKAUTH_PARAMS_ENABLED` | 1 | 0 = bus/address/count fixed in firmware, no parameters |
| `AP_PACKAUTH_BUS_DEFAULT`, `_ADDR_DEFAULT`, `_NUM_DEFAULT` | -1, 0x0B, 1 | values when parameters are off, or their defaults |
| `AP_PACKAUTH_NUM_MIN` | 1 | floor: the count can never be set below this |
| `AP_PACKAUTH_MAX_PACKS` | 5 | slots built in |
| `AP_PACKAUTH_MIN_FLIGHT_S` | 30 | armed time that counts as a use; 0 = every arm |
| `AP_PACKAUTH_DEBUG` | 0 | status text every 5 s (installer default 1; use `--release` for 0) |

Timing (top of `AP_PackAuth.cpp`): rescan 2 s, retry 1 s, pack reply timeout 0.4 s, re-challenge 3 s,
proof valid 8 s, thread heartbeat 2 s, 5 attempts to report a use.

## Testing

| Test | How | Expect |
|---|---|---|
| Host | `bash setup.sh test` | `ALL TESTS PASSED`; the 40 s flight is counted, the 5 s one is not (30 s rule) |
| Nano self-test | boot the Nano with the dev key | `self-test response: 672EDC8C1FB809AF` (differs with your own key) |
| Pack alone | Uno with `uno_multi_master` (set `BASE_ADDR`, `NUM_PACKS`) | `OK` per pack; `t` on a Nano gives `MISMATCH` there only |
| Use report | Uno with `TEST_USE 1`, once | `use report: count N -> N+1` |
| Gate | Cube, any arm attempt without a good pack | `PreArm: Pack ...` even with `ARMING_CHECK=0` or force-arm |
| Counting | arm >= 30 s, disarm | `Pack 1: use N/500` (bench: build with `AP_PACKAUTH_MIN_FLIGHT_S 0`) |
| Signed count | change `ARMING_PKA_NUM` by hand | `pack count not signed` |

## Before real flights

1. `bash setup.sh key` and rebuild: the shipped dev key is public. Changing it later means redoing every
   pack and every signed Cube.
2. Set the ATmega lock bits on the packs so the key cannot be read out over ISP.
3. Build with debug off (`bash setup.sh install --release`, or `define AP_PACKAUTH_DEBUG 0`); optionally
   `AP_PACKAUTH_PARAMS_ENABLED 0` with the bus, address and count fixed.
4. Re-run the gate, tamper and unplug tests on the final build.

## Resource use (estimates from the code, not measurements)

Flash a few KB, RAM about 0.5 KB plus a 3 KB thread stack, CPU negligible (a hash takes microseconds),
I2C about 4-5 ms per pack every 3 s, nothing in flight. Measure on your build: compare the build size with
`AP_PACKAUTH_ENABLED` 0 and 1, `freemem` in the GCS, and `PM` (Load, MaxT) in a log.

## Limits

- The use counter is only as strong as the pack: whoever can rewrite the pack's EEPROM can reset it (the `c` command does).
- A flight that ends in a power cut before disarm is not counted.
- Anyone who can flash firmware can remove the gate; for that you need signed firmware / secure boot.
- Packs must sit at consecutive addresses, and a spare must be given the failed pack's address.
- The signature is only as secret as the key, and the key is in the firmware image.

## Roadmap

CAN transport (the nonce, MAC, record and gate stay; only the I2C reads and writes change), an I2C
multiplexer or coding pins for address-free pack swaps, binding the set id into the signed config.
