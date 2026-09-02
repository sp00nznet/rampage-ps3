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

### A wrong turn, corrected

The first read of the stall was that the front-end was stuck in an **audio drain
loop**: 2 draws, 2 clears, then 59 iterations of `timer_usleep(2500 us)` from one
fixed site (`lr=0x000E4518`), inside a function whose only exit calls
`cellAudioPortStop`.

That was wrong, and the log said so:

```
sys_ppu_thread_create tid=2 name="pcm" entry=0x00157B10
```

The `usleep` lines carry `cia=0x00157B10` - the same address. That loop is the
**"pcm" audio thread ticking normally at 400 Hz**, which is what a PCM thread is
supposed to do. It was never the hang; it was just the loudest thing in the log.

Two lessons worth keeping. First, "the most frequent line in the log" is not the
same as "the blocked thread" - a healthy 400 Hz worker will always out-shout a
thread that is silently asleep. Second, the watchdog's own label was actively
misleading: it prints

```
last HLE call = 0x1BC200F4 (cellSysutilGetSystemParamInt)
```

but NID `0x1BC200F4` is **`sys_lwmutex_unlock`** in `sysPrxForUser`. The name
table is wrong, and following the printed name leads into `cellSysutil` and away
from everything that mattered. Trust the NID, not the label.

### The actual blocker

Finding the *blocked* thread rather than the *busy* one settled it. The main
guest thread (tid 1) sits here:

```
[ppu] lv2_syscall 190 (stub)
[WAIT] event_queue_receive(q=2 timeout=0) tid=1 cia=0x00000000 lr=0x000BC740
```

lv2 syscall **190 is `sys_spu_thread_write_spu_mb`** - the PPU-to-SPU inbound
mailbox write. Main hands work to the MultiStream mixer through that mailbox and
then waits on event queue 2 for the SPU to report back; the SPU's outbound events
are bound to exactly that queue (`thread_connect_event tid=0x2000 queue=0x2`).

190 is the **only** SPU-thread syscall missing from an otherwise complete
registration table in `lv2_register.c`, and the outbound (SPU to PPU) direction
is fully implemented. So the message is dropped, the mixer is never told to do
anything, no completion event is posted, and main waits for a wake that cannot
come.

### Two fixes that had to land first

**The SPU was never running at all.** Before any of the above could even be
observed, the log said:

```
[SPU] thread tid=0x2000 image @0x10017B80 (54272 bytes)
      fp=0xE82F0FE56D1B967B is NOT in the workload registry
[SPU] group_start id=0x1000 (1 thread(s), none spawned: 0 ran synchronously, 1 had no fallback)
```

The registration used `0xF92BC94C97BE3985`, the FNV-1a-64 of the file
`extract_spu_images.py` wrote out. The runtime fingerprints the image **as it
sits in guest memory** - same 54,272 bytes, different content, because the
extractor reconstructs an ELF wrapper. Registering the runtime's fingerprint made
it spawn:

```
[SPU] group_start id=0x1000 tid=0x2000 entry=0x00000090 -> spawned host thread
[SPU] group_start id=0x1000 (1 host threads running, 0 instant)
```

Worth remembering for every future port: when a lifted SPU image "isn't in the
registry", the runtime prints the fingerprint it actually wants - use that one,
not the one computed offline from the extracted file.

**`sys_cond_signal_to` (lv2 syscall 110) was never registered.** It was the only
member of the condvar family missing - create, destroy, wait, signal and
signal_all were all there - so it fell through to the generic stub and returned
success without waking anyone. This title uses it:

```
[SIGNAL] cond_signal_to(cond=1 target_tid=1) tid=3 lr=0x000762E0
```

A missing *wait* primitive fails loudly. A missing *wake* primitive just
deadlocks whoever was waiting, which is far harder to spot. Implemented upstream;
it wakes all waiters rather than the named one (the cond struct holds a bare
condition variable with no waiter list), which can never fail to wake the
intended thread. Marked with a `ponytail:` comment naming the upgrade path.

### The HDD-game path

`cellHddGameCheck` now correctly reports that the HDD game directory exists - and
the title immediately acted on it, re-opening its resources from
`/dev_hdd0/game/NPUB30003/USRDIR/`:

```
[fs] open FAIL '/dev_hdd0/game/NPUB30003/USRDIR/strings.tsv'
[fs] open FAIL '/dev_hdd0/game/NPUB30003/USRDIR/uiresource.txt'
```

That is not a regression - it is the *correct* path. `CATEGORY=HG` means this
title installs to `/dev_hdd0/game/<TITLEID>/`, and the relative-path loads seen
earlier are only its first pass. Mirroring `USRDIR` under
`hdd0/game/NPUB30003/` (a directory junction, no 87 MB copy) cleared every
remaining resource failure. What is left is `cache.dat` (absent by design on a
first run) and `main_menu_circles.texture.ps3`, which is genuinely not shipped in
the package.

