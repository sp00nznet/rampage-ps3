#!/bin/sh
# Regenerate everything git-ignored: the lifted PPU tree, the lifted SPU tree
# and the HLE NID table. Run from the repo root.
# PS3RECOMP defaults to the sibling checkout.
set -e
PS3RECOMP="${PS3RECOMP:-../ps3recomp}"

# Loader pass: OPD table, guest image manifest, and the firmware import list
# (analysis/EBOOT.imports.json) that --hle-stubs consumes below.
python "$PS3RECOMP/tools/ppu_loader.py" game/EBOOT.elf -o analysis

python "$PS3RECOMP/tools/find_functions.py" game/EBOOT.elf --output analysis/functions.json

# --hle-stubs rewrites each import trampoline as ps3_hle_call(nid), so direct
# `bl` calls to an import dispatch to the HLE handler instead of running the
# literal stub (whose pointer table the recomp never fills).
#
# 0x14C6CC is the end of the .lib.stub trampoline section (0x14B36C + 0x1360)
# and the end of all executable content; past it is .rodata, which the
# branch-target pass would otherwise explode into bogus functions.
rm -rf src/recomp && mkdir -p src/recomp src/gen
python "$PS3RECOMP/tools/ppu_lifter.py" game/EBOOT.elf \
    --functions analysis/functions.json \
    --hle-stubs analysis/EBOOT.imports.json \
    --code-end 0x14C6CC \
    -o src/recomp

python "$PS3RECOMP/tools/gen_hle_nids.py" --all --out src/gen/ppu_hle_nids.cpp

# ---- SPU -------------------------------------------------------------------
# One image only, and it is Sony's: SCEE MultiStream 0.94 (libmixer), statically
# linked into the data segment at guest 0x10017B80. It is a real SPU ELF, so the
# extractor finds it in the EBOOT -- no SPU_DUMP_MISS capture run needed.
mkdir -p spu_dump
python "$PS3RECOMP/tools/extract_spu_images.py" game/EBOOT.elf -o spu_dump
SPU_ELF=spu_dump/spu_0000_at_00167B80.elf
# --auto-functions, NOT a positional input. spu_lifter.py treats a positional
# argument as a FLAT local-store blob and maps file offset -> LS address via
# --offset/--base; it does not read ELF program headers. Handing it this ELF
# directly lifted the ELF HEADER as code: .text lives at file 0x100 / LS 0x80,
# so LS 0x90 decoded the header word 0x00000004 as `stop 4` and the entry
# function came out as a single stop instruction. It fails SILENTLY -- 637
# functions are still reported lifted -- and the SPU halts instantly at every
# dispatch. --auto-functions runs find_spu_functions' ELF parse and lifts the
# real text segment.
rm -rf src/spu_gen/msng && mkdir -p src/spu_gen/msng
python "$PS3RECOMP/tools/spu_lifter.py" --auto-functions "$SPU_ELF" \
    --symbol-prefix msng_ -o src/spu_gen/msng
