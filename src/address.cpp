/* address.cpp -- RECONSTRUCTED data TU (src nfs4\address.obj, tree root).
 *
 *   address.obj defines the FE-overlay LOAD-EXTENT pointers -- the start and end addresses of the
 *   bigBuf overlay buffer (bigbuf.cpp). Verified from nfs4-f.exe .rdata bytes:
 *       FrontStartAddress @0x80054D90  =  0x80010000  = &bigBuf[0]         (overlay load start)
 *       FrontEndAddress   @0x80054D94  =  0x80054548  = &bigBuf[0x44548]   (overlay load end)
 *   i.e. these two words ARE the address of bigBuf (start) and one-past-its-end. They also pin
 *   bigBuf's exact size: FrontEndAddress - FrontStartAddress = 0x44548 (= sizeof bigBuf).
 *   (The FE overlay loader DMAs each sub-overlay from CD into [FrontStartAddress, FrontEndAddress).)
 *
 *   Materialized as pointers into bigBuf so there are 0 bare absolute VAs. Unreferenced by the
 *   reconstructed game logic in this build, but the obj faithfully owns this .rdata. @0xVA breadcrumbs.
 */
#include "nfs4_types.h"
extern "C" char bigBuf[];   /* @0x80010000  FE-overlay buffer (defined in bigbuf.cpp) */

extern "C" char *FrontStartAddress = bigBuf;              /* @0x80054d90  = 0x80010000 = &bigBuf[0] */
extern "C" char *FrontEndAddress   = bigBuf + 0x44548;    /* @0x80054d94  = 0x80054548 = &bigBuf[0x44548] (one-past-end) */
/* end of address.cpp */
