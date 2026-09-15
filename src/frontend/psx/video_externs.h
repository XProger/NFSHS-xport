/* frontend/psx/video_externs.h - reconstructed extern decls (globals + lib prototypes)
 * referenced by VIDEO.CPP. NOT an original source header; added so the reconstructed
 * translation unit compiles standalone as C++. Signatures inferred from call sites. */
#ifndef _FRONTEND_PSX_VIDEO_EXTERNS_H_
#define _FRONTEND_PSX_VIDEO_EXTERNS_H_
#include "../../nfs4_types.h"
#include "../../lib/libfns.h"

/* --- globals --- */
extern "C" extern "C" char *Paths_Paths[];
extern "C" int   CF_DVLC;
extern int   screenbpp;
extern "C" int   timerhz;
extern "C" int   ticks;

/* --- eaclib / platform / mdec / stream prototypes --- */
extern "C" {
extern "C" { void  Platform_ResetDCTBuffer(void); }
intptr_t initmdec(int, int, int, int); /* SYM INT on MIPS; native pointer carrier */
void  restoremdec(intptr_t);
void  mdec(intptr_t, char *, int, int);
int   mdecdone(intptr_t);
void  mdecreset(void);
intptr_t STREAM_create(int, int, int, void *, int);
void  STREAM_setfilter(intptr_t, int, unsigned int, unsigned int, int);
unsigned int STREAM_queuefile(intptr_t, char *, int, int);
int   STREAM_bufferusage(intptr_t);
int   STREAM_state(intptr_t);
int   STREAM_isendofstream(intptr_t);
intptr_t STREAM_get(intptr_t, void *, int);
void  STREAM_release(intptr_t, intptr_t);
void  STREAM_kill(intptr_t);
void  STREAM_destroy(intptr_t);
}
#endif
