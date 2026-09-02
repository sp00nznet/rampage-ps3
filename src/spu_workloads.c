/* spu_workloads.c -- register the title's lifted SPU binaries.
 *
 * Rampage World Tour has exactly one SPU program, and it is not the title's
 * own: SCEE's MultiStream 0.94 audio mixer (libmixer / cellMS*), linked
 * statically and living in the data segment at guest 0x10017B80. Unlike the
 * Simpsons Arcade port's CRI jobs it IS a real SPU ELF, so
 * extract_spu_images.py finds it in the EBOOT and no SPU_DUMP_MISS capture is
 * needed. Rendering does not go through the SPU here -- this is audio only.
 */
#include "spu_workload.h"

extern void spu_begin_image(int image_id);

extern void msng_spu_func_00000090(spu_context*);
extern void msng_spu_recomp_register(void);

void rampage_spu_register_all(void)
{
    /* SCEE MultiStream 0.94 mixer, guest EA 0x10017B80, 54272 bytes.
     *
     * The fingerprint is FNV-1a-64 over the image AS IT SITS IN GUEST MEMORY,
     * which is NOT the same as the file extract_spu_images.py writes out: same
     * 54,272 bytes, different content (the extractor reconstructs an ELF
     * wrapper). Fingerprinting the extracted file instead gives
     * 0xF92BC94C97BE3985 and the image silently never dispatches --
     * "is NOT in the workload registry ... 1 had no fallback". If this ever
     * drifts, the runtime prints the fingerprint it actually wants. */
    /* Entry is LS 0x90, NOT 0x80. find_spu_functions reports the text segment
     * as starting at 0x80, but that first "function" is a lone `stop` word of
     * padding -- registering it ran the mixer as: enter, stop, done (status
     * 0x2, mailbox never read, no reply). The image's real entry is the one
     * _sys_spu_image_import prints: "entry=0x00090". */
    spu_begin_image(1); msng_spu_recomp_register();
    spu_workload_register_img(0xE82F0FE56D1B967BULL, msng_spu_func_00000090,
                              1, "scee_multistream_0.94");
}

/* Registered at startup rather than from a hook in the shared boot harness, so
 * the harness stays title-agnostic (same trick build_spu_workloads.py emits). */
__attribute__((constructor)) static void rampage_spu_register_all_ctor(void)
{
    rampage_spu_register_all();
}
