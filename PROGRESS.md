# PROGRESS — rampage-ps3

Running log. Newest session at the top.

---

## Session 1 — 2026-09-01 — from a `.rar` to a booting, rendering executable

### Extraction

The drop was a nested RAR: an outer archive containing a cover JPEG and a second
RAR, which in turn held two packages — `Rampage-World-Tour_Full.pkg` (89 MB) and
`Rampage-World-Tour_Crack-4.21.pkg` (2 MB). Only the first was touched; the
"crack" package is a DRM-circumvention payload, is not needed for a static
recompilation, and was left alone.

`pkg_extract.py` then failed outright:

```
AssertionError: could not decrypt file table with either keystream
```

The header explains why. `pkg_type` at offset `0x04` is `0x0000` — this is a
**debug / non-finalized** package, not a retail one, so it is XOR'd with a SHA-1
keystream rather than AES-128-CTR under the public GPKG key. The tool's debug
path hashed the raw `0x40` bytes at header offset `0x60`. RPCS3 instead builds
the hash input as the 16-byte QA digest arranged
`qa[0:8] qa[0:8] qa[8:16] qa[8:16]`, the rest zero, with the block index in the
last 8 bytes. Fixed upstream in `ps3recomp/tools/pkg_extract.py` — this was a
shared-tool bug that would have broken every non-finalized package, not just
this one.

Package metadata worth keeping:

- DRM type `3` (free) — no RAP or klicensee needed
- content type `5`, platform PS3, 330 table entries
- 311 real files, **89,157,922 B**, matching the package's declared data size

> One self-inflicted detour: piping the extractor through `head -40` killed it
> with a broken pipe partway through, and 22 MB of 89 MB looked like a complete
> extraction. Re-ran without the pipe.

### What the binary turned out to be

The working hypothesis going in was "this is probably just an emulator." It is —
and specifically it is Digital Eclipse / Backbone Entertainment's `emutech`
arcade emulation library, named directly in the EBOOT's assert strings under
`source/emutech/ZYTWunit/`, with a `TMS34010/source/machine.cpp` beneath it.

The decisive detail is that the ROM-name table is not Rampage-specific: `mk1`,
`mk2`, `umk3`, `narc`, `totalcarnage` and `RampageWorldTour` all appear. This
EBOOT is the generic Midway emulator; the game itself is the romset inside
`rwt.sr` (38,162,432 B).

That is structurally identical to the Simpsons Arcade PSN release — recompile
the emulator, let it interpret the ROM — which makes the already-playable
Simpsons port a direct oracle for this one.

`PARAM.SFO` also settles a question the title ID invites: `CATEGORY=HG`, so this
is a native PS3 HDD game, **not** a PS1 Classic running under Sony's PS1
emulator. Minimum firmware `00.93` marks it as a launch-window PSN title.

### Decrypt and lift

`rpcs3 --decrypt` produced `EBOOT.elf` with no RAP required.

- Entry OPD `0x00150500` → code `0x0001022C`, TOC `0x00161D18`
- `.text` at `0x0001022C`, size `0x0013B11C`
- `.lib.stub` import trampolines at `0x0014B36C`, size `0x1360`
- **`--code-end 0x0014C6CC`** — the end of the stub section, and the end of all
  executable content. Past it is `.rodata`, which the branch-target pass would
  otherwise explode into thousands of bogus functions.
- 4,869 OPD descriptors; `find_functions.py` detected 5,104 and verified that
  every OPD address is a function start; the lifter emitted **5,117**.
- 155 firmware imports across 13 libraries.

SPU was unexpectedly easy. `extract_spu_images.py` found exactly one image, at
guest `0x10017B80`, 54,272 B — and its strings identify it as SCEE's
**MultiStream 0.94** (`libmixer`, the `cellMS*` API), statically linked. It is a
genuine SPU ELF, so no `SPU_DUMP_MISS` capture run was needed, unlike the
Simpsons port's raw CRI job images. 637 functions lifted under the `msng_`
prefix; registered by FNV-1a-64 fingerprint `0xF92BC94C97BE3985`. Nothing
renders through the SPU here — this is audio only.

### Build

Modeled on `simpsons/CMakeLists.txt`. Two environment snags:

- `clang-cl` is not on `PATH`; it lives at `C:/Program Files/LLVM/bin/clang-cl.exe`.
- The link step needs the Windows SDK `rc.exe`, also not on `PATH`
  (`C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64`). Without it
  CMake reports the compiler as "broken" when it compiles fine and only the
  resource step fails.

Linked clean on the first attempt. 21 MB translation unit, 211 warnings, all of
them the lifter's benign `-Wparentheses-equality` on `bdnz` idioms.

### First boot

Better than expected. On the first run it reached the D3D12 backend and issued
draws, with a very clean HLE log — no missing modules, no unimplemented syscall
storms.

Highlights from the log:

- All 8 `cellSysmodule` loads resolve
- `cellGcmSys` init, 8 tiles, 2 display buffers, zcull, flip handler
- `cellVideoOut` configured 1280x720
- `cellAudio` opens two ports (2ch and 8ch)
- **`MULTISTREAM: SPU thread started successfully (Thread ID : 2000)`** — the
  lifted SPU mixer actually runs
- D3D12 backend up on the real adapter, pipelines and vertex buffers created