### Where it stands

Boots, renders, loads its entire front-end from the correct HDD path, spawns and
runs the lifted MultiStream SPU mixer. Blocks on one missing syscall.

### Session 1b - wiring the SPU mixer

**`sys_spu_thread_write_spu_mb` (syscall 190) implemented.** The word is
delivered by re-running the parked worker with it pre-loaded, which is the shape
the interpreter path already used for per-frame work descriptors. Run
synchronously: the caller's next move is `sys_event_queue_receive` on the queue
the worker replies to, and a host thread racing that is how completion events get
lost.

**The lifted path could never have woken the PPU anyway.**
`spu_run_lifted_job_abi` never set `ctx.spu_id`. Without it an outbound mailbox
word cannot be matched back to an lv2 SPU thread, so the completion event is
dropped on the floor. The interpreter path sets it and carries a comment saying
exactly why; the lifted path simply did not. Raw SPU threads now pass a
`spu_run_opts` carrying `spu_id`/`group_id`, the inbound mailbox word, and
park-on-empty-inbox. SPURS jobs pass `NULL` and behave exactly as before.

### The SPU lift was garbage, and it failed silently

With all that wired, the mixer still did nothing. An `SPU_WORKER_TRACE` run
summary gave the answer:

```
[worker] spu=0x2000 steps=0 status=0x2 pc=0x00000 inmbox(n=1) outmbox(n=0)
```

`status=0x2` is `STOPPED_BY_STOP`: the SPU ran and hit a `stop` instruction
immediately. Looking at the lifted entry explained why - it was one instruction
long:

```c
void msng_spu_func_00000090(spu_context* ctx) {
        ctx->stop_code = 0x4u; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); return;
}
```

`spu_lifter.py` does not parse ELF program headers. A positional input is a FLAT
local-store blob, with file offset mapped to LS address by `--offset`/`--base`.
This image's `.text` sits at file `0x100` / LS `0x80`, so passing the ELF
directly lifted the **ELF header itself as code**: LS `0x80` decoded the header
word `0x00000000` as `stop 0`, LS `0x90` decoded `0x00000004` as `stop 4`. Both
"entry points" were header bytes.

What makes this worth writing down is how quiet it is. The tool reported *637
functions lifted, 89.3% coverage* and exited 0. `find_spu_functions.py` had
parsed the ELF correctly and printed `Text segment: va=0x80 .. 0xCBD0`, so
every number on screen looked right. Nothing anywhere said the two tools
disagreed about what a file offset meant.

`--auto-functions <elf>` runs the same ELF parse `find_spu_functions` uses.
Re-lifted, the entry becomes real code:

```c
void msng_spu_func_00000090(spu_context* ctx) {
        ctx->gpr[8] = spu_ila(0x3FFD0);          /* stack top - a real SPU CRT entry */
        { ctx->pc = 0x94; g_spu_trampoline_fn = msng_spu_func_00000094; return; }
}
```

`tools/relift.sh` fixed to match.

> A related false trail: the runtime reported the image's fingerprint as
> `0xE82F0FE56D1B967B` while the extracted file hashes to `0xF92BC94C97BE3985`,
> which looked like the extractor rewriting bytes. It is not - the extracted file
> is **byte-identical** to the raw EBOOT slice. The guest image is patched in
> memory before `group_start` (a version word, most likely, given MultiStream's
> PPU/SPU version handshake). Register the fingerprint the runtime prints; lift
> from the file.

### Where it stands

```
[SPU] write_spu_mb tid=0x2000 val=0x0000FFDD -> re-running worker
[worker] spu=0x2000 halted=1 steps=12 status=0x0 inmbox(n=1) outmbox(n=0) outintr(n=0)
```

`halted=1` means the worker now reaches an idle channel poll and **parks**
cleanly - it is executing real lifted SPU code. But `inmbox(n=1)`: our command
word is still unread when it parks. The mixer idles on some *other* channel and
only consults the mailbox later in its protocol.

### Session 1c - the local store was never loaded

With the lift fixed and the mailbox syscall in place, the mixer still did
essentially nothing: 12 lifted control transfers and an immediate return
(`branch to LS 0 -- job complete`).

The cause turned out to be the plainest thing possible. `spu_elf_load_to_ls` is
called by the SPURS/workload dispatch paths before running a job, but the raw
`sys_spu_thread_*` path never called it, and this title never calls
`sys_spu_thread_write_ls` itself. Lifting supplies the **instructions**;
everything else the code needs - `.data`, `.rodata`, jump tables, the stack area
- lives in local store. The worker was executing real recompiled SPU code
against 256 KB of zeroes.

Loading the image into the thread's local store once at `group_start` (re-runs
must keep whatever the worker has built up, not reset it):

```
[SPU] thread tid=0x2000 local store loaded (entry 0x00090)
[worker] spu=0x2000 halted=1 steps=546 status=0x0 pc=0x00260
```

