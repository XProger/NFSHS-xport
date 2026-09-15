/* nullfunc.c -- RECONSTRUCTED from nfs4-f.exe (disasm-v3).
 *   obj NULLFUNC.ASM: asyncidle -- the async-engine idle hook.  A zero-result
 *   null function (the EXE body is `jr $ra; move $v0,$zero`).  Installed as the async idle/poll
 *   callback when no streaming work is pending.  Surfaced by the link harness
 *   (declared in lib/libfns.h, referenced by nsync.cpp / feaudio.cpp, never defined). */

/* ---- asyncidle [@0x800F6114] ---- */
/* int asyncidle(void) { return 0; } */
extern "C" int asyncidle(void)
{
    return 0;
}
