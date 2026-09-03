# 🦍 rampage-ps3 — Static Recompilation

> Rampage World Tour (PS3 / PSN, `NPUB30003`) recompiled to a native PC executable — no emulator.

Built on [ps3recomp](https://github.com/sp00nznet/ps3recomp), the same harness as the
[flOw](https://github.com/sp00nznet/flow), [Twisted Metal](https://github.com/sp00nznet/twistedmetal)
and [Simpsons Arcade](https://github.com/sp00nznet/simpsons) PS3 ports.

---

## 🎯 Status

**Boots and plays its logo sequence on screen.** The Midway legal screen, the Backbone logo and the rest of the boot logos render; one logo is visibly wrong.

| Milestone | Status |
|---|---|
| Extract the PSN PKG | ✅ 311 files, 89,157,922 B |
| Decrypt EBOOT.BIN (SELF → ELF) | ✅ `rpcs3 --decrypt`, no RAP needed |
| Identify the engine | ✅ Digital Eclipse "emutech" arcade emulator |
| Function discovery + PPU lift | ✅ 5,104 detected → **5,117 lifted** |
| SPU lift | ✅ 1 image (SCEE MultiStream 0.94), 637 functions |
| SPU mixer executes real code | ✅ runs and parks at its idle poll |
| Build on the shared ps3recomp harness | ✅ links clean, 21 MB TU |
| First boot (recompiled CRT runs) | ✅ |
| Reach `main()` / CRT init | ✅ |
| D3D12 backend up | ✅ live NV4097 → D3D12 |
| MultiStream SPU thread runs | ✅ `SPU thread started successfully` |
| Front-end assets load | ✅ fonts, strings, logos, every menu texture |
| SPU mixer answers the PPU | ✅ live worker, real command/reply traffic |
| Boot logo sequence on screen | ✅ Midway legal, Backbone, others (one logo wrong) |
| Menus render | ⚠️ not reached yet |
| Input | ❌ not reached |
| Playable | ❌ not yet |

### Current status - the logo sequence plays

Confirmed on screen, not inferred from counters: the title boots into its logo
sequence and plays through it - the **Midway legal screen**, the **Backbone
logo**, and the rest of the boot logos. One logo does not render correctly; the
others look right.

That is the first visual confirmation the recompiled title is producing real
frames, and it lands well past the point where it used to deadlock.

Underneath it, the MultiStream mixer is a live persistent worker: it blocks
inside `rdch` on its own host thread, the PPU pokes its mailbox with
`sys_spu_thread_write_spu_mb`, and it wakes where it stood with registers intact,
works, and replies to the right queue.

Three consecutive runs of the same binary:

| run | draw log lines | RSX log lines | PPU threads | mailbox BUSY | worker re-runs |
|---|---|---|---|---|---|
| 1 | 10 | 10 | 5 | 1 | 0 |
| 2 | 10 | 10 | 5 | 1 | 0 |
| 3 | 24 (capped) | 32 (capped) | 5 | 2731 | 0 |

**Read that table carefully - those are log lines, not draws.** Both counters are
capped: the D3D12 one logs its first 20 calls then every 1000th, and the RSX one
stops after 32. So run 1 and run 2 really did issue only ten draws and stalled,
while run 3 saturated both caps - 24 lines means roughly *four thousand* draws,
which is the run that actually plays the logo sequence.

The spread between runs is therefore enormous (ten draws versus thousands), not
the mild 10-vs-24 jitter the raw numbers suggest. A saturating counter is not a
measurement.

`re-runs = 0` is the column that matters: the worker is always alive when the PPU
writes to it, so no command is ever delivered by restarting it from its entry.
Some runs stall after ten draws and others render the whole sequence, so
something downstream is still timing-dependent.

**Known visual bug:** one logo in the boot sequence draws incorrectly while the
rest are fine. Worth pinning down which one - a single bad logo among correct
ones usually means one texture format or swizzle case, not a broken pipeline.

> **A measurement lesson.** An earlier build reached 20 draws and looked like a
> win. It was not: the mailbox path was logging *every* write, and 42,562
> `fprintf`+`fflush` calls were slowing the PPU just enough for the SPU worker to
> win a race it normally lost. Removing the logging dropped it straight back to
> 2 draws. The real fix was a start handshake, not the logging. Any result that
> depends on how much you are printing is not a result.

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

- **`spu_lifter.py` does NOT parse ELF program headers.** A positional input is
  treated as a FLAT local-store blob, with file offset mapped to LS address via
  `--offset`/`--base`. This image's `.text` is at file `0x100` / LS `0x80`, so
  handing the ELF over directly lifted the **ELF header as code**: LS `0x90`
  decoded the header word `0x00000004` as `stop 4`, and the entry function came
  out as a single stop instruction. It fails *silently* - 637 functions are still
  reported lifted, and the SPU simply halts the instant it is dispatched. Use
  `--auto-functions <elf>` instead, which runs the same ELF parse
  `find_spu_functions.py` uses.
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

- `runtime/syscalls/lv2_register.c` - **worker start handshake.**
  `sys_spu_thread_group_start` now waits (bounded) for a spawned persistent
  worker to publish its live context before returning. Without it the PPU could
  call `sys_spu_thread_write_spu_mb` first, see a null context, and fall back to
  re-running the worker from its entry - which restarts init and swallows the
  command as a startup parameter.
- `runtime/syscalls/lv2_register.c` - **SPU mailbox back-pressure.** The inbound
  mailbox is a single slot and `spu_channel_write` overwrites unconditionally, so
  a PPU thread writing faster than the worker drained silently destroyed
  commands. `sys_spu_thread_write_spu_mb` now reports `CELL_EBUSY` on a full
  mailbox (and still wakes the worker, so back-pressure cannot become livelock),
  which is what hardware does and what the game's retry loop already expects.

- `runtime/syscalls/lv2_register.c` - **SPU thread events are routed per SPU
  PORT.** `sys_spu_thread_connect_event(id, eq, et, spup)` binds one queue per
  port and a thread commonly has several; the handler kept a single
  `connected_queue`, so a later bind clobbered an earlier one. MultiStream binds
  port `0x2A` to its command/completion queue and port `0x01` to its printf
  queue, and lv2 encodes the destination port in the **top byte** of the word the
  SPU sends back (`0x2A000001`). Every reply was going to the printf server while
  the PPU waited on the command queue. The handler also stored `et` into a field
  named `connect_spup` and never read the real `spup` argument at all.
- `runtime/syscalls/spu_lifted_fallback.c` - raw SPU workers now **block** on an
  empty mailbox (`g_spu_force_ch_block`, the same switch `spu_raw.c` uses) rather
  than parking. Blocking keeps the host thread's C stack alive, so the SPU's
  registers survive the wait; park-and-restart re-ran init from the entry and
  swallowed the next command as a startup parameter.

- `runtime/syscalls/lv2_register.c` - **a raw SPU thread's local store was never
  loaded.** The SPURS/workload dispatch paths call `spu_elf_load_to_ls` before
  running a job; the raw `sys_spu_thread_*` path did not, and a title that starts
  a plain SPU thread group does not write LS itself. Lifting supplies the
  instructions, but `.data`, `.rodata`, jump tables and the stack area all live
  in local store - so the worker ran against 256 KB of zeroes. Loading it took
  the MultiStream mixer from 12 lifted control transfers to 546.
- `runtime/spu/spu_channels.c` - `SPU_CHHIST` takes an interval
  (`SPU_CHHIST=25`); it was hardcoded to dump every 2000 accesses, so a
  short-lived worker never printed anything at all.

- `runtime/syscalls/lv2_register.c` - **`sys_spu_thread_write_spu_mb` (lv2
  syscall 190) implemented.** It was the only SPU-thread syscall missing from an
  otherwise complete registration table, so PPU-to-SPU mailbox words were
  silently dropped. Delivered by re-running the parked worker with the word
  pre-loaded.
- `runtime/spu/spu_lifted_job.h` - lifted SPU runs gained a `spu_run_opts` for
  RAW SPU THREADS (jobs pass `NULL` and are unaffected): inbound-mailbox
  pre-load, park-on-empty-inbox, and - importantly - **`spu_id`, which the lifted
  path never set**. Without it an outbound mailbox word cannot be matched back to
  an lv2 SPU thread, so the completion event is dropped and the PPU waits
  forever. The interpreter path already set it; the lifted path did not. Adds a
  one-per-run completion signal and an `SPU_WORKER_TRACE` run summary.

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
