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

extern void msng_spu_func_00000080(spu_context*);
extern void msng_spu_recomp_register(void);

void rampage_spu_register_all(void)
{
    /* SCEE MultiStream 0.94 mixer, guest EA 0x10017B80, 54272 bytes. */
    spu_begin_image(1); msng_spu_recomp_register();
    spu_workload_register_img(0xF92BC94C97BE3985ULL, msng_spu_func_00000080,
                              1, "scee_multistream_0.94");
}

/* Registered at startup rather than from a hook in the shared boot harness, so
 * the harness stays title-agnostic (same trick build_spu_workloads.py emits). */
__attribute__((constructor)) static void rampage_spu_register_all_ctor(void)
{
    rampage_spu_register_all();
}