**12 -> 546** control transfers, and `pc` finally lands somewhere meaningful.

### A blind spot in the channel histogram

The worker showed zero channel activity even at `SPU_CHHIST=25`, which read as
"never talks to a channel". Two things were wrong with that reading.

First, `SPU_CHHIST` was hardcoded to dump every 2000 accesses, so a worker doing
a few hundred printed nothing whatsoever - "below threshold" and "no activity"
looked identical. It now takes an interval.

Second, and the actual explanation: `SPU_CHHIST` instruments `spu_wrch` and
`spu_rdch` but **not `spu_rchcnt`** - which is precisely where
`park_on_empty_inmbox` lives. A worker that polls `rchcnt(SPU_RdInMbox)`, finds
it empty and parks is completely invisible to the histogram. The mixer is doing
exactly that, and the absence of any `job complete` marker confirms it: it is
parking, not returning.

### Where it stands

The mixer inits properly and parks on an empty mailbox - the intended
persistent-worker behaviour. It just never writes an outbound word, so no
completion event reaches queue 2 and main stays blocked.

### Session 1d - the mixer comes alive

Two fixes, and the second one is the whole game.

**Block, do not park.** Park-and-restart could never work for a persistent
worker: local store survives a park but registers do not, so each re-run
re-executed 546 hops of init and consumed the next command as though it were a
startup parameter. The runtime already had the right mechanism - `spu_ch_wait` /
`spu_ch_wake` and the `g_spu_force_ch_block` switch that `spu_raw.c` sets for
exactly this shape ("a raw SPU on its own host thread against a PPU that pokes
its mailboxes"). Blocking inside `rdch` keeps the host thread's C stack alive, so
the SPU's register state survives the wait for free. The live context is
published per thread so `sys_spu_thread_write_spu_mb` can write the mailbox and
wake the worker where it stands.

That made the mixer reply for the first time:

```
[SPU] write_spu_mb tid=0x2000 val=0x0000FFDD -> live worker
[SPU->PPU] mbox deliver spu=0x2000 intr=0 val=0x00000001 -> q=3
[SPU->PPU] mbox deliver spu=0x2000 intr=1 val=0x2A000001 -> q=3
```

...to the wrong queue. Main was waiting on **q=2**.

**Route SPU events by SPU PORT.** The real signature is
`sys_spu_thread_connect_event(id, eq, et, spup)` - one queue per *port*, and a
thread commonly binds several. Our handler kept a single `connected_queue`, so
the second bind clobbered the first. It also stored `et` into a field named
`connect_spup` and never read the actual `spup` argument, which is why both binds
looked identical in the log. Printing r6 settled it instantly:

```
thread_connect_event tid=0x2000 queue=0x2 et=0x1 spup=0x2A   <- command/completion
thread_connect_event tid=0x2000 queue=0x3 et=0x1 spup=0x1    <- printf server
```

And the completion word the SPU had been sending all along was **`0x2A000001`** -
top byte `0x2A`, the destination port. lv2 encodes the target port in the high
bits; we were ignoring it and delivering everything to whichever queue was bound
last. Routing on `(value >> 24)`, with the first binding kept as the fallback for
words that are not port-addressed:

```
[SPU->PPU] mbox deliver spu=0x2000 intr=1 val=0x2A000001 -> q=2
```

**Draws went from 2 to 20**, RSX draws from 2 to 32, and the PPU/SPU exchange
became a real conversation across three distinct commands with a fifth game
thread joining in.

### Where it stands

A throughput mismatch, which is a much better problem than a deadlock. Game
thread 5 writes the same command in a tight loop and waits on queue 2 for each
reply:

```
[SPU] write_spu_mb tid=0x2000 val=0x102F2780 -> live worker
[WAIT] event_queue_receive(q=2 timeout=0) tid=5 cia=0x00153808
```

**42,562 mailbox writes, 32 replies.** The SPU inbound mailbox is only a few
words deep, so writes arriving faster than the worker drains them are being lost.

### Next

1. **Honour SPU mailbox depth.** `sys_spu_thread_write_spu_mb` currently always
   accepts the write; hardware reports a full mailbox instead, and the writer
   retries - which is exactly what thread 5 is already doing. Returning EBUSY on
   a full inbox should convert the flood into proper back-pressure.
2. Check whether every command actually warrants a reply, or whether the worker
   is replying correctly and only the dropped writes are missing.
3. Implement the four remaining NIDs: `cellAudioSetPortLevel`,
   `cellPadSetPressMode`, `cellPadSetSensorMode`, `inet_addr`.
4. Fix the watchdog NID-to-name table so `0x1BC200F4` reports
   `sys_lwmutex_unlock`.
5. Get `rwt.sr` opening - the romset container still has not been touched, so the
   arcade emulator core has not started.