### The VFS trap

First run loaded no game assets at all:

```
[fs] open FAIL 'strings.tsv'          -> 'vfs/strings.tsv'
[fs] open FAIL 'fonts/Font_M.rf'      -> 'vfs/fonts/Font_M.rf'
[fs] open FAIL 'uiresource.txt'       -> 'vfs/uiresource.txt'
```

The title opens its content by **relative** path, not through `/dev_bdvd/...`.
The boot harness had rooted the VFS at `vfs/`, so everything resolved one
directory tree too high. Setting `PS3_VFS_ROOT=vfs/PS3_GAME/USRDIR` fixed it
with no code change, and the front-end immediately came to life: `strings.tsv`,
all three font sets, the Digital Eclipse and Midway logos, the legal screen, and
every `FE_IMAGES` / `PS3_IMAGES` texture.

Two harmless oddities noted along the way:

- `/dev_hdd0/cache.dat` is probed before *every* texture load and is absent on a
  first run — a texture cache the title recreates.
- `PS3_IMAGES/03_main_menu/main_menu_circles.texture.ps3` is requested but is
  genuinely not present in the package. A shipping bug in the title, not ours.

### Chasing the stall

The whole front-end loads, two draws go through, and then it parks. The watchdog
samples the same last HLE call at both 8 s and 15 s, so it is a spin, not a
crash.

Five NIDs were unresolved. Four fell out of a brute-force over the known export
names (the NID is a hash of the function name, so candidates can just be
enumerated and hashed):

| NID | Library | Function |
|---|---|---|
| `0xF83F8182` | `sys_io` | `cellPadSetPressMode` |
| `0xBE5BE3BA` | `sys_io` | `cellPadSetSensorMode` |
| `0x56DFE179` | `cellAudio` | `cellAudioSetPortLevel` |
| `0xDABBC2C0` | `sys_net` | `inet_addr` |
| `0x9117DF20` | `cellSysutil` | **`cellHddGameCheck`** |

`cellHddGameCheck` looked like the answer. It is asynchronous from the title's
point of view: firmware runs the title's `funcStat` callback and the boot state
machine waits on it, so an unregistered NID returns the fake `CELL_OK` default
*without ever running the callback* and the title waits forever.

RPCS3 defines `CellHddGameStatGet` / `StatSet` / `CBResult` as plain aliases of
the `CellGameData` ones, so the RPCS3-verified struct layout already in
`cellGameDataCheckCreate2` applies unchanged. Both entry points now share one
`gamedata_stat_callback()` helper in `libs/system/cellGame.c`; only the log tag,
the `isNewData` value and the error mapping differ.

It works — the callback fires and returns OK:

```
[cellGame] HddGameCheck(version=0 dir='NPUB30003' funcStat=0x00153378)
[cellGame] HddGameCheck: funcStat returned result=0
```

**It was not the blocker.** Still 2 draws. A real gap in the shared runtime,
closed for every title that calls it, but not this hang.

### The actual blocker

Counting what repeats gave the shape: **2 `DRAW_ARRAYS`, 2 `CLEAR_SURFACE`, then
59 iterations of `timer_usleep(2500 us)` from one fixed call site**,
`lr=0x000E4518`. Disassembling the containing function (`0x000E4240`) made it
plain:

```
000E4510  lwz  r3, -0x49E8(r2)
000E4514  bl   0x14BD4C          ; sys_lwmutex_unlock
000E4520  li   r3, 2500
000E4524  li   r11, 141          ; sys_timer_usleep
000E4528  sc
000E452C  lwz  r29, -0x49B8(r2)
000E4530  lwz  r0, 0(r29)        ; reload counter
000E4538  cmpwi cr7, r0, 0
000E453C  bgt  cr7, 0xE42D8      ; > 0 -> re-lock, go round again
000E4540  ...                    ; else: cellAudioPortStop, store -1, return
```

Take a lightweight mutex, work, release, sleep 2.5 ms, repeat while a counter
stays above zero — and the only exit calls **`cellAudioPortStop`**. This is an
audio drain/wait. The front-end is waiting for sound work to complete before
advancing past the logo sequence, and the counter never reaches zero.

That makes `cellAudioSetPortLevel` (unimplemented, silently returning a fake
`CELL_OK`) the prime suspect.

> **A diagnostic trap worth recording.** The watchdog prints
> `last HLE call = 0x1BC200F4 (cellSysutilGetSystemParamInt)`. NID `0x1BC200F4`
> is actually **`sys_lwmutex_unlock`** in `sysPrxForUser` — the runtime's
> watchdog name table mislabels it, and following the printed name leads into
> `cellSysutil` and away from the audio loop entirely. Trust the NID, not the
> label. Worth fixing upstream so the next port does not lose the same time.

### Next

1. Implement `cellAudioSetPortLevel`, `cellPadSetPressMode`, `cellPadSetSensorMode`
   and `inet_addr` — all small, all genuine gaps in the shared runtime.
2. Work out what decrements the counter at `-0x49B8(r2)` and why our `cellAudio`
   path never drives it to zero. That is the hang.
3. Fix the watchdog NID→name table so `0x1BC200F4` reports `sys_lwmutex_unlock`.
4. Get `rwt.sr` opening — nothing has touched the romset container yet, so the
   arcade emulator core has not started.
