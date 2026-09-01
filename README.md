# 🦍 rampage-ps3 — Static Recompilation

> Rampage World Tour (PS3 / PSN, `NPUB30003`) recompiled to a native PC executable — no emulator.

Built on [ps3recomp](https://github.com/sp00nznet/ps3recomp), the same harness as the
[flOw](https://github.com/sp00nznet/flow), [Twisted Metal](https://github.com/sp00nznet/twistedmetal)
and [Simpsons Arcade](https://github.com/sp00nznet/simpsons) PS3 ports.

---

## 🎯 Status

**Boots, renders, loads its whole front-end.** Stalls before the menu draws.

| Milestone | Status |
|---|---|
| Extract the PSN PKG | ✅ 311 files, 89,157,922 B |
| Decrypt EBOOT.BIN (SELF → ELF) | ✅ `rpcs3 --decrypt`, no RAP needed |
| Identify the engine | ✅ Digital Eclipse "emutech" arcade emulator |
| Function discovery + PPU lift | ✅ 5,104 detected → **5,117 lifted** |
| SPU lift | ✅ 1 image (SCEE MultiStream 0.94), 637 functions |
| Build on the shared ps3recomp harness | ✅ links clean, 21 MB TU |
| First boot (recompiled CRT runs) | ✅ |
| Reach `main()` / CRT init | ✅ |
| D3D12 backend up | ✅ live NV4097 → D3D12 |
| MultiStream SPU thread runs | ✅ `SPU thread started successfully` |
| Front-end assets load | ✅ fonts, strings, logos, every menu texture |
| Menus render | ❌ **stalls after 2 draws** |
| Input | ❌ not reached |
| Playable | ❌ not yet |

### Current blocker - a dropped SPU mailbox write

The title loads its entire front-end, issues **2 draws and 2 clears**, and then
the main thread blocks forever. The chain is now fully traced:

```
[SPU] group_start ... tid=0x2000 -> spawned host thread   <- MultiStream mixer running
[rwh] #1 img=1 ctx=... entry=...                          <- lifted SPU image ran
[ppu] lv2_syscall 190 (stub)                              <- sys_spu_thread_write_spu_mb: DROPPED
[WAIT] event_queue_receive(q=2 timeout=0) tid=1 lr=0x000BC740   <- main blocks, forever
```

Main hands work to the MultiStream SPU by writing its inbound mailbox
(`sys_spu_thread_write_spu_mb`, lv2 syscall **190**), then waits on event queue 2
for the SPU to report back. The SPU's outbound events are bound to that very
queue (`thread_connect_event tid=0x2000 queue=0x2`). Syscall 190 is the **only**
SPU-thread syscall missing from an otherwise complete registration table, so the
message is dropped, the mixer is never told to do anything, no completion event
is posted, and main waits for a wake that cannot come.

**Next step.** Implementing 190 needs a `tid -> spu_context*` registry: the
context is a stack local inside `spu_run_lifted_job_abi`, so nothing outside can
reach `ctx->ch_in_mbox` today. It also raises a real design question - the
MultiStream mixer is a *persistent worker* that blocks on `rdch SPU_RdInMbox`,
whereas the runtime's lifted-SPU model is job-shaped (enter, drain, finish). That
is a change to the SPU execution model, not a one-line syscall.

### Remaining unresolved NIDs

| NID | Library | Function |
|---|---|---|
| `0xF83F8182` | `sys_io` | `cellPadSetPressMode` |
| `0xBE5BE3BA` | `sys_io` | `cellPadSetSensorMode` |
| `0x56DFE179` | `cellAudio` | `cellAudioSetPortLevel` |
| `0xDABBC2C0` | `sys_net` | `inet_addr` |

`cellAudioSetPortLevel` is the only one of the title's 8 `cellAudio` imports not
covered; the other seven are implemented.

---

## 🧬 What This Binary Actually Is

The question that shaped the whole port: *is this just an emulator?* Yes — and
that is good news, because it is the same shape as the already-working
[Simpsons Arcade](https://github.com/sp00nznet/simpsons) port.

The EBOOT's assert strings name its own source tree:

```
../../../source/emutech/ZYTWunit/TMS34010/source/machine.cpp
../../../source/emutech/ZYTWunit/source/CSound.cpp
../../../source/emutech/ZYTWunit/source/DFSound.cpp
../../../source/emutech/ZYTWunit/source/Sound65.cpp
```

`emutech` is Digital Eclipse / Backbone Entertainment's arcade emulation library.
`ZYTWunit` is the Midway **Z / Y / T / W unit** hardware family, whose main CPU is
the **TMS34010** graphics processor. The `DFSound.cpp` asserts still reference
`WAVEFORMATEX` and `lpwfxFormat`, so the audio layer carries a DirectSound
lineage from the Windows/Xbox builds of the same library.

Better still, this EBOOT is the *generic* Midway emulator, not a
Rampage-specific one. Its ROM-name table covers the whole line:

```
mk1.rom  mk2.rom  umk3.rom  narc.rom  totalcarnage.rom  RampageWorldTour.rom
```

So the recompiled executable is an arcade emulator; the actual game is the
romset inside **`rwt.sr`** (38,162,432 B), which the emulator interprets. Exactly
the trick the Simpsons Arcade PSN release plays with `SIMPSONS.SR`, and exactly
why that port is a strong oracle for this one.

---

## 📺 The Game

| | |
|---|---|
| **Title** | Rampage World Tour |
| **Platform** | PlayStation 3 (PSN download) |
| **Title ID** | `NPUB30003` |
| **Content ID** | `UP0000-NPUB30003_00-RAMPAGEWRLDTR000` |
| **Category** | `HG` (native PS3 HDD game — *not* a PS1 Classic) |
| **Min firmware** | `00.93` — a launch-window PSN title |
| **Developer** | Digital Eclipse / Backbone Entertainment |
| **Publisher** | Midway |
| **Original** | 1997 Midway arcade game |

---

## 🔬 Binary Facts

| | |
|---|---|
| `EBOOT.BIN` (SELF) | 2,023,440 B |
| `EBOOT.elf` (decrypted) | 2,020,696 B |
| Machine | PPC64, big-endian, `ET_EXEC` |
| Entry OPD | `0x00150500` → code `0x0001022C`, TOC `0x00161D18` |
| `.text` | `0x0001022C`, size `0x0013B11C` |
| Import stubs (`.lib.stub`) | `0x0014B36C`, size `0x1360` |
| End of executable content | `0x0014C6CC` (the lifter's `--code-end`) |
| OPD descriptors | 4,869 |
| Functions detected / lifted | 5,104 / **5,117** |
| Firmware imports | 155 across 13 libraries |
| SPU images | 1 — SCEE MultiStream 0.94 @ guest `0x10017B80`, 54,272 B |

### Imports by library

`sceNp` 40 · `cellSysutil` 19 · `sys_io` 17 · `sys_net` 15 · `cellGcmSys` 14 ·
`sysPrxForUser` 13 · `cellAudio` 8 · `sys_fs` 8 · `cellSpurs` 7 · `cellNetCtl` 6 ·
`cellKey2char` 4 · `cellSysmodule` 2 · `cellRtc` 2

A lean surface: no `cellSail`, no `cellAtrac`, no `cellFont`. The 40 `sceNp`
imports are almost entirely `sceNpScore*` — online leaderboards — and are stubbed
for offline play. `cellKey2char` is there for the leaderboard name-entry field.

---

## 🛠️ Pipeline

```
  PSN PKG ──► EBOOT.BIN ──► EBOOT.elf ──► ppu_lifter ──► C++ ──► link ps3recomp ──► rampage.exe
              (SELF)        (decrypt)     (5,117 fns)           (shared harness + HLE)
                                              ▲
              MultiStream SPU ELF ── spu_lifter ┘   (a real ELF in the data segment,
              @ 0x10017B80                          so no runtime capture needed)
```

Unlike the Simpsons port, the SPU side is trivial here: the only SPU program in
the binary is Sony's own MultiStream audio mixer, it is a genuine SPU ELF sitting
in the data segment, and `extract_spu_images.py` finds it directly — no
`SPU_DUMP_MISS` capture run. Nothing renders through the SPU.

## 📦 Building

Prereqs: Python 3.9+, CMake 3.20+, **clang-cl** + Ninja (the shared harness uses
`__builtin_bswap` and weak symbols MSVC lacks), the Windows SDK `rc.exe` on
`PATH`, and a sibling [ps3recomp](https://github.com/sp00nznet/ps3recomp) checkout.

```bash
# 1. Extract your own legally-obtained PKG and decrypt the EBOOT:
python ../ps3recomp/tools/pkg_extract.py your.pkg extracted
cp extracted/USRDIR/EBOOT.BIN game/
rpcs3 --decrypt game/EBOOT.BIN          # -> game/EBOOT.elf, no RAP needed

# 2. Lay the game data out the way the harness expects:
mkdir -p vfs/PS3_GAME && cp extracted/PARAM.SFO vfs/PS3_GAME/
cp -r extracted/USRDIR vfs/PS3_GAME/ && cp game/EBOOT.elf vfs/PS3_GAME/USRDIR/

# 3. Lift the PPU + SPU images and generate the HLE NID table:
PS3RECOMP=../ps3recomp ./tools/relift.sh

# 4. Build. Release is the default and it matters — an unoptimised build of the
#    21 MB recompiled translation unit runs at a fraction of the speed.
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build

# 5. This is a CATEGORY=HG title, so it installs to /dev_hdd0/game/<TITLEID>/.
#    Mirror USRDIR there (a junction avoids copying 87 MB):
mkdir -p hdd0/game/NPUB30003 && cp extracted/PARAM.SFO hdd0/game/NPUB30003/
#    Windows:  New-Item -ItemType Junction -Path hdd0/game/NPUB30003/USRDIR \
#                        -Target vfs/PS3_GAME/USRDIR
#    POSIX:    ln -s ../../../vfs/PS3_GAME/USRDIR hdd0/game/NPUB30003/USRDIR

# 6. Run:
PS3_VFS_ROOT=vfs/PS3_GAME/USRDIR PS3_HDD0_ROOT=hdd0 \
  ./build/rampage.exe vfs/PS3_GAME/USRDIR/EBOOT.elf
```

### Gotchas found the hard way

- **The title loads its content twice, by two different paths.** Early boot uses
  RELATIVE paths (`strings.tsv`, `fonts/Font_M.rf`, `uiresource.txt`), so
  `PS3_VFS_ROOT` must point at `USRDIR`. Then, once `cellHddGameCheck` reports the
  HDD game directory exists, it re-opens everything under
  `/dev_hdd0/game/NPUB30003/USRDIR/` - which is the *correct* path for a
  `CATEGORY=HG` title. Both layouts have to be present.
- **A lifted SPU image is fingerprinted as it sits in GUEST MEMORY**, not as the
  file `extract_spu_images.py` writes out. Same byte count here, different
  content, so registering the file's fingerprint left the mixer silently
  undispatched (`is NOT in the workload registry ... 1 had no fallback`). The
  runtime prints the fingerprint it actually wants - use that one.
- **This PKG is a *debug* (non-finalized) package** (`pkg_type=0x0000`), not a
  retail one, so it uses the SHA-1 keystream rather than AES-CTR. `pkg_extract.py`
  had that keystream wrong; fixed upstream (see below).
- **`PS3_IMAGES/03_main_menu/main_menu_circles.texture.ps3` is not in the PKG.**
  The title asks for it and it simply is not shipped. Not our bug.
- **`/dev_hdd0/cache.dat` is probed before every single texture load** and is
  expected to be absent on a first run.

## 🔧 Upstream fixes made for this port

- `runtime/syscalls/sys_cond.c` - **`sys_cond_signal_to` (lv2 syscall 110) was
  never registered**, the only member of the condvar family missing, so it hit
  the generic stub and returned success without waking anyone. A missing *wait*
  primitive fails loudly; a missing *wake* primitive just deadlocks the waiter.
  This title uses it (`cond_signal_to(cond=1 target_tid=1)`).
- `libs/system/cellGame.c` - **`cellHddGameCheck` implemented.** It is async:
  firmware runs the title's `funcStat` callback and the boot state machine waits
  on it, so an unregistered NID hung any title calling it. RPCS3 aliases the
  `CellHddGame*` structs to the `CellGameData` ones, so both entry points now
  share one `gamedata_stat_callback()` helper.

- `ps3recomp/tools/pkg_extract.py` — the debug/non-finalized keystream hashed the
  raw 0x40 bytes at header `0x60`. RPCS3 instead builds a 0x40-byte block as
  `qa[0:8] qa[0:8] qa[8:16] qa[8:16]` + zeros with the block index in the last 8
  bytes. Every non-finalized PKG failed with *"could not decrypt file table with
  either keystream"* before this.

## ⚖️ Legal

This repository contains **no copyrighted game code, assets, binaries, or
encryption keys** — only analysis notes, configuration, and recompilation
tooling. You must supply your own legally obtained copy of the game. `pkg/`,
`extracted/`, `game/`, `vfs/` and every generated source tree are git-ignored.

## 🔗 Related

- [ps3recomp](https://github.com/sp00nznet/ps3recomp) — the PS3 HLE runtime this links against
- [simpsons](https://github.com/sp00nznet/simpsons) — **the closest sibling: the same Backbone arcade-emulator-wrapper trick, already playable**
- [flOw](https://github.com/sp00nznet/flow) · [tokyojungle](https://github.com/sp00nznet/tokyojungle) — sister PS3 ports
- [RPCS3](https://github.com/RPCS3/rpcs3) — emulator whose HLE research makes this possible
