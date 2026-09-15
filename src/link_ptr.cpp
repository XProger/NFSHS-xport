/* link_ptr.cpp -- AGGREGATION TU (Stage 2b part 2). Module-owned globals declared
 * `extern` but never defined; values recovered from NFS4.EXE.  Pointer tables are
 * SYMBOLICATED to string literals (no bare absolute VAs).  Five entries are NULL
 * pointers whose SYM object size was LUMPED with adjacent anonymous string data
 * (the trailing bytes belong to other, anonymous symbols) -> defined as the bare
 * (zero) pointer they actually are. */
#include "nfs4_types.h"

extern "C" {

/* ---- NULL pointers (image first-word = 0; runtime-assigned; SYM size lumped) ---- */
void        *Audio_gHeap;          /* @8013d500 (SYM 28B lumped: +amus/game/.bnk strings) */
Draw_tPixMap *CarIO_carPixMap;     /* @8013d71c (SYM 32B lumped: +plate1/plate2/blnk strings) */
char        *wordFile;             /* @8013d458 (SYM 84B lumped: +%s%s/S0.../N0... fmt strings) */
char        *smallShapeFile;       /* @8013d9d0 (SYM 12B lumped: +back string) */
DVECTOR     *Weather_gPrevPos;     /* @8013dbb0 (SYM 20B lumped: +0xffffffff sentinels) */

/* ---- string-pointer tables (symbolicated to literals; bytes from NFS4.EXE .rodata) ---- */
char *langFileName[6] = {          /* @8011e140 : language text-bundle filenames */
    (char *)"text.eng", (char *)"text.ger", (char *)"text.fre",
    (char *)"text.spa", (char *)"text.ita", (char *)"text.swe"
};
char *movienames[5] = {            /* @800529d8 : intro/demo XA streams */
    (char *)"NFS4TITL.XA", (char *)"DEMO1AV.XA", (char *)"DEMO2AV.XA",
    (char *)"DEMO3AV.XA",  (char *)"EADOLBY.XA"
};
char *shapeext = (char *)".psh";   /* @8013dd20 (SYM 8B lumped: +"%c") */

/* ---- plain initialised scalars/arrays (byte-exact from NFS4.EXE) ---- */
int stacksize = 0x8000;            /* @80136cd0 : 32 KiB */
int counter[4] = { 320, 384, 448, 512 };  /* @8011e09c : 0x140 stride-0x40 size table */

}
