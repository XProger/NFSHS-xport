/* Native Win32 replacement for the PsyQ surface used by NFSHS.
 *
 * This file owns the Windows boundary. Game/frontend code is left untouched;
 * STR/MDEC remain out of scope, while timing, input, host files, GPU commands
 * and SPU state have real semantics.
 */
#define WIN32_LEAN_AND_MEAN
#include "diagnostics.h"

enum { PSYQ_GPU_VRAM_WIDTH = 1024, PSYQ_GPU_VRAM_HEIGHT = 512 };
extern "C" {
extern unsigned short PsyQ_gpu_vram[PSYQ_GPU_VRAM_WIDTH * PSYQ_GPU_VRAM_HEIGHT];
int PsyQGpuClearImage(int, int, int, int, unsigned char, unsigned char, unsigned char);
int PsyQGpuLoadImage(int, int, int, int, const unsigned short *);
int PsyQGpuStoreImage(int, int, int, int, unsigned short *);
int PsyQGpuMoveImage(int, int, int, int, int, int);
unsigned short PsyQGpuGetClut(int, int);
unsigned short PsyQGpuGetTPage(int, int, int, int);
void PsyQPadInitDirect(unsigned char *, unsigned char *);
void PsyQPadStart(void);
void PsyQPadStop(void);
int PsyQPadGetState(int);
void PsyQPadPublishDigital(int, int, unsigned short);
void PsyQSpuAdvance(unsigned int);
int waveout_is_running(void);
}
#define OpenEventA Win32_OpenEventA
#define EnterCriticalSection Win32_EnterCriticalSection
#define ExitCriticalSection Win32_ExitCriticalSection
#define LoadImageA Win32_LoadImageA
#include <windows.h>
#include <mmsystem.h>
#undef OpenEventA
#undef OpenEvent
#undef EnterCriticalSection
#undef ExitCriticalSection
#undef LoadImageA
#undef LoadImage
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct Car_tObj;
extern int gNumSlices;
void AILife_PlaceCarAtLocation(Car_tObj *,int,int,int,int,int);

/* The PSX heap reuses blocks between frontend and game phases.  Keep the host
 * arena low-addressed and 16-byte aligned, but retain the original delete/free
 * semantics through an in-arena free list. */
enum { HOST_OBJECT_ARENA_SIZE = 512 * 1024 * 1024 };
static unsigned char *gHostObjectArena;
static unsigned int gHostObjectArenaUsed;
static unsigned int gHostObjectFreeHead = 0xffffffffU;

static int NFSHS_HostInitMemory(void) {
    if (!gHostObjectArena) {
        gHostObjectArena = (unsigned char *)VirtualAlloc(0, HOST_OBJECT_ARENA_SIZE,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!gHostObjectArena) return 0;
    }
    return 1;
}

static uintptr_t g_host_packet_begin;
static uintptr_t g_host_packet_end;
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_dma_seen[32768];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_dma_seen_generation[32768];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_dma_seen_index[32768];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_dma_seen_predecessor[32768];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_dma_generation;
#endif
static uintptr_t g_host_added_prims[16384];
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_added_ots[16384];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_added_callers[16384];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_added_links[16384];
#endif
static unsigned int g_host_added_generation[16384];
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_prev_added_prims[16384];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_prev_added_ots[16384];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_prev_added_callers[16384];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_prev_added_generation[16384];
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_prev_add_generation;
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_all_added_prims[32769];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_all_added_ots[32769];
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_all_added_callers[32769];
#endif
static unsigned int g_host_add_generation;
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_last_added_prim;
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_last_added_end;
#endif
#if NFSHS_DIAGNOSTICS
static unsigned int g_host_packet_overlap_count;
#endif
#if NFSHS_DIAGNOSTICS
static uintptr_t g_host_addprim_caller_override;
#endif
#if NFSHS_DIAGNOSTICS
static int g_host_dma_tag_fault_seen;
#endif
extern "C" void NFSHS_HostLog(const char *, ...);
struct HostOtRange { uintptr_t begin, end; };
static HostOtRange g_host_ot_ranges[16];
static unsigned int g_host_ot_range_count;

#if NFSHS_DIAGNOSTICS
/* Diagnostic history may forget provenance, never stall gameplay.
   Index 32768 is a permanently empty sentinel for a full-table miss. */
static unsigned int host_history_slot(uintptr_t key)
{
    unsigned int slot=((unsigned int)key>>2)&32767u;
    for(unsigned int n=0;n<32768;n++,slot=(slot+1)&32767u)
        if(!g_host_all_added_prims[slot] || g_host_all_added_prims[slot]==key)
            return slot;
    return 32768;
}
#endif

static int host_dma_address_is_current(unsigned int low24)
{
#if NFSHS_DIAGNOSTICS

    if (low24 == 0x00ffffffu) return 1;
    low24 &= 0x00fffffcu;
    for (unsigned int i=0; i<g_host_ot_range_count; ++i) {
        unsigned int begin=(unsigned int)(g_host_ot_ranges[i].begin & 0x00ffffffu);
        unsigned int end=(unsigned int)(g_host_ot_ranges[i].end & 0x00ffffffu);
        if (low24 >= begin && low24 < end) return 1;
    }
    /* A packet link is valid only after that packet was inserted this frame.
       Merely falling inside the arena is insufficient: it can point at stale
       bytes in the unused tail and still look like a plausible GPU packet. */
    for (unsigned int i=0; i<16384; ++i) {
        if (g_host_added_generation[i] == g_host_add_generation &&
            ((unsigned int)g_host_added_prims[i] & 0x00fffffcu) == low24)
            return 1;
    }
    return 0;

#else
    return 0;
#endif
}

/* PSX DMA links retain only address bits 0..23.  Static HUD primitives and
   heap OT nodes can live in different 16 MiB host regions with the same
   low bits.  Resolve a primitive through the exact AddPrim observation for
   this frame before attempting any high-bit reconstruction. */
static uintptr_t host_added_prim_resolve(unsigned int low24)
{
    low24&=0x00fffffcu;
    unsigned int slot=(low24>>2)&16383u;
    unsigned int first=slot;
    while(g_host_added_generation[slot]==g_host_add_generation) {
        if(((unsigned int)g_host_added_prims[slot]&0x00fffffcu)==low24)
            return g_host_added_prims[slot];
        slot=(slot+1)&16383u;
        if(slot==first) break;
    }
    return 0;
}

static void host_check_linked_tags(uintptr_t observer)
{
#if NFSHS_DIAGNOSTICS

    if (g_host_dma_tag_fault_seen) return;
    for (unsigned int i=0; i<16384; ++i) {
        if (g_host_added_generation[i] != g_host_add_generation) continue;
        unsigned int actual=*(unsigned int *)g_host_added_prims[i]&0x00ffffffu;
        if (actual != g_host_added_links[i]) {
            g_host_dma_tag_fault_seen=1;
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("FIRST_DMA_TAG_MUTATION prim=%p owner=%p ot=%p expected=%06x actual=%06x observer=%p",
                (void*)g_host_added_prims[i],(void*)g_host_added_callers[i],
                (void*)g_host_added_ots[i],g_host_added_links[i],actual,(void*)observer));
            return;
        }
    }

#else

#endif
}

struct HostAllocHeader {
    unsigned int magic;
    unsigned int size;
    unsigned int span;
    unsigned int nextFree;
};

enum { HOST_ALLOC_MAGIC = 0x4846534eU }; /* "NSFH" little endian */
enum { HOST_FREE_MAGIC = 0x45455246U };  /* "FREE" little endian */

static HostAllocHeader *host_header_from_offset(unsigned int offset) {
    return (HostAllocHeader *)(gHostObjectArena + offset);
}

extern "C" void *NFSHS_HostAlloc(unsigned int size) {
    if (!NFSHS_HostInitMemory()) return 0;
    unsigned int payload = (size ? size : 1);
    if (payload > 0xffffffffU - (unsigned int)sizeof(HostAllocHeader) - 15U) return 0;
    unsigned int span = (sizeof(HostAllocHeader) + payload + 15U) & ~15U;
    unsigned int *freeLink = &gHostObjectFreeHead;
    HostAllocHeader *h = 0;
    while (*freeLink != 0xffffffffU) {
        HostAllocHeader *candidate = host_header_from_offset(*freeLink);
        if (candidate->magic == HOST_FREE_MAGIC && candidate->span >= span) {
            unsigned int remainder = candidate->span - span;
            h = candidate;
            if (remainder >= sizeof(HostAllocHeader) + 16U) {
                unsigned int splitOffset = *freeLink + span;
                HostAllocHeader *split = host_header_from_offset(splitOffset);
                split->magic = HOST_FREE_MAGIC;
                split->size = 0;
                split->span = remainder;
                split->nextFree = candidate->nextFree;
                *freeLink = splitOffset;
                h->span = span;
            } else {
                *freeLink = candidate->nextFree;
            }
            break;
        }
        freeLink = &candidate->nextFree;
    }
    if (!h) {
        if (span > HOST_OBJECT_ARENA_SIZE - gHostObjectArenaUsed) return 0;
        h = (HostAllocHeader *)(gHostObjectArena + gHostObjectArenaUsed);
        h->span = span;
        gHostObjectArenaUsed += span;
    }
    h->magic = HOST_ALLOC_MAGIC;
    h->size = size;
    h->nextFree = 0xffffffffU;
    void *p = h + 1;
    memset(p, 0, h->span - sizeof(*h));
    return p;
}

extern "C" void NFSHS_HostFree(void *p) {
    if (!p || !gHostObjectArena) return;
    HostAllocHeader *h = (HostAllocHeader *)p - 1;
    uintptr_t headerAddress = (uintptr_t)h;
    uintptr_t arenaAddress = (uintptr_t)gHostObjectArena;
    if (headerAddress < arenaAddress ||
        headerAddress + sizeof(*h) > arenaAddress + gHostObjectArenaUsed ||
        h->magic != HOST_ALLOC_MAGIC || h->span < sizeof(*h) ||
        h->span > gHostObjectArenaUsed - (unsigned int)(headerAddress - arenaAddress)) return;

    unsigned int offset = (unsigned int)(headerAddress - arenaAddress);
    unsigned int *freeLink = &gHostObjectFreeHead;
    HostAllocHeader *previous = 0;
    while (*freeLink != 0xffffffffU && *freeLink < offset) {
        previous = host_header_from_offset(*freeLink);
        freeLink = &previous->nextFree;
    }
    h->magic = HOST_FREE_MAGIC;
    h->size = 0;
    h->nextFree = *freeLink;
    *freeLink = offset;

    if (h->nextFree != 0xffffffffU) {
        HostAllocHeader *next = host_header_from_offset(h->nextFree);
        if (offset + h->span == h->nextFree && next->magic == HOST_FREE_MAGIC) {
            h->span += next->span;
            h->nextFree = next->nextFree;
        }
    }
    if (previous) {
        unsigned int previousOffset = (unsigned int)((unsigned char *)previous - gHostObjectArena);
        if (previousOffset + previous->span == offset && previous->magic == HOST_FREE_MAGIC) {
            previous->span += h->span;
            previous->nextFree = h->nextFree;
        }
    }
}

extern "C" unsigned int NFSHS_HostAllocationSize(void *p) {
    if (!p) return 0;
    HostAllocHeader *h = (HostAllocHeader *)p - 1;
    return h->magic == HOST_ALLOC_MAGIC ? h->size : 0;
}

extern "C" unsigned int NFSHS_HostAvailable(void) {
    unsigned int available = HOST_OBJECT_ARENA_SIZE - gHostObjectArenaUsed;
    unsigned int offset = gHostObjectFreeHead;
    while (offset != 0xffffffffU) {
        HostAllocHeader *h = host_header_from_offset(offset);
        if (h->magic != HOST_FREE_MAGIC) break;
        available += h->span;
        offset = h->nextFree;
    }
    return available;
}

extern "C" int NFSHS_HostAllocatorSelfTest(void) {
    unsigned int before = NFSHS_HostAvailable();
    unsigned char *a = (unsigned char *)NFSHS_HostAlloc(64);
    unsigned char *b = (unsigned char *)NFSHS_HostAlloc(96);
    unsigned char *c = (unsigned char *)NFSHS_HostAlloc(128);
    if (!a || !b || !c) return 1;
    if (((uintptr_t)a & 15U) || ((uintptr_t)b & 15U) || ((uintptr_t)c & 15U)) return 2;
    if (NFSHS_HostAllocationSize(a) != 64 || NFSHS_HostAllocationSize(b) != 96 ||
        NFSHS_HostAllocationSize(c) != 128) return 3;
    memset(b,0xa5,96);
    NFSHS_HostFree(b);
    unsigned char *replacement = (unsigned char *)NFSHS_HostAlloc(48);
    if (replacement != b || NFSHS_HostAllocationSize(replacement) != 48) return 4;
    for (unsigned int i=0; i<48; ++i) if (replacement[i] != 0) return 5;
    NFSHS_HostFree(replacement);
    NFSHS_HostFree(a);
    NFSHS_HostFree(c);
    if (NFSHS_HostAvailable() != before) return 6;
    unsigned char *combined = (unsigned char *)NFSHS_HostAlloc(240);
    if (combined != a) return 7;
    NFSHS_HostFree(combined);
    if (NFSHS_HostAvailable() != before) return 8;
    return 0;
}

struct PsxRect { short x, y, w, h; };
struct PsxDispEnv { PsxRect disp, screen; unsigned char isinter, isrgb24, pad0, pad1; };
struct PsxDrawEnv {
    PsxRect clip; short ofs[2]; PsxRect tw; unsigned short tpage;
    unsigned char dtd, dfe, isbg, r0, g0, b0; unsigned long dr_env[16];
};
struct PsxVector { long vx, vy, vz, pad; };
struct PsxSVector { short vx, vy, vz, pad; };
struct PsxMatrix { short m[3][3]; long t[3]; };
struct PsxCdlLoc { unsigned char minute, second, sector, track; };
struct PsxCdlFile { PsxCdlLoc pos; unsigned long size; char name[16]; };

static HWND g_window;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_qpc_start;
static void (*g_vsync_callback)(void);
static int g_video_mode;
static int g_geom_screen = 256;
static PsxMatrix g_rotation;
#if NFSHS_DIAGNOSTICS
static int g_host_log_fd = -2;
#endif
static unsigned short g_pad_buttons;
static char g_auto_sequence[256];
static int g_auto_initialized, g_auto_active, g_auto_index;
static DWORD g_auto_stage_start;
static int g_host_race_gas;
static int g_host_race_started;
static unsigned long long g_host_timer_ticks;
static unsigned long long g_host_fast_vsync_count;
static int g_host_fast_test = -1;
static int g_host_test_verbose = -1;
static int g_host_timer_resolution_active;
#if defined(AP_WIN) && NFSHS_DIAGNOSTICS
/* Temporary deterministic-input harness.  The table is generated from the
 * raw digital-pad packets captured from DuckStation, not reconstructed from
 * steering/throttle observations. */
struct HostDuckReplayRun {
    unsigned short firstGameTick;
    unsigned short lastGameTick;
    unsigned short keys;
};
#include "duckstation_half_lap_input.inc"
static const HostDuckReplayRun *g_host_duck_replay_data=g_host_duck_replay_runs;
static unsigned g_host_duck_replay_count=sizeof(g_host_duck_replay_runs)/sizeof(g_host_duck_replay_runs[0]);
static HostDuckReplayRun g_host_duck_external_runs[4096];
static int g_host_duck_replay_initialized;
static int g_host_duck_replay_enabled;
static int g_host_duck_replay_finished;
static unsigned int g_host_duck_replay_run;
#endif
extern "C" unsigned int tmrint(void);
extern "C" void NFSHS_HostPadPoll(void);
extern "C" int NFSHS_HostCurrentGameTicks(void);
extern "C" int libticks;
extern "C" int gTicks;
extern "C" void NFSHS_HostLog(const char *, ...);
static void ensure_window(void);
static void pump_messages(void);

static void host_restore_timer_resolution(void)
{
    if (g_host_timer_resolution_active) {
        timeEndPeriod(1);
        g_host_timer_resolution_active=0;
    }
}

static void host_request_timer_resolution(void)
{
    if (!g_host_timer_resolution_active && timeBeginPeriod(1)==TIMERR_NOERROR) {
        g_host_timer_resolution_active=1;
        atexit(host_restore_timer_resolution);
    }
}

static int host_fast_test_enabled(void)
{
#if NFSHS_DIAGNOSTICS

    if (g_host_fast_test < 0) {
        char value[2];
        g_host_fast_test = GetEnvironmentVariableA("NFSHS_TEST_FAST",value,sizeof(value)) != 0;
    }
    return g_host_fast_test;

#else
return 0;
#endif
}

static int host_test_verbose_enabled(void)
{
#if NFSHS_DIAGNOSTICS

    if (g_host_test_verbose < 0) {
        char value[2];
        g_host_test_verbose = GetEnvironmentVariableA("NFSHS_TEST_VERBOSE",value,sizeof(value)) != 0;
    }
    return g_host_test_verbose;

#else
return 0;
#endif
}

/* Optional deterministic render schedule used only by OT replay tests.  The
 * listed ticks are direct DrawOTag observations from DuckStation.  Before the
 * first listed tick native startup/render history remains untouched. */
extern "C" int NFSHS_HostShouldRenderTick(int gameTick)
{
#if NFSHS_DIAGNOSTICS

    static int initialized;
    static int enabled;
    static int first_tick=0x7fffffff;
    static unsigned char ticks[65536];
    if (!initialized) {
        char path[MAX_PATH];
        initialized=1;
        if (GetEnvironmentVariableA("NFSHS_RENDER_TICKS",path,sizeof(path))) {
            int fd=_open(path,_O_RDONLY|_O_BINARY);
            if (fd>=0) {
                char buffer[4096];
                int value=0,have=0,count;
                while ((count=_read(fd,buffer,sizeof(buffer)))>0) {
                    for (int i=0;i<count;i++) {
                        unsigned char ch=(unsigned char)buffer[i];
                        if (ch>='0' && ch<='9') {
                            value=value*10+(ch-'0');
                            have=1;
                        } else if (have) {
                            if ((unsigned int)value<sizeof(ticks)) {
                                ticks[value]=1;
                                if (value<first_tick) first_tick=value;
                            }
                            value=0;
                            have=0;
                        }
                    }
                }
                if (have && (unsigned int)value<sizeof(ticks)) {
                    ticks[value]=1;
                    if (value<first_tick) first_tick=value;
                }
                _close(fd);
                enabled=first_tick!=0x7fffffff;
            }
        }
    }
    if (!enabled) return 1;
    if (gameTick<first_tick) return 1;
    return (unsigned int)gameTick<sizeof(ticks) && ticks[gameTick]!=0;

#else
return 1;
#endif
}

static DWORD host_automation_millis(void)
{
    if (host_fast_test_enabled())
        return (DWORD)((g_host_timer_ticks * 1000ULL) / 128ULL);
    return GetTickCount();
}

#ifdef AP_WIN
static int host_duck_replay_keys(int gameTick, unsigned short *keys)
{
#if NFSHS_DIAGNOSTICS

    if (!g_host_duck_replay_initialized) {
        char value[2];
        g_host_duck_replay_enabled =
            GetEnvironmentVariableA("NFSHS_DUCK_REPLAY",value,sizeof(value)) != 0;
        g_host_duck_replay_initialized = 1;
        char path[MAX_PATH];
        if(GetEnvironmentVariableA("NFSHS_DUCK_REPLAY_FILE",path,sizeof(path))) {
            FILE *file=fopen(path,"r");unsigned first,last,keys,count=0;
            if(!file)ExitProcess(94);
            while(fscanf(file,"%u %u %x",&first,&last,&keys)==3) {
                if(count>=4096 || first>last || last>65535 || keys>65535 ||
                   (count && first!=g_host_duck_external_runs[count-1].lastGameTick+1))ExitProcess(94);
                g_host_duck_external_runs[count].firstGameTick=(unsigned short)first;
                g_host_duck_external_runs[count].lastGameTick=(unsigned short)last;
                g_host_duck_external_runs[count++].keys=(unsigned short)keys;
            }
            if(!feof(file)||!count)ExitProcess(94);
            fclose(file);g_host_duck_replay_data=g_host_duck_external_runs;
            g_host_duck_replay_count=count;g_host_duck_replay_enabled=1;
        }
        if (g_host_duck_replay_enabled)
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("DUCK_REPLAY armed records=%d runs=%u ticks=%u..%u",
                          HOST_DUCK_REPLAY_SOURCE_RECORDS,
                          g_host_duck_replay_count,
                          (unsigned)g_host_duck_replay_data[0].firstGameTick,
                          (unsigned)g_host_duck_replay_data[g_host_duck_replay_count-1].lastGameTick));
    }
    if (!g_host_duck_replay_enabled || !g_host_race_started) return 0;

    /* The capture is indexed by simGlobal.gameTicks (the original 64 Hz game
       clock), so repeated PAD polls in one tick must return the same packet. */
    if (gameTick < g_host_duck_replay_data[0].firstGameTick) {
        *keys = 0xffff;
        return 1;
    }
    while (g_host_duck_replay_run + 1 <
               g_host_duck_replay_count &&
           gameTick > g_host_duck_replay_data[g_host_duck_replay_run].lastGameTick)
        ++g_host_duck_replay_run;
    const HostDuckReplayRun *run=&g_host_duck_replay_data[g_host_duck_replay_run];
    if (gameTick <= run->lastGameTick) {
        *keys=run->keys;
    } else {
        *keys=0xffff;
        if (!g_host_duck_replay_finished) {
            g_host_duck_replay_finished=1;
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("DUCK_REPLAY finished tick=%d",gameTick));
        }
    }
    return 1;

#else
return 0;
#endif
}
#endif

extern "C" int NFSHS_HostTestRaceRequested(void)
{
#if NFSHS_DIAGNOSTICS

    char value[2];
    return GetEnvironmentVariableA("NFSHS_TEST_RACE",value,sizeof(value)) != 0;

#else
return 0;
#endif
}

/* NFSHS_TEST_RACE is deliberately parsed at the platform boundary.  The game
 * receives ordinary frontend selections and still uses Front_BuildStream and
 * GameSetup_StartUp, exactly like a menu-started race. */
extern "C" int NFSHS_HostTestRaceParam(const char *name, int fallback)
{
#if NFSHS_DIAGNOSTICS

    char config[512];
    DWORD length=GetEnvironmentVariableA("NFSHS_TEST_RACE",config,sizeof(config));
    if (!length || length >= sizeof(config) || !name || !*name) return fallback;
    const size_t name_length=strlen(name);
    for (char *p=config; *p;) {
        while (*p==' ' || *p=='\t' || *p==',' || *p==';') ++p;
        char *key=p;
        while (*p && *p!='=' && *p!=',' && *p!=';' && *p!=' ' && *p!='\t') ++p;
        char *key_end=p;
        while (*p==' ' || *p=='\t') ++p;
        if (*p!='=') { while (*p && *p!=',' && *p!=';') ++p; continue; }
        ++p;
        while (*p==' ' || *p=='\t') ++p;
        char *value_end=0;
        long value=strtol(p,&value_end,0);
        if ((size_t)(key_end-key)==name_length && !strncmp(key,name,name_length) && value_end!=p)
            return (int)value;
        p=value_end!=p ? value_end : p+1;
        while (*p && *p!=',' && *p!=';') ++p;
    }
    return fallback;

#else
return fallback;
#endif
}

static LONG WINAPI nfshs_unhandled_exception(EXCEPTION_POINTERS *ep)
{
#if NFSHS_DIAGNOSTICS

    EXCEPTION_RECORD *er=ep ? ep->ExceptionRecord : 0;
    if(er) {
        unsigned long access=er->NumberParameters>0 ? (unsigned long)er->ExceptionInformation[0] : 0;
        unsigned long address=er->NumberParameters>1 ? (unsigned long)er->ExceptionInformation[1] : 0;
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("UNHANDLED code=%08lx pc=%p access=%lu address=%08lx",
                      er->ExceptionCode,er->ExceptionAddress,access,address));
    }
#if defined(__i386__) || defined(_M_IX86)
    if(ep && ep->ContextRecord) {
        CONTEXT *c=ep->ContextRecord;
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("UNHANDLED_X86 eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
            c->Eax,c->Ebx,c->Ecx,c->Edx,c->Esi,c->Edi,c->Ebp,c->Esp));
        MEMORY_BASIC_INFORMATION mbi;
        if(c->Esp && VirtualQuery((void *)(uintptr_t)c->Esp,&mbi,sizeof(mbi)) &&
           mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
            unsigned int *stack=(unsigned int *)(uintptr_t)c->Esp;
            for(int i=0;i<32;i+=8)
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("UNHANDLED_STACK +%02x %08x %08x %08x %08x %08x %08x %08x %08x",
                    i*4,stack[i],stack[i+1],stack[i+2],stack[i+3],stack[i+4],stack[i+5],stack[i+6],stack[i+7]));
        }
        if(c->Ebp>=0x20 && VirtualQuery((void *)(uintptr_t)(c->Ebp-0x20),&mbi,sizeof(mbi)) &&
           mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
            uintptr_t menu=*(uintptr_t *)(uintptr_t)(c->Ebp-0x1c);
            uintptr_t item=*(uintptr_t *)(uintptr_t)(c->Ebp-0x10);
            int index=-1;
            if(menu && VirtualQuery((void *)menu,&mbi,sizeof(mbi)) && mbi.State==MEM_COMMIT &&
               !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
                for(int i=0;i<16;i++) if(*(uintptr_t *)(menu+0x10+i*4)==item) { index=i; break; }
            }
            uintptr_t vf=0;
            unsigned int flags=0,text=0;
            if(item && VirtualQuery((void *)item,&mbi,sizeof(mbi)) && mbi.State==MEM_COMMIT &&
               !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
                flags=*(unsigned int *)item;
                text=*(unsigned int *)(item+4);
                vf=*(uintptr_t *)(item+0x18);
            }
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("UNHANDLED_MENU menu=%p item=%p index=%d flags=%08x text=%08x vf=%p",
                          (void *)menu,(void *)item,index,flags,text,(void *)vf));
        }
    }
#endif
    return EXCEPTION_EXECUTE_HANDLER;

#else
    return EXCEPTION_CONTINUE_SEARCH;
#endif
}

static unsigned short host_auto_mask(char key)
{
    switch(key) {
      case 'u':return 0x0010; case 'r':return 0x0020;
      case 'd':return 0x0040; case 'l':return 0x0080;
      case 'x': case 'g':return 0x4000; case 'z':return 0x8000;
      case 's':return 0x0008;
    }
    return 0;
}

static unsigned short host_key_mask(WPARAM key)
{
    switch(key) {
      case VK_UP: case 'W':return 0x0010;
      case VK_RIGHT: case 'D':return 0x0020;
      case VK_DOWN: case 'S':return 0x0040;
      case VK_LEFT: case 'A':return 0x0080;
      case VK_BACK:return 0x0001; case VK_RETURN:return 0x0008;
      case '1':return 0x0100; case '3':return 0x0200;
      case 'Q':return 0x0400; case 'E':return 0x0800;
      case 'I':return 0x1000; case 'L':return 0x2000;
      case 'K':return 0x4000; case 'J':return 0x8000;
    }
    return 0;
}

enum { HOST_FB_W = 512, HOST_FB_H = 240, HOST_VRAM_W = PSYQ_GPU_VRAM_WIDTH, HOST_VRAM_H = PSYQ_GPU_VRAM_HEIGHT };
static unsigned int g_framebuffer[HOST_FB_W * HOST_FB_H];
static unsigned long long g_host_pixel_writes;
#define g_vram PsyQ_gpu_vram
static PsxDispEnv g_display_env;
static PsxDrawEnv g_draw_env;
static unsigned short g_active_tpage;
static unsigned short g_draw_mask_set,g_draw_mask_check;
static int g_frame_pending_clear;
static unsigned int g_vram_load_calls, g_vram_load_words, g_texel_reads, g_texel_transparent;
static unsigned int g_texture_window;
static unsigned int g_tpage_reads[1024],g_tpage_transparent[1024];
static const unsigned char *g_host_current_packet;
static const unsigned char *g_host_probe_last_packet;
static unsigned int g_host_probe_last_color;
static unsigned int g_host_probe_writes;
static unsigned int g_host_draw_call_id;
static unsigned int g_hole_probe_count;
static int g_host_probe_x=300,g_host_probe_y=20,g_host_probe_configured;
#if NFSHS_DIAGNOSTICS
static int g_host_collect_render_stats=1;
#else
static const int g_host_collect_render_stats=0;
#endif
static int g_host_ot_trace_in_range;

#if NFSHS_DIAGNOSTICS
struct HostFlareReadback { int tick; unsigned short pixels[25]; };
static HostFlareReadback g_host_flare_readbacks[2048];
static int g_host_flare_readback_count=-1;

static void host_load_flare_readbacks(void)
{
    if(g_host_flare_readback_count>=0) return;
    g_host_flare_readback_count=0;
    char path[MAX_PATH];
    if(!GetEnvironmentVariableA("NFSHS_FLARE_READBACK_REPLAY",path,sizeof(path))) return;
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE) return;
    unsigned char header[12]; DWORD got=0;
    if(!ReadFile(file,header,sizeof(header),&got,0) || got!=sizeof(header) ||
       memcmp(header,"N4FRDBK\0",8)!=0 || *(unsigned int *)(header+8)!=1) {
        CloseHandle(file); return;
    }
    while(g_host_flare_readback_count<2048) {
        unsigned char row[54];
        if(!ReadFile(file,row,sizeof(row),&got,0) || got!=sizeof(row)) break;
        HostFlareReadback *dst=&g_host_flare_readbacks[g_host_flare_readback_count++];
        dst->tick=*(int *)row;
        memcpy(dst->pixels,row+4,50);
    }
    CloseHandle(file);
}

extern "C" void NFSHS_HostReplayFlareReadback(void *memory)
{
    host_load_flare_readbacks();
    if(!memory || g_host_flare_readback_count<=0) return;
    int tick=NFSHS_HostCurrentGameTicks();
    for(int i=0;i<g_host_flare_readback_count;i++) if(g_host_flare_readbacks[i].tick==tick) {
        memcpy(memory,g_host_flare_readbacks[i].pixels,50);
        return;
    }
}

static void host_load_initial_vram(void)
{
    static int loaded;
    char path[MAX_PATH];
    char tick_text[32];
    if(loaded) return;
    if(!GetEnvironmentVariableA("NFSHS_INITIAL_VRAM",path,sizeof(path))) return;
    if(GetEnvironmentVariableA("NFSHS_INITIAL_VRAM_TICK",tick_text,sizeof(tick_text)) &&
       NFSHS_HostCurrentGameTicks()<atoi(tick_text)) return;
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD got=0;
    BOOL ok=ReadFile(file,g_vram,(DWORD)sizeof(g_vram),&got,0);
    CloseHandle(file);
    if(ok && got==sizeof(g_vram))
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("loaded initial VRAM fixture path=%s bytes=%lu",path,(unsigned long)got));
    else
        memset(g_vram,0,sizeof(g_vram));
    loaded=1;
}

#endif

static unsigned int rgb555(unsigned short c)
{
    unsigned int r=(c&31)<<3, g=((c>>5)&31)<<3, b=((c>>10)&31)<<3;
    return ((r|(r>>5))<<16)|((g|(g>>5))<<8)|(b|(b>>5));
}

static unsigned short host_rgb_to_vram(unsigned int color)
{
    return (unsigned short)(((color>>19)&0x1f) | ((color>>6)&0x3e0) |
                            ((color<<7)&0x7c00));
}

static void host_write_draw_vram(int x,int y,unsigned int color)
{
    /* Packet XY is local to DRAWENV.ofs, not to DISPENV or the host window. */
    int vx=x+g_draw_env.ofs[0],vy=y+g_draw_env.ofs[1];
    if((unsigned)vx<HOST_VRAM_W && (unsigned)vy<HOST_VRAM_H)
        g_vram[vy*HOST_VRAM_W+vx]=host_rgb_to_vram(color);
}

static void host_write_draw_vram_word(int x,int y,unsigned short color)
{
    int vx=x+g_draw_env.ofs[0],vy=y+g_draw_env.ofs[1];
    if((unsigned)vx<HOST_VRAM_W && (unsigned)vy<HOST_VRAM_H)
        g_vram[vy*HOST_VRAM_W+vx]=color;
}

static void host_clear_draw_target(unsigned int color)
{
    int x0=g_draw_env.clip.x,y0=g_draw_env.clip.y;
    int w=g_draw_env.clip.w,h=g_draw_env.clip.h;
    unsigned short packed=host_rgb_to_vram(color);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        int vx=x0+x,vy=y0+y;
        if((unsigned)vx<HOST_VRAM_W && (unsigned)vy<HOST_VRAM_H)
            g_vram[vy*HOST_VRAM_W+vx]=packed;
    }
}

static void host_configure_probe(void)
{
    if (g_host_probe_configured) return;
    const char *px=getenv("NFSHS_PROBE_X"),*py=getenv("NFSHS_PROBE_Y");
    if(px&&*px) g_host_probe_x=atoi(px);
    if(py&&*py) g_host_probe_y=atoi(py);
    g_host_probe_configured=1;
}

static void host_pixel(int x,int y,unsigned int color)
{
    if (g_host_collect_render_stats) host_configure_probe();
    int clip_x=g_draw_env.clip.x-g_draw_env.ofs[0];
    int clip_y=g_draw_env.clip.y-g_draw_env.ofs[1];
    int clip_w=g_draw_env.clip.w,clip_h=g_draw_env.clip.h;
    if ((unsigned)(x+g_draw_env.ofs[0]) < HOST_VRAM_W &&
        (unsigned)(y+g_draw_env.ofs[1]) < HOST_VRAM_H &&
        x>=clip_x && x<clip_x+clip_w && y>=clip_y && y<clip_y+clip_h) {
        host_write_draw_vram(x,y,color);
        if (!g_host_collect_render_stats) return;
        ++g_host_pixel_writes;
        if(x==g_host_probe_x && y==g_host_probe_y && g_host_current_packet && g_host_probe_writes<32) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("PIXEL_PROBE call=%u n=%u packet=%p code=%02x color=%06x words=%u",
                g_host_draw_call_id,g_host_probe_writes,g_host_current_packet,
                g_host_current_packet[7]&0xfc,color&0xffffff,g_host_current_packet[3]));
            ++g_host_probe_writes;
        }
        if(x==g_host_probe_x && y==g_host_probe_y && g_host_current_packet) {
            g_host_probe_last_packet=g_host_current_packet;
            g_host_probe_last_color=color&0xffffff;
        }
    }
}

/* Triangle setup already clips its bounds, so the hot path can avoid
 * recomputing the draw rectangle for every covered pixel. */
static void host_raster_pixel(int x,int y,unsigned int color)
{
    host_write_draw_vram(x,y,color);
    if (!g_host_collect_render_stats) return;
    host_configure_probe();
    ++g_host_pixel_writes;
    if(x==g_host_probe_x && y==g_host_probe_y && g_host_current_packet) {
        if(g_host_probe_writes<32) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("PIXEL_PROBE call=%u n=%u packet=%p code=%02x color=%06x words=%u",
                g_host_draw_call_id,g_host_probe_writes,g_host_current_packet,
                g_host_current_packet[7]&0xfc,color&0xffffff,g_host_current_packet[3]));
            ++g_host_probe_writes;
        }
        g_host_probe_last_packet=g_host_current_packet;
        g_host_probe_last_color=color&0xffffff;
    }
}

static unsigned short host_psx_dither(int value,int x,int y)
{
    static const signed char matrix[4][4]={
        {-4,0,-3,1},{2,-2,3,-1},{-3,1,-4,0},{3,-1,2,-2}
    };
    int dy=(g_active_tpage&0x200u)?((y+g_draw_env.ofs[1])&3):2;
    int dx=(g_active_tpage&0x200u)?((x+g_draw_env.ofs[0])&3):3;
    value=(value+matrix[dy][dx])>>3;
    return (unsigned short)(value<0?0:(value>31?31:value));
}

static unsigned short host_psx_blend(unsigned short bg,unsigned short fg)
{
    unsigned int bg_bits=bg,fg_bits=fg,color=fg;
    switch((g_active_tpage>>5)&3) {
      case 0:
        bg_bits|=0x8000u;
        color=((fg_bits+bg_bits)-((fg_bits^bg_bits)&0x0421u))>>1;
        break;
      case 1: {
        bg_bits&=~0x8000u;
        unsigned int sum=fg_bits+bg_bits;
        unsigned int carry=(sum-((fg_bits^bg_bits)&0x8421u))&0x8420u;
        color=(sum-carry)|(carry-(carry>>5));
        break;
      }
      case 2: {
        bg_bits|=0x8000u; fg_bits&=~0x8000u;
        unsigned int diff=bg_bits-fg_bits+0x108420u;
        unsigned int borrow=(diff-((bg_bits^fg_bits)&0x108420u))&0x108420u;
        color=(diff-borrow)&(borrow-(borrow>>5));
        break;
      }
      default: {
        bg_bits&=~0x8000u;
        fg_bits=((fg_bits>>2)&0x1ce7u)|0x8000u;
        unsigned int sum=fg_bits+bg_bits;
        unsigned int carry=(sum-((fg_bits^bg_bits)&0x8421u))&0x8420u;
        color=(sum-carry)|(carry-(carry>>5));
        break;
      }
    }
    return (unsigned short)color;
}

static void host_raster_pixel_word(int x,int y,unsigned short color,int semitrans,int textured)
{
    int vx=x+g_draw_env.ofs[0],vy=y+g_draw_env.ofs[1];
    if((unsigned)vx>=HOST_VRAM_W || (unsigned)vy>=HOST_VRAM_H) return;
    unsigned short background=g_vram[vy*HOST_VRAM_W+vx];
    if(background&g_draw_mask_check) return;
    /* Read the physical drawing page, not the displayed RGB framebuffer.
       STP qualifies textured pixels only; untextured primitives always blend
       when opcode bit 1 is set. Bit 15 is not an alpha channel in the result. */
    if(semitrans && (!textured || (color&0x8000u)))
        color=host_psx_blend(background,(unsigned short)(color|0x8000u));
    if(!textured) color&=0x7fffu;
    color|=g_draw_mask_set;
    host_write_draw_vram_word(x,y,color);
    if (!g_host_collect_render_stats) return;
    host_configure_probe();
    ++g_host_pixel_writes;
    if(x==g_host_probe_x && y==g_host_probe_y && g_host_current_packet) {
        if(g_host_probe_writes<32) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("PIXEL_PROBE call=%u n=%u packet=%p code=%02x color=%06x words=%u",
                g_host_draw_call_id,g_host_probe_writes,g_host_current_packet,
                g_host_current_packet[7]&0xfc,rgb555(color)&0xffffff,g_host_current_packet[3]));
            ++g_host_probe_writes;
        }
        g_host_probe_last_packet=g_host_current_packet;
        g_host_probe_last_color=rgb555(color)&0xffffff;
    }
}

struct HostVertex { int x,y,u,v; };

struct HostRasterScan {
    int minx,maxx,miny,maxy;
    long long e0,e1,e2;
    long long e0dx,e1dx,e2dx,e0dy,e1dy,e2dy;
    long long u,v,r,g,b;
    long long udx,vdx,rdx,gdx,bdx;
    long long udy,vdy,rdy,gdy,bdy;
    unsigned int psx_u,psx_v,psx_r,psx_g,psx_b;
    int psx_udx,psx_vdx,psx_udy,psx_vdy;
    int psx_rdx,psx_gdx,psx_bdx,psx_rdy,psx_gdy,psx_bdy;
    struct { unsigned long long start_x[2],step_x[2]; int start_y,end_y,upside; } part[2];
};

static int host_psx_attrib_step(int av,int bv,int cv,int ay,int by,int cy,int det)
{
    long long n=(long long)(bv-av)*(cy-by)-(long long)(cv-bv)*(by-ay);
    return (int)((unsigned int)((n*4096)/det)<<12);
}

static void host_setup_psx_uv(HostVertex va,HostVertex vb,HostVertex vc,HostRasterScan *s)
{
    HostVertex v[3]={va,vb,vc},tmp;
    unsigned int tl=0;
    if(v[1].x<=v[0].x) tl=(v[2].x<=v[1].x)?4:2;
    else if(v[2].x<v[0].x) tl=4; else tl=1;
    if(v[2].y<v[1].y){tmp=v[2];v[2]=v[1];v[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    if(v[1].y<v[0].y){tmp=v[1];v[1]=v[0];v[0]=tmp;tl=((tl>>1)&1)|((tl<<1)&2)|(tl&4);}
    if(v[2].y<v[1].y){tmp=v[2];v[2]=v[1];v[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    tl>>=1;
    int det=(vb.x-va.x)*(vc.y-vb.y)-(vc.x-vb.x)*(vb.y-va.y);
    s->psx_udx=host_psx_attrib_step(va.u,vb.u,vc.u,va.y,vb.y,vc.y,det);
    s->psx_vdx=host_psx_attrib_step(va.v,vb.v,vc.v,va.y,vb.y,vc.y,det);
    s->psx_udy=host_psx_attrib_step(va.x,vb.x,vc.x,va.u,vb.u,vc.u,det);
    s->psx_vdy=host_psx_attrib_step(va.x,vb.x,vc.x,va.v,vb.v,vc.v,det);
    unsigned int u=((unsigned int)(v[tl].u*4096+2048))<<12;
    unsigned int vv=((unsigned int)(v[tl].v*4096+2048))<<12;
    u+=(unsigned int)(s->psx_udx*-v[tl].x);u+=(unsigned int)(s->psx_udy*-v[tl].y);
    vv+=(unsigned int)(s->psx_vdx*-v[tl].x);vv+=(unsigned int)(s->psx_vdy*-v[tl].y);
    s->psx_u=u+(unsigned int)(s->psx_udx*s->minx)+(unsigned int)(s->psx_udy*s->miny);
    s->psx_v=vv+(unsigned int)(s->psx_vdx*s->minx)+(unsigned int)(s->psx_vdy*s->miny);
}

static void host_setup_psx_rgb(HostVertex va,HostVertex vb,HostVertex vc,
                               unsigned int ca,unsigned int cb,unsigned int cc,
                               HostRasterScan *s)
{
    struct ColorVertex { HostVertex v; unsigned int c; } q[3]={{va,ca},{vb,cb},{vc,cc}},tmp;
    unsigned int tl=0;
    if(q[1].v.x<=q[0].v.x) tl=(q[2].v.x<=q[1].v.x)?4:2;
    else if(q[2].v.x<q[0].v.x) tl=4; else tl=1;
    if(q[2].v.y<q[1].v.y){tmp=q[2];q[2]=q[1];q[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    if(q[1].v.y<q[0].v.y){tmp=q[1];q[1]=q[0];q[0]=tmp;tl=((tl>>1)&1)|((tl<<1)&2)|(tl&4);}
    if(q[2].v.y<q[1].v.y){tmp=q[2];q[2]=q[1];q[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    tl>>=1;
    int det=(vb.x-va.x)*(vc.y-vb.y)-(vc.x-vb.x)*(vb.y-va.y);
    int ar=(ca>>16)&255,ag=(ca>>8)&255,ab=ca&255;
    int br=(cb>>16)&255,bg=(cb>>8)&255,bb=cb&255;
    int cr=(cc>>16)&255,cg=(cc>>8)&255,bc=cc&255;
    s->psx_rdx=host_psx_attrib_step(ar,br,cr,va.y,vb.y,vc.y,det);
    s->psx_gdx=host_psx_attrib_step(ag,bg,cg,va.y,vb.y,vc.y,det);
    s->psx_bdx=host_psx_attrib_step(ab,bb,bc,va.y,vb.y,vc.y,det);
    s->psx_rdy=host_psx_attrib_step(va.x,vb.x,vc.x,ar,br,cr,det);
    s->psx_gdy=host_psx_attrib_step(va.x,vb.x,vc.x,ag,bg,cg,det);
    s->psx_bdy=host_psx_attrib_step(va.x,vb.x,vc.x,ab,bb,bc,det);
    unsigned int tc=q[tl].c;
    unsigned int r=((((tc>>16)&255u)*4096u+2048u)<<12);
    unsigned int g=((((tc>>8)&255u)*4096u+2048u)<<12);
    unsigned int b=(((tc&255u)*4096u+2048u)<<12);
    r+=(unsigned int)(s->psx_rdx*-q[tl].v.x);r+=(unsigned int)(s->psx_rdy*-q[tl].v.y);
    g+=(unsigned int)(s->psx_gdx*-q[tl].v.x);g+=(unsigned int)(s->psx_gdy*-q[tl].v.y);
    b+=(unsigned int)(s->psx_bdx*-q[tl].v.x);b+=(unsigned int)(s->psx_bdy*-q[tl].v.y);
    s->psx_r=r+(unsigned int)(s->psx_rdx*s->minx)+(unsigned int)(s->psx_rdy*s->miny);
    s->psx_g=g+(unsigned int)(s->psx_gdx*s->minx)+(unsigned int)(s->psx_gdy*s->miny);
    s->psx_b=b+(unsigned int)(s->psx_bdx*s->minx)+(unsigned int)(s->psx_bdy*s->miny);
}

static long long host_psx_make_x(int x)
{ return ((long long)x<<32)+0xffffffffLL-(1<<11); }

static long long host_psx_step_x(int dx,int dy)
{
    long long bias=dx<0 ? -(long long)(dy-1) : (dx>0 ? (long long)(dy-1) : 0);
    return ((long long)dx*(1LL<<32)+bias)/dy;
}

static void host_setup_psx_coverage(HostVertex va,HostVertex vb,HostVertex vc,HostRasterScan *s)
{
    HostVertex v[3]={va,vb,vc},tmp;
    unsigned int tl=0;
    if(v[1].x<=v[0].x) tl=(v[2].x<=v[1].x)?4:2;
    else if(v[2].x<v[0].x) tl=4; else tl=1;
    if(v[2].y<v[1].y){tmp=v[2];v[2]=v[1];v[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    if(v[1].y<v[0].y){tmp=v[1];v[1]=v[0];v[0]=tmp;tl=((tl>>1)&1)|((tl<<1)&2)|(tl&4);}
    if(v[2].y<v[1].y){tmp=v[2];v[2]=v[1];v[1]=tmp;tl=((tl>>1)&2)|((tl<<1)&4)|(tl&1);}
    tl>>=1;
    int vo=tl?1:0,vp=(tl==2)?3:0;
    long long base=host_psx_make_x(v[0].x);
    long long base_step=host_psx_step_x(v[2].x-v[0].x,v[2].y-v[0].y);
    long long upper=(v[1].y==v[0].y)?0:host_psx_step_x(v[1].x-v[0].x,v[1].y-v[0].y);
    long long lower=(v[2].y==v[1].y)?0:host_psx_step_x(v[2].x-v[1].x,v[2].y-v[1].y);
    int right=(v[1].y==v[0].y)?(v[1].x>v[0].x):(upper>base_step);
    int r=right?1:0,o=right?0:1;
    int i0=0^vo,i1=1^vo;
    s->part[vo].start_y=v[i0].y;s->part[vo].end_y=v[i1].y;
    s->part[vo].start_x[r]=(unsigned long long)host_psx_make_x(v[i0].x);
    s->part[vo].step_x[r]=(unsigned long long)upper;
    s->part[vo].start_x[o]=(unsigned long long)(base+(long long)(v[vo].y-v[0].y)*base_step);
    s->part[vo].step_x[o]=(unsigned long long)base_step;s->part[vo].upside=vo;
    i0=1^vp;i1=2^vp;
    s->part[vo^1].start_y=v[i0].y;s->part[vo^1].end_y=v[i1].y;
    s->part[vo^1].start_x[r]=(unsigned long long)host_psx_make_x(v[i0].x);
    s->part[vo^1].step_x[r]=(unsigned long long)lower;
    s->part[vo^1].start_x[o]=(unsigned long long)(base+(long long)(v[i0].y-v[0].y)*base_step);
    s->part[vo^1].step_x[o]=(unsigned long long)base_step;s->part[vo^1].upside=(vp!=0);
}

static int host_psx_span(const HostRasterScan *s,int y,int *left,int *right)
{
    for(int i=0;i<2;i++) {
        const unsigned long long *start=s->part[i].start_x,*step=s->part[i].step_x;
        unsigned long long lx,rx;
        if(!s->part[i].upside) {
            if(y<s->part[i].start_y || y>=s->part[i].end_y) continue;
            int n=y-s->part[i].start_y;lx=start[0]+n*step[0];rx=start[1]+n*step[1];
        } else {
            if(y>=s->part[i].start_y || y<s->part[i].end_y) continue;
            int n=s->part[i].start_y-y;lx=start[0]-n*step[0];rx=start[1]-n*step[1];
        }
        *left=(int)(lx>>32);*right=(int)(rx>>32);return *left<*right;
    }
    return 0;
}

static void host_raster_bounds(HostVertex a,HostVertex b,HostVertex c,
                               int *minx,int *maxx,int *miny,int *maxy)
{
    *minx=a.x;*maxx=a.x;*miny=a.y;*maxy=a.y;
    if(b.x<*minx)*minx=b.x;if(c.x<*minx)*minx=c.x;
    if(b.x>*maxx)*maxx=b.x;if(c.x>*maxx)*maxx=c.x;
    if(b.y<*miny)*miny=b.y;if(c.y<*miny)*miny=c.y;
    if(b.y>*maxy)*maxy=b.y;if(c.y>*maxy)*maxy=c.y;
    int clip_x=g_draw_env.clip.x-g_draw_env.ofs[0];
    int clip_y=g_draw_env.clip.y-g_draw_env.ofs[1];
    int clip_x1=clip_x+g_draw_env.clip.w;
    int clip_y1=clip_y+g_draw_env.clip.h;
    if(*minx<clip_x)*minx=clip_x;if(*maxx>=clip_x1)*maxx=clip_x1-1;
    if(*miny<clip_y)*miny=clip_y;if(*maxy>=clip_y1)*maxy=clip_y1-1;
    /* Clip in VRAM space. Negative local XY and off-display rendering are
       legal; neither the host window nor the current DISPENV limits drawing. */
    if(*minx < -g_draw_env.ofs[0])*minx=-g_draw_env.ofs[0];
    if(*miny < -g_draw_env.ofs[1])*miny=-g_draw_env.ofs[1];
    if(*maxx>=HOST_VRAM_W-g_draw_env.ofs[0])*maxx=HOST_VRAM_W-g_draw_env.ofs[0]-1;
    if(*maxy>=HOST_VRAM_H-g_draw_env.ofs[1])*maxy=HOST_VRAM_H-g_draw_env.ofs[1]-1;
}

static long long host_edge2(const HostVertex *a,const HostVertex *b,int sx,int sy)
{
    return (long long)(sx-a->x*2)*(b->y-a->y) -
           (long long)(sy-a->y*2)*(b->x-a->x);
}

/* DuckStation's native-scale hardware shader offsets every vertex by 0.5,
 * so fragment-centre interpolation evaluates the original polygon at integer
 * PSX coordinates.  Texture lookup then uses roundEven(), not a truncation.
 * Keep this rational to make the readback-dependent lens flare deterministic. */
static int host_hw_attrib_round_even(HostVertex a,HostVertex b,HostVertex c,
                                     int av,int bv,int cv,int x,int y)
{
    long long area=(long long)(c.x-a.x)*(b.y-a.y)-(long long)(c.y-a.y)*(b.x-a.x);
    long long sign=area<0?-1:1;
    long long denominator=area*2*sign;
    long long e0=host_edge2(&b,&c,x*2,y*2)*sign;
    long long e1=host_edge2(&c,&a,x*2,y*2)*sign;
    long long e2=host_edge2(&a,&b,x*2,y*2)*sign;
    long long numerator=(long long)av*e0+(long long)bv*e1+(long long)cv*e2;
    int negative=numerator<0;
    unsigned long long magnitude=(unsigned long long)(negative?-numerator:numerator);
    unsigned long long divisor=(unsigned long long)denominator;
    unsigned long long quotient=magnitude/divisor,remainder=magnitude%divisor;
    if(remainder*2>divisor || (remainder*2==divisor && (quotient&1))) ++quotient;
    return negative?-(int)quotient:(int)quotient;
}

static long long host_fixed_value(int a,int b,int c,long long e0,long long e1,long long e2,
                                  long long denominator)
{
    return ((long long)a*e0+(long long)b*e1+(long long)c*e2)*65536/denominator;
}

static int host_setup_scan(HostVertex a,HostVertex b,HostVertex c,
                           unsigned int ca,unsigned int cb,unsigned int cc,HostRasterScan *s)
{
    long long area,denominator,sign;
    int sx,sy;
    host_raster_bounds(a,b,c,&s->minx,&s->maxx,&s->miny,&s->maxy);
    if(s->minx>s->maxx || s->miny>s->maxy) return 0;
    area=(long long)(c.x-a.x)*(b.y-a.y)-(long long)(c.y-a.y)*(b.x-a.x);
    if(area==0) return 0;
    host_setup_psx_coverage(a,b,c,s);
    host_setup_psx_uv(a,b,c,s);
    host_setup_psx_rgb(a,b,c,ca,cb,cc,s);
    sign=area<0 ? -1 : 1;
    denominator=area*2*sign;
    /* PSX attribute steppers are anchored at integer screen coordinates
       (DuckStation GPU_SW UVStepper::StepX/StepY), not pixel centres. */
    sx=s->minx*2;sy=s->miny*2;
    s->e0=host_edge2(&b,&c,sx,sy)*sign;
    s->e1=host_edge2(&c,&a,sx,sy)*sign;
    s->e2=host_edge2(&a,&b,sx,sy)*sign;
    s->e0dx=(long long)2*(c.y-b.y)*sign;
    s->e1dx=(long long)2*(a.y-c.y)*sign;
    s->e2dx=(long long)2*(b.y-a.y)*sign;
    s->e0dy=(long long)-2*(c.x-b.x)*sign;
    s->e1dy=(long long)-2*(a.x-c.x)*sign;
    s->e2dy=(long long)-2*(b.x-a.x)*sign;
#define HOST_SCAN_VALUE(av,bv,cv) host_fixed_value((av),(bv),(cv),s->e0,s->e1,s->e2,denominator)
#define HOST_SCAN_DX(av,bv,cv) host_fixed_value((av),(bv),(cv),s->e0dx,s->e1dx,s->e2dx,denominator)
#define HOST_SCAN_DY(av,bv,cv) host_fixed_value((av),(bv),(cv),s->e0dy,s->e1dy,s->e2dy,denominator)
    s->u=HOST_SCAN_VALUE(a.u,b.u,c.u);s->v=HOST_SCAN_VALUE(a.v,b.v,c.v);
    s->r=HOST_SCAN_VALUE((ca>>16)&255,(cb>>16)&255,(cc>>16)&255);
    s->g=HOST_SCAN_VALUE((ca>>8)&255,(cb>>8)&255,(cc>>8)&255);
    s->b=HOST_SCAN_VALUE(ca&255,cb&255,cc&255);
    s->udx=HOST_SCAN_DX(a.u,b.u,c.u);s->vdx=HOST_SCAN_DX(a.v,b.v,c.v);
    s->rdx=HOST_SCAN_DX((ca>>16)&255,(cb>>16)&255,(cc>>16)&255);
    s->gdx=HOST_SCAN_DX((ca>>8)&255,(cb>>8)&255,(cc>>8)&255);
    s->bdx=HOST_SCAN_DX(ca&255,cb&255,cc&255);
    s->udy=HOST_SCAN_DY(a.u,b.u,c.u);s->vdy=HOST_SCAN_DY(a.v,b.v,c.v);
    s->rdy=HOST_SCAN_DY((ca>>16)&255,(cb>>16)&255,(cc>>16)&255);
    s->gdy=HOST_SCAN_DY((ca>>8)&255,(cb>>8)&255,(cc>>8)&255);
    s->bdy=HOST_SCAN_DY(ca&255,cb&255,cc&255);
#undef HOST_SCAN_VALUE
#undef HOST_SCAN_DX
#undef HOST_SCAN_DY
    return 1;
}

static int host_clamp_channel(long long fixed)
{
    int value=(int)(fixed>>16);
    return value<0 ? 0 : (value>255 ? 255 : value);
}

static unsigned short host_texel(int u,int v,unsigned short tpage,unsigned short clut,int *transparent)
{
    int tp=(tpage>>7)&3, tx=(tpage&15)*64, ty=((tpage>>4)&1)*256;
    /* GPU ignores CLUT bit 15; Y occupies bits 6..14 only.  Keeping bit 15
       produced y>=512 for valid NFSHS palettes such as 0xFCD6. */
    int cx=(clut&63)*16, cy=(clut>>6)&0x1ff, index;
    unsigned short word,color;
    if (g_host_collect_render_stats) {
        ++g_texel_reads;
        ++g_tpage_reads[tpage&1023];
    }
    u=((u&~(((int)g_texture_window&31)<<3))|((((int)g_texture_window>>10)&31)&((int)g_texture_window&31))<<3)&255;
    v=((v&~((((int)g_texture_window>>5)&31)<<3))|((((int)g_texture_window>>15)&31)&(((int)g_texture_window>>5)&31))<<3)&255;
    *transparent=1;
    if ((unsigned)(ty+v)>=HOST_VRAM_H || (unsigned)cy>=HOST_VRAM_H) return 0;
    if (tp==0) { word=g_vram[(ty+v)*HOST_VRAM_W+tx+(u>>2)]; index=(word>>((u&3)*4))&15; }
    else if (tp==1) { word=g_vram[(ty+v)*HOST_VRAM_W+tx+(u>>1)]; index=(word>>((u&1)*8))&255; }
    else { color=g_vram[(ty+v)*HOST_VRAM_W+tx+u]; *transparent=(color==0); if(*transparent&&g_host_collect_render_stats){++g_texel_transparent;++g_tpage_transparent[tpage&1023];} return color; }
    if ((unsigned)(cx+index)>=HOST_VRAM_W) return 0;
    color=g_vram[cy*HOST_VRAM_W+cx+index]; *transparent=(color==0); if(*transparent&&g_host_collect_render_stats){++g_texel_transparent;++g_tpage_transparent[tpage&1023];} return color;
}

static void host_textured_triangle(HostVertex a,HostVertex b,HostVertex c,unsigned short tp,unsigned short cl,
                                   unsigned int ca,unsigned int cb,unsigned int cc,int raw_texture,int semitrans)
{
    HostRasterScan s;
    if(!host_setup_scan(a,b,c,ca,cb,cc,&s)) return;
    for(int y=s.miny;y<=s.maxy;y++) {
        long long e0=s.e0,e1=s.e1,e2=s.e2,u=s.u,v=s.v,rr=s.r,gg=s.g,bb=s.b;
        unsigned int psx_u=s.psx_u,psx_v=s.psx_v;
        unsigned int psx_r=s.psx_r,psx_g=s.psx_g,psx_b=s.psx_b;
        int entered=0,span_left,span_right;
        int has_span=host_psx_span(&s,y,&span_left,&span_right);
        for(int x=s.minx;x<=s.maxx;x++) {
            if(has_span && x>=span_left && x<span_right) {
                int tr;
                int sample_u=host_hw_attrib_round_even(a,b,c,a.u,b.u,c.u,x,y);
                int sample_v=host_hw_attrib_round_even(a,b,c,a.v,b.v,c.v,x,y);
                unsigned short t=host_texel(sample_u,sample_v,tp,cl,&tr);
                entered=1;
            if(g_host_collect_render_stats && x==g_host_probe_x && y==g_host_probe_y && g_host_current_packet) {
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TEXEL_PROBE tick=%d call=%u packet=%p code=%02x uv=%d,%d texel=%04x transparent=%d tp=%04x cl=%04x tw=%05x",
                    NFSHS_HostCurrentGameTicks(),g_host_draw_call_id,g_host_current_packet,
                    g_host_current_packet[7]&0xfc,sample_u,sample_v,t,tr,tp,cl,g_texture_window));
            }
            if(g_host_collect_render_stats && x==190 && y==170 && g_hole_probe_count<128) {
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("HOLE_PROBE n=%u call=%u packet=%p code=%02x tp=%04x cl=%04x uv=%d,%d texel=%04x transparent=%d tri=(%d,%d)(%d,%d)(%d,%d)",
                    g_hole_probe_count,g_host_draw_call_id,g_host_current_packet,
                    g_host_current_packet?g_host_current_packet[7]&0xfc:0,tp,cl,
                    sample_u,sample_v,t,tr,a.x,a.y,b.x,b.y,c.x,c.y));
                ++g_hole_probe_count;
                }
                if(!tr) {
                    unsigned short color=t;
                    if(!raw_texture) {
                        int vr=(unsigned char)(psx_r>>24);
                        int vg=(unsigned char)(psx_g>>24);
                        int vb=(unsigned char)(psx_b>>24);
                        color=(unsigned short)(
                            host_psx_dither(((t&31)*vr)>>4,x,y) |
                            (host_psx_dither((((t>>5)&31)*vg)>>4,x,y)<<5) |
                            (host_psx_dither((((t>>10)&31)*vb)>>4,x,y)<<10) |
                            (t&0x8000u));
                    }
                    host_raster_pixel_word(x,y,color,semitrans,1);
                }
            } else if(entered) break;
            e0+=s.e0dx;e1+=s.e1dx;e2+=s.e2dx;
            u+=s.udx;v+=s.vdx;rr+=s.rdx;gg+=s.gdx;bb+=s.bdx;
            psx_u+=(unsigned int)s.psx_udx;psx_v+=(unsigned int)s.psx_vdx;
            psx_r+=(unsigned int)s.psx_rdx;psx_g+=(unsigned int)s.psx_gdx;psx_b+=(unsigned int)s.psx_bdx;
        }
        s.e0+=s.e0dy;s.e1+=s.e1dy;s.e2+=s.e2dy;
        s.u+=s.udy;s.v+=s.vdy;s.r+=s.rdy;s.g+=s.gdy;s.b+=s.bdy;
        s.psx_u+=(unsigned int)s.psx_udy;s.psx_v+=(unsigned int)s.psx_vdy;
        s.psx_r+=(unsigned int)s.psx_rdy;s.psx_g+=(unsigned int)s.psx_gdy;s.psx_b+=(unsigned int)s.psx_bdy;
    }
}

static short host_s16(const unsigned char *p,int off) { return *(const short *)(p+off); }
static HostVertex host_vertex(const unsigned char *p,int xy,int uv)
{ HostVertex v={host_s16(p,xy),host_s16(p,xy+2),p[uv],p[uv+1]}; return v; }

static void host_colored_triangle(HostVertex a,HostVertex b,HostVertex c,
                                  unsigned int ca,unsigned int cb,unsigned int cc,int semitrans,int shaded=1)
{
    HostRasterScan s;
    if(!host_setup_scan(a,b,c,ca,cb,cc,&s)) return;
    for(int y=s.miny;y<=s.maxy;y++) {
        long long e0=s.e0,e1=s.e1,e2=s.e2,rr=s.r,gg=s.g,bb=s.b;
        unsigned int psx_r=s.psx_r,psx_g=s.psx_g,psx_b=s.psx_b;
        int entered=0,span_left,span_right;
        int has_span=host_psx_span(&s,y,&span_left,&span_right);
        for(int x=s.minx;x<=s.maxx;x++) {
            if(has_span && x>=span_left && x<span_right) {
                entered=1;
                unsigned short color=(unsigned short)(
                    host_psx_dither((unsigned char)(psx_r>>24),x,y) |
                    (host_psx_dither((unsigned char)(psx_g>>24),x,y)<<5) |
                    (host_psx_dither((unsigned char)(psx_b>>24),x,y)<<10) |
                    (semitrans?0x8000u:0));
                if(!shaded) color=host_rgb_to_vram(ca);
                host_raster_pixel_word(x,y,color,semitrans,0);
            } else if(entered) break;
            e0+=s.e0dx;e1+=s.e1dx;e2+=s.e2dx;
            rr+=s.rdx;gg+=s.gdx;bb+=s.bdx;
            psx_r+=(unsigned int)s.psx_rdx;psx_g+=(unsigned int)s.psx_gdx;psx_b+=(unsigned int)s.psx_bdx;
        }
        s.e0+=s.e0dy;s.e1+=s.e1dy;s.e2+=s.e2dy;
        s.r+=s.rdy;s.g+=s.gdy;s.b+=s.bdy;
        s.psx_r+=(unsigned int)s.psx_rdy;s.psx_g+=(unsigned int)s.psx_gdy;s.psx_b+=(unsigned int)s.psx_bdy;
    }
}

static unsigned int host_rgb(const unsigned char *p,int off)
{ return ((unsigned int)p[off]<<16)|((unsigned int)p[off+1]<<8)|p[off+2]; }

/* GP0 rectangles (tiles and sprites) have no dithering or interpolation.
   Keep modulation in five-bit channels before applying the common blender. */
static void host_rectangle(const unsigned char *p)
{
    int code=p[7],textured=code&4,size=(code>>3)&3;
    int x0=host_s16(p,8),y0=host_s16(p,10);
    int w=size ? (size==1?1:(size==2?8:16)) :
        (*(const unsigned short *)(p+(textured?16:12))&0x3ff);
    int h=size ? w : (*(const unsigned short *)(p+(textured?18:14))&0x1ff);
    if(!w || !h) return;
    HostVertex a={x0,y0,0,0},b={x0+w-1,y0+h-1,0,0};
    int minx,maxx,miny,maxy;
    host_raster_bounds(a,b,b,&minx,&maxx,&miny,&maxy);
    unsigned short cl=textured ? *(const unsigned short *)(p+14) : 0;
    unsigned short flat=host_rgb_to_vram(host_rgb(p,4));
    for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++) {
        unsigned short color=flat;
        if(textured) {
            int tr;
            int u=p[12]+((g_active_tpage&0x1000u)?x0-x:x-x0);
            int v=p[13]+((g_active_tpage&0x2000u)?y0-y:y-y0);
            color=host_texel(u,v,g_active_tpage,cl,&tr);
            if(tr) continue;
            if(!(code&1)) {
                int r=((color&31)*p[4])>>7;
                int g=(((color>>5)&31)*p[5])>>7;
                int b=(((color>>10)&31)*p[6])>>7;
                color=(unsigned short)((r>31?31:r)|((g>31?31:g)<<5)|
                    ((b>31?31:b)<<10)|(color&0x8000u));
            }
        }
        host_raster_pixel_word(x,y,color,code&2,textured);
    }
}

static int host_sign_extend_11(unsigned int value);

static void host_flat_line(const unsigned char *p)
{
    /* Hud_BuildTach 0x800D40EC..0x800D411C emits LINE_F2 (GP0 42).
       Native-scale DDA rules: major-axis stepping, both endpoints, rounding
       bias at half-pixels. Reference: DuckStation GPU_SW DrawLine. */
    int ax=host_s16(p,8),ay=host_s16(p,10),bx=host_s16(p,12),by=host_s16(p,14);
    int w=abs(bx-ax),h=abs(by-ay),n=w>h?w:h;
    if(w>=1024 || h>=512)return;
    if(ax>=bx && n) { int t=ax;ax=bx;bx=t;t=ay;ay=by;by=t; }
    const long long unit=0x100000000LL;
    long long dx=n?((bx-ax)*unit+(bx>ax?n-1:0))/n:0;
    long long dy=n?((by-ay)*unit+(by>ay?n-1:(by<ay?1-n:0)))/n:0;
    long long fx=ax*unit+unit/2-1024,fy=ay*unit+unit/2-(dy<0?1024:0);
    for(int i=0;i<=n;i++,fx+=dx,fy+=dy) {
        int x=host_sign_extend_11((unsigned int)(fx>>32)+g_draw_env.ofs[0]);
        int y=host_sign_extend_11((unsigned int)(fy>>32)+g_draw_env.ofs[1]);
        if(x<g_draw_env.clip.x || x>=g_draw_env.clip.x+g_draw_env.clip.w ||
           y<g_draw_env.clip.y || y>=g_draw_env.clip.y+g_draw_env.clip.h)continue;
        x-=g_draw_env.ofs[0];y-=g_draw_env.ofs[1];
        unsigned short c=host_psx_dither(p[4],x,y)|(host_psx_dither(p[5],x,y)<<5)|
                         (host_psx_dither(p[6],x,y)<<10);
        host_raster_pixel_word(x,y,c,p[7]&2,0);
    }
}

static int host_draw_state_word(unsigned int word)
{
    switch(word>>24) {
      case 0xe1: g_active_tpage=(unsigned short)(word&0x3fffu); return 1;
      case 0xe2: g_texture_window=word&0xfffffu; return 1;
      case 0xe3: {
        int right=g_draw_env.clip.x+g_draw_env.clip.w;
        int bottom=g_draw_env.clip.y+g_draw_env.clip.h;
        g_draw_env.clip.x=(short)(word&0x3ffu);
        g_draw_env.clip.y=(short)((word>>10)&0x1ffu);
        g_draw_env.clip.w=(short)(right-g_draw_env.clip.x);
        g_draw_env.clip.h=(short)(bottom-g_draw_env.clip.y);
        return 1;
      }
      case 0xe4:
        g_draw_env.clip.w=(short)((word&0x3ffu)+1-g_draw_env.clip.x);
        g_draw_env.clip.h=(short)(((word>>10)&0x1ffu)+1-g_draw_env.clip.y);
        return 1;
      case 0xe5:
        g_draw_env.ofs[0]=(short)host_sign_extend_11(word);
        g_draw_env.ofs[1]=(short)host_sign_extend_11(word>>11);
        return 1;
      case 0xe6:
        g_draw_mask_set=(word&1)?0x8000u:0;
        g_draw_mask_check=(word&2)?0x8000u:0;
        return 1;
    }
    return 0;
}

static int host_sign_extend_11(unsigned int value)
{
    value&=0x7ffu;
    return (value&0x400u) ? (int)(value|~0x7ffu) : (int)value;
}

static int host_apply_draw_environment_packet(const unsigned char *p)
{
    const unsigned int *words=(const unsigned int *)p;
    unsigned int count=words[0]>>24;
    if(count<6 || (words[1]&0xff000000u)!=0xe3000000u ||
       (words[2]&0xff000000u)!=0xe4000000u ||
       (words[3]&0xff000000u)!=0xe5000000u) return 0;

    /* SetDrawEnv emits the real PsyQ GP0 environment sequence in one DMA
       packet.  E3/E4 are inclusive VRAM bounds and E5 is signed 11-bit.
       These commands select the physical framebuffer page written by all
       following primitives; PutDispEnv does not modify drawing state. */
    host_draw_state_word(words[1]);
    host_draw_state_word(words[2]);
    host_draw_state_word(words[3]);
    host_draw_state_word(words[4]);
    host_draw_state_word(words[5]);
    host_draw_state_word(words[6]);

    if(count>=9 && (words[7]&0xfc000000u)==0x60000000u) {
        unsigned char packet[16];
        memset(packet,0,sizeof(packet));
        ((unsigned int *)packet)[1]=words[7];
        ((unsigned int *)packet)[2]=words[8];
        ((unsigned int *)packet)[3]=words[9];
        host_rectangle(packet);
    }
    return 1;
}

static void host_draw_packet(const unsigned char *p)
{
    if(host_apply_draw_environment_packet(p)) return;
    int code=p[7]&0xfc;
    if(host_draw_state_word(*(const unsigned int *)(p+4))) {
        /* DR_MODE contains E1 followed by E2 in the same DMA node. */
        unsigned int count=*(const unsigned int *)p>>24;
        for(unsigned int i=2;i<=count;i++) host_draw_state_word(((const unsigned int *)p)[i]);
        return;
    }
    if(code>=0x20 && code<=0x3c && (code&4)) {
        unsigned short tp=*(const unsigned short *)(p+((code&0x10)?26:22));
        /* A textured polygon latches TPAGE bits 0..8 and 11 for itself AND
           subsequent sprites/untextured primitives. E1 dither is preserved. */
        g_active_tpage=(unsigned short)((g_active_tpage&~0x9ffu)|(tp&0x9ffu));
    }
    if(code==0x20){HostVertex a=host_vertex(p,8,0),b=host_vertex(p,12,0),c=host_vertex(p,16,0);unsigned int q=host_rgb(p,4);host_colored_triangle(a,b,c,q,q,q,p[7]&2,0);return;}
    if(code==0x28){HostVertex a=host_vertex(p,8,0),b=host_vertex(p,12,0),c=host_vertex(p,16,0),d=host_vertex(p,20,0);unsigned int q=host_rgb(p,4);host_colored_triangle(a,b,c,q,q,q,p[7]&2,0);host_colored_triangle(c,b,d,q,q,q,p[7]&2,0);return;}
    if(code==0x30){HostVertex a=host_vertex(p,8,0),b=host_vertex(p,16,0),c=host_vertex(p,24,0);host_colored_triangle(a,b,c,host_rgb(p,4),host_rgb(p,12),host_rgb(p,20),p[7]&2);return;}
    if(code==0x38){HostVertex a=host_vertex(p,8,0),b=host_vertex(p,16,0),c=host_vertex(p,24,0),d=host_vertex(p,32,0);unsigned int ca=host_rgb(p,4),cb=host_rgb(p,12),cc=host_rgb(p,20),cd=host_rgb(p,28);host_colored_triangle(a,b,c,ca,cb,cc,p[7]&2);host_colored_triangle(c,b,d,cc,cb,cd,p[7]&2);return;}
    if(code==0x24) { HostVertex a=host_vertex(p,8,12),b=host_vertex(p,16,20),c=host_vertex(p,24,28); unsigned short cl=*(const unsigned short*)(p+14),tp=*(const unsigned short*)(p+22); unsigned int q=host_rgb(p,4); host_textured_triangle(a,b,c,tp,cl,q,q,q,p[7]&1,p[7]&2); return; }
    if(code==0x34) { HostVertex a=host_vertex(p,8,12),b=host_vertex(p,20,24),c=host_vertex(p,32,36); unsigned short cl=*(const unsigned short*)(p+14),tp=*(const unsigned short*)(p+26); host_textured_triangle(a,b,c,tp,cl,host_rgb(p,4),host_rgb(p,16),host_rgb(p,28),p[7]&1,p[7]&2); return; }
    if(code==0x2c) { HostVertex a=host_vertex(p,8,12),b=host_vertex(p,16,20),c=host_vertex(p,24,28),d=host_vertex(p,32,36); unsigned short cl=*(const unsigned short*)(p+14),tp=*(const unsigned short*)(p+22); unsigned int q=host_rgb(p,4); host_textured_triangle(a,b,c,tp,cl,q,q,q,p[7]&1,p[7]&2);host_textured_triangle(c,b,d,tp,cl,q,q,q,p[7]&1,p[7]&2); return; }
    if(code==0x3c) { HostVertex a=host_vertex(p,8,12),b=host_vertex(p,20,24),c=host_vertex(p,32,36),d=host_vertex(p,44,48); unsigned short cl=*(const unsigned short*)(p+14),tp=*(const unsigned short*)(p+26); unsigned int ca=host_rgb(p,4),cb=host_rgb(p,16),cc=host_rgb(p,28),cd=host_rgb(p,40); host_textured_triangle(a,b,c,tp,cl,ca,cb,cc,p[7]&1,p[7]&2);host_textured_triangle(c,b,d,tp,cl,cc,cb,cd,p[7]&1,p[7]&2); return; }
    if(code>=0x60 && code<=0x7c) { host_rectangle(p); return; }
    if(code==0x40) { host_flat_line(p); return; }
}

static int host_readable(const void *p)
{
    MEMORY_BASIC_INFORMATION mbi; if(!p||!VirtualQuery(p,&mbi,sizeof(mbi)))return 0;
    return mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD));
}

/* Observation-only OT stream.  The payload deliberately excludes each DMA
 * tag's low 24-bit pointer: host and PSX addresses differ, while the count of
 * zero-link nodes and every GPU command word are directly comparable. */
#if NFSHS_DIAGNOSTICS
struct HostOtTraceHeader { char magic[8]; unsigned int version; };
struct HostOtTraceFrame {
    unsigned int marker;
    int tick, call, view;
    unsigned int nodes, packets, trailingZeros, flags;
};
struct HostOtTracePacket {
    unsigned int zeroRun, words;
    uintptr_t address, caller;
};
static int g_host_ot_trace_fd=-2;
static int g_host_ot_trace_start;
static int g_host_ot_trace_end=0x7fffffff;
static int g_host_ot_trace_call;
extern "C" short gClutDepth[256][16];
extern "C" void NFSHS_GTE_DebugState(int *,int *,int *);
extern "C" void NFSHS_GTE_DebugTransform(void *,void *);

struct HostDrawQuadCandidate {
    unsigned int marker;
    int tick,call;
    uintptr_t sd,quad,material;
    unsigned char quadBytes[16],materialBytes[4];
    int sxy[4];
    int transformed[4][3];
    int otz,offset;
    short matrix[10];
    int translation[3];
    int h,ofx,ofy;
    unsigned char mirror,fogstate;
    short startfog,distfog;
};

struct HostDrawCPrimEntry {
    unsigned int marker;
    int tick,call,envmap;
    uintptr_t matrix,translation,obj,overlay,cache,primPtr,mprimPtr;
    unsigned char objHead[32];
    unsigned char matrixBytes[36];
    int translationValues[3];
    unsigned char cacheHead[32];
};

extern "C" void NFSHS_HostTraceDrawCPrimEntry(
    const void *matrix,const void *translation,const void *obj,const void *overlay,
    int envmap,const void *rawCache)
{
    static int fd=-2,startTick,endTick=0x7fffffff,call;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_DRAWC_PRIM_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_DRAWC_PRIM_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_DRAWC_PRIM_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4DCPRM\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !matrix || !translation || !obj || !rawCache || tick<startTick || tick>endTick) return;
    const unsigned char *cache=(const unsigned char *)rawCache;
    HostDrawCPrimEntry record={};
    record.marker=0x5043444eU; record.tick=tick; record.call=call++; record.envmap=envmap;
    record.matrix=(uintptr_t)matrix; record.translation=(uintptr_t)translation;
    record.obj=(uintptr_t)obj; record.overlay=(uintptr_t)overlay; record.cache=(uintptr_t)rawCache;
    memcpy(&record.primPtr,cache+4,sizeof(record.primPtr));
    memcpy(&record.mprimPtr,cache+8,sizeof(record.mprimPtr));
    memcpy(record.objHead,obj,sizeof(record.objHead));
    memcpy(record.matrixBytes,matrix,sizeof(record.matrixBytes));
    memcpy(record.translationValues,translation,sizeof(record.translationValues));
    memcpy(record.cacheHead,cache,sizeof(record.cacheHead));
    _write(fd,&record,sizeof(record));
}

struct HostR3DCarEntry {
    unsigned int marker;
    int tick,call,carIndex;
    uintptr_t car;
    unsigned char position[12];
    unsigned char render[152];
    unsigned char visible[57];
};

extern "C" void NFSHS_HostTraceR3DCarEntry(const void *rawCar,const void *rawVisible)
{
    static int fd=-2,startTick,endTick=0x7fffffff,call;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_R3DCAR_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_R3DCAR_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_R3DCAR_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4R3CAR\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !rawCar || !rawVisible || tick<startTick || tick>endTick) return;
    const unsigned char *car=(const unsigned char *)rawCar;
    HostR3DCarEntry record={};
    record.marker=0x4333444eU; record.tick=tick; record.call=call++; record.car=(uintptr_t)rawCar;
    memcpy(&record.carIndex,car+0x254,4);
    memcpy(record.position,car+0x18,12);
    memcpy(record.render,car+0x840,sizeof(record.render));
    memcpy(record.visible,rawVisible,sizeof(record.visible));
    _write(fd,&record,sizeof(record));
}

struct HostPrimStartResult {
    unsigned int marker;
    int tick,call,result,carIndex;
    uintptr_t car;
    unsigned char center[8];
    unsigned char screenMatrix[32];
    unsigned char cacheHead[80];
};

extern "C" void NFSHS_HostTracePrimStartResult(
    const void *rawCar,int result,const void *center,const void *screenMatrix,const void *cache)
{
    static int fd=-2,startTick,endTick=0x7fffffff,call;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_PRIMSTART_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_PRIMSTART_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_PRIMSTART_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4PSTRT\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !rawCar || !center || !screenMatrix || !cache || tick<startTick || tick>endTick) return;
    const unsigned char *car=(const unsigned char *)rawCar;
    HostPrimStartResult record={};
    record.marker=0x5350444eU; record.tick=tick; record.call=call++; record.result=result;
    record.car=(uintptr_t)rawCar; memcpy(&record.carIndex,car+0x254,4);
    memcpy(record.center,center,sizeof(record.center));
    memcpy(record.screenMatrix,screenMatrix,sizeof(record.screenMatrix));
    memcpy(record.cacheHead,cache,sizeof(record.cacheHead));
    _write(fd,&record,sizeof(record));
}

extern "C" void NFSHS_HostTraceDrawQuadCandidate(
    const void *rawSd,const void *quad,const void *material,
    long sxy0,long sxy1,long sxy2,long sxy3)
{
    static int fd=-2,startTick,endTick=0x7fffffff,call;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_DRAWQUAD_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_DRAWQUAD_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_DRAWQUAD_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4DQUAD\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !rawSd || !quad || !material || tick<startTick || tick>endTick) return;
    const unsigned char *sd=(const unsigned char *)rawSd;
    HostDrawQuadCandidate record={};
    record.marker=0x4451434eU;
    record.tick=tick; record.call=call++;
    record.sd=(uintptr_t)rawSd; record.quad=(uintptr_t)quad; record.material=(uintptr_t)material;
    memcpy(record.quadBytes,quad,sizeof(record.quadBytes));
    memcpy(record.materialBytes,material,sizeof(record.materialBytes));
    record.sxy[0]=(int)sxy0; record.sxy[1]=(int)sxy1;
    record.sxy[2]=(int)sxy2; record.sxy[3]=(int)sxy3;
    for(int i=0;i<4;i++) memcpy(record.transformed[i],sd+0x98+i*0x10,12);
    memcpy(&record.otz,sd+0x94,4); memcpy(&record.offset,sd+0xec,4);
    memcpy(record.matrix,sd+0x14,sizeof(record.matrix));
    memcpy(record.translation,sd+0x14+20,sizeof(record.translation));
    NFSHS_GTE_DebugState(&record.h,&record.ofx,&record.ofy);
    record.mirror=sd[0x0c]; record.fogstate=sd[0x10e];
    memcpy(&record.startfog,sd+0xdc,2); memcpy(&record.distfog,sd+0xde,2);
    _write(fd,&record,sizeof(record));
}

extern "C" void NFSHS_HostTraceFlareState(const void *state)
{
    static int fd=-2,startTick,endTick=0x7fffffff;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_FLARE_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_FLARE_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_FLARE_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4FLARE\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !state || tick<startTick || tick>endTick) return;
    _write(fd,&tick,4);
    _write(fd,state,128);
}

extern "C" void NFSHS_HostTraceFlareEmit(
    const char *kind,const void *center,int otz,const void *caller)
{
    static int fd=-2,startTick,endTick=0x7fffffff;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_FLARE_EMIT_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_FLARE_EMIT_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_FLARE_EMIT_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4FEMIT\1",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !center || tick<startTick || tick>endTick) return;
    struct Emit { int tick,otz,xypack; uintptr_t caller; char kind[12]; } record={};
    record.tick=tick; record.otz=otz; memcpy(&record.xypack,center,4);
    record.caller=(uintptr_t)caller;
    if(kind) strncpy(record.kind,kind,sizeof(record.kind)-1);
    _write(fd,&record,sizeof(record));
}

static int host_flare_halo_trace_fd(void)
{
    static int fd=-2;
    if(fd==-2) {
        char path[260];
        unsigned long length=GetEnvironmentVariableA("NFSHS_FLARE_HALO_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return fd; }
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4FHALO\1",8);
    }
    return fd;
}

extern "C" void NFSHS_HostTraceFlareHaloEntry(
    const void *view,int scale,int type,const void *fpt,const void *fpt2,const void *cache)
{
    int fd=host_flare_halo_trace_fd();
    if(fd<0 || !view || !fpt) return;
    struct Entry {
        unsigned marker; int tick,scale,type;
        uintptr_t view,fpt,fpt2,cache;
        int viewPos[3],point[3],point2[3];
    } record={};
    record.marker=0x48454e54U; record.tick=NFSHS_HostCurrentGameTicks();
    record.scale=scale; record.type=type;
    record.view=(uintptr_t)view; record.fpt=(uintptr_t)fpt;
    record.fpt2=(uintptr_t)fpt2; record.cache=(uintptr_t)cache;
    memcpy(record.viewPos,(const char *)view+8,12);
    memcpy(record.point,fpt,12);
    if(fpt2) memcpy(record.point2,fpt2,12);
    _write(fd,&record,sizeof(record));
}

extern "C" void NFSHS_HostTraceFlareHaloRTPS(
    const void *diff,const void *screen,int flags,int scale)
{
    int fd=host_flare_halo_trace_fd();
    if(fd<0 || !diff) return;
    struct RTPS {
        unsigned marker; int tick,flags,scale,diff[3],screenPack,sz;
    } record={};
    record.marker=0x48525450U; record.tick=NFSHS_HostCurrentGameTicks();
    record.flags=flags; record.scale=scale;
    memcpy(record.diff,diff,12);
    if(screen) memcpy(&record.screenPack,screen,4);
    _write(fd,&record,sizeof(record));
}

static void host_trace_clut_depth_once(int tick)
{
    static int done;
    if(done)return;
    char path[260],number[32];
    unsigned long length=GetEnvironmentVariableA("NFSHS_CLUT_TRACE",path,sizeof(path));
    if(!length || length>=sizeof(path))return;
    int startTick=0;
    if(GetEnvironmentVariableA("NFSHS_CLUT_TRACE_START",number,sizeof(number))) startTick=atoi(number);
    if(tick<startTick)return;
    int fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
    if(fd>=0) {
        _write(fd,"N4CLUT\1\0",8);
        _write(fd,&tick,4);
        _write(fd,gClutDepth,sizeof(gClutDepth));
        _close(fd);
    }
    done=1;
}

static void host_ot_trace_write(const void *data,unsigned int size)
{
    const unsigned char *bytes=(const unsigned char *)data;
    while(size) {
        int written=_write(g_host_ot_trace_fd,bytes,size);
        if(written<=0) return;
        bytes+=written;
        size-=(unsigned int)written;
    }
}

#endif

extern "C" {
static uintptr_t host_psyq_ot_tail_resolve(unsigned int next);
static int host_psyq_ot_tail_contains(uintptr_t address);
}

static int host_ot_or_packet_range(uintptr_t address)
{
    for(unsigned int i=0;i<g_host_ot_range_count;i++)
        if(address>=g_host_ot_ranges[i].begin && address+4<=g_host_ot_ranges[i].end)
            return 1;
    return g_host_packet_begin && address>=g_host_packet_begin && address+4<=g_host_packet_end;
}

static int host_dma_known_range(uintptr_t address)
{
    if(host_ot_or_packet_range(address)) return 1;
    return gHostObjectArena && address>=(uintptr_t)gHostObjectArena &&
           address+4<=(uintptr_t)gHostObjectArena+gHostObjectArenaUsed;
}

static uintptr_t host_ot_trace_next(unsigned char *cur,unsigned char *start,unsigned int next)
{
    uintptr_t added=host_added_prim_resolve(next);
    if(added) return added;
    uintptr_t tail=host_psyq_ot_tail_resolve(next);
    if(tail) return tail;
    /* The PSX DMA tag retains only address bits 23:2.  A native chain can
       alternate between the PE image, packet arena, object arena and OT.
       Resolve all explicitly known native ranges before accepting a readable
       zero-extended PE alias: low 24 bits alone are not unique on Windows. */
    uintptr_t candidates[4]={
        ((uintptr_t)cur&~(uintptr_t)0xffffffu)|next,
        ((uintptr_t)start&~(uintptr_t)0xffffffu)|next,
        (g_host_packet_begin&~(uintptr_t)0xffffffu)|next,
        ((uintptr_t)gHostObjectArena&~(uintptr_t)0xffffffu)|next
    };
    for(int i=0;i<4;i++)
        if(host_dma_known_range(candidates[i])) return candidates[i];
    if(host_readable((void *)(uintptr_t)next)) return (uintptr_t)next;
    for(int i=0;i<4;i++)
        if(host_readable((void *)candidates[i])) return candidates[i];
    return 0;
}

#if NFSHS_DIAGNOSTICS
static void host_trace_ot(unsigned long *start)
{
    g_host_ot_trace_in_range=0;
    if(g_host_ot_trace_fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_OT_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) {
            g_host_ot_trace_fd=-1;
            return;
        }
        if(GetEnvironmentVariableA("NFSHS_OT_TRACE_START",number,sizeof(number)))
            g_host_ot_trace_start=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_OT_TRACE_END",number,sizeof(number)))
            g_host_ot_trace_end=atoi(number);
        g_host_ot_trace_fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(g_host_ot_trace_fd>=0) {
            HostOtTraceHeader header={{'N','4','O','T','R','C','E','\0'},2};
            host_ot_trace_write(&header,sizeof(header));
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_TRACE opened path=%s ticks=%d..%d",path,
                          g_host_ot_trace_start,g_host_ot_trace_end));
        }
    }
    if(g_host_ot_trace_fd<0 || !start) return;
    int tick=NFSHS_HostCurrentGameTicks();
    int call=g_host_ot_trace_call++;
    if(tick<g_host_ot_trace_start || tick>g_host_ot_trace_end) return;
    g_host_ot_trace_in_range=1;
    host_trace_clut_depth_once(tick);

    unsigned char *cur=(unsigned char *)start;
    unsigned int nodes=0,packets=0,zeroRun=0;
    int terminated=0,cycle=0;
    uintptr_t seen[8192];
    unsigned int seenCount=0;
    while(host_readable(cur) && nodes<100000 && seenCount<8192) {
        uintptr_t address=(uintptr_t)cur;
        unsigned int si=0;
        for(;si<seenCount;si++) if(seen[si]==address) break;
        if(si<seenCount) { cycle=1; break; }
        seen[seenCount++]=address;
        unsigned int tag=*(unsigned int *)cur;
        unsigned int words=tag>>24,rawNext=tag&0x00ffffffu;
        ++nodes;
        if(words) { ++packets; zeroRun=0; }
        else ++zeroRun;
        if(rawNext==0x00ffffffu) { terminated=1; break; }
        uintptr_t next=host_ot_trace_next(cur,(unsigned char *)start,rawNext&0x00fffffcu);
        if(!next || next==(uintptr_t)cur) break;
        cur=(unsigned char *)next;
    }
    int view=-1;
    for(unsigned int i=0;i<g_host_ot_range_count;i++)
        if((uintptr_t)start==g_host_ot_ranges[i].end-4u) { view=(int)i; break; }
    HostOtTraceFrame frame={0x46544f4eU,tick,call,view,nodes,packets,zeroRun,
                            (unsigned int)(terminated|(cycle<<1))};
    host_ot_trace_write(&frame,sizeof(frame));

    cur=(unsigned char *)start;
    seenCount=0;
    zeroRun=0;
    while(host_readable(cur) && seenCount<8192) {
        uintptr_t address=(uintptr_t)cur;
        unsigned int si=0;
        for(;si<seenCount;si++) if(seen[si]==address) break;
        if(si<seenCount) break;
        seen[seenCount++]=address;
        unsigned int tag=*(unsigned int *)cur;
        unsigned int words=tag>>24,rawNext=tag&0x00ffffffu;
        if(words) {
            uintptr_t caller=0;
            unsigned int slot=((unsigned int)(uintptr_t)cur>>2)&16383u;
            unsigned int probes=0;
            while(g_host_added_generation[slot]==g_host_add_generation &&
                  g_host_added_prims[slot]!=(uintptr_t)cur && ++probes<16384)
                slot=(slot+1)&16383u;
            if(g_host_added_generation[slot]==g_host_add_generation &&
               g_host_added_prims[slot]==(uintptr_t)cur)
                caller=g_host_added_callers[slot];
            if(!caller) {
                slot=host_history_slot((uintptr_t)cur);
                if(g_host_all_added_prims[slot]==(uintptr_t)cur)
                    caller=g_host_all_added_callers[slot];
            }
            HostOtTracePacket packet={zeroRun,words,(uintptr_t)cur,caller};
            host_ot_trace_write(&packet,sizeof(packet));
            host_ot_trace_write(cur+4,words*4u);
            zeroRun=0;
        } else ++zeroRun;
        if(rawNext==0x00ffffffu) break;
        uintptr_t next=host_ot_trace_next(cur,(unsigned char *)start,rawNext&0x00fffffcu);
        if(!next || next==(uintptr_t)cur) break;
        cur=(unsigned char *)next;
    }
}

extern "C" void NFSHS_HostTraceSkyProjection(void *scratch)
{
    static int fd=-2,startTick,endTick=0x7fffffff;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_SKY_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_SKY_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_SKY_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4SKY\0\2\0",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !scratch || tick<startTick || tick>endTick) return;
    _write(fd,&tick,4);
    _write(fd,scratch,0x2c0);
    struct { unsigned char matrix[20]; int translation[3]; } transform;
    NFSHS_GTE_DebugTransform(transform.matrix,transform.translation);
    _write(fd,&transform,sizeof(transform));
}

extern "C" void NFSHS_HostTraceHorizonCache(void *scratch)
{
    static int fd=-2,startTick,endTick=0x7fffffff;
    if(fd==-2) {
        char path[260],number[32];
        unsigned long length=GetEnvironmentVariableA("NFSHS_HORIZON_TRACE",path,sizeof(path));
        if(!length || length>=sizeof(path)) { fd=-1; return; }
        if(GetEnvironmentVariableA("NFSHS_HORIZON_TRACE_START",number,sizeof(number))) startTick=atoi(number);
        if(GetEnvironmentVariableA("NFSHS_HORIZON_TRACE_END",number,sizeof(number))) endTick=atoi(number);
        fd=_open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666);
        if(fd>=0) _write(fd,"N4HRZ\0\1\0",8);
    }
    int tick=NFSHS_HostCurrentGameTicks();
    if(fd<0 || !scratch || tick<startTick || tick>endTick) return;
    _write(fd,&tick,4);
    _write(fd,scratch,0x168);
}

static void host_write_ppm(const char *name)
{
    FILE *shot=fopen(name,"wb");
    if(!shot)return;
    int width=(g_display_env.disp.w>0 && g_display_env.disp.w<=HOST_FB_W) ?
        g_display_env.disp.w : HOST_FB_W;
    int height=(g_display_env.disp.h>0 && g_display_env.disp.h<=HOST_FB_H) ?
        g_display_env.disp.h : HOST_FB_H;
    fprintf(shot,"P6\n%d %d\n255\n",width,height);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        unsigned int c=g_framebuffer[y*HOST_FB_W+x];
        fputc((c>>16)&255,shot); fputc((c>>8)&255,shot); fputc(c&255,shot);
    }
    fclose(shot);
}

static void host_write_vram_ppm(const char *name)
{
    FILE *shot=fopen(name,"wb");
    if(!shot)return;
    fprintf(shot,"P6\n%d %d\n255\n",HOST_VRAM_W,HOST_VRAM_H);
    for(int i=0;i<HOST_VRAM_W*HOST_VRAM_H;i++) {
        unsigned int c=rgb555(g_vram[i]);
        fputc((c>>16)&255,shot); fputc((c>>8)&255,shot); fputc(c&255,shot);
    }
    fclose(shot);
}

static void host_write_texture_ppm(const char *name,unsigned short tpage,unsigned short clut)
{
    FILE *shot=fopen(name,"wb");
    if(!shot)return;
    fprintf(shot,"P6\n256 256\n255\n");
    for(int v=0;v<256;v++) for(int u=0;u<256;u++) {
        int transparent=0;
        unsigned int c=rgb555((unsigned short)host_texel(u,v,tpage,clut,&transparent));
        fputc((c>>16)&255,shot); fputc((c>>8)&255,shot); fputc(c&255,shot);
    }
    fclose(shot);
}

#endif

extern "C" int NFSHS_HostReadableRange(const void *p,unsigned int bytes)
{
    if (!p || !bytes) return 0;
    return host_readable(p) && host_readable((const unsigned char *)p + bytes - 1);
}

static void host_refresh_scanout(void);

static void host_present(void)
{
    host_refresh_scanout();
    int display_width=(g_display_env.disp.w>0 && g_display_env.disp.w<=HOST_FB_W) ? g_display_env.disp.w : HOST_FB_W;
    int display_height=(g_display_env.disp.h>0 && g_display_env.disp.h<=HOST_FB_H) ? g_display_env.disp.h : HOST_FB_H;
#if NFSHS_DIAGNOSTICS
    static unsigned int present_count;
    static unsigned int best_nonblack;
    static int capture_enabled=-1;
    static int frame_capture_tick=-2;
    if(frame_capture_tick==-2) {
        const char *value=getenv("NFSHS_FRAME_CAPTURE_TICK");
        frame_capture_tick=value?atoi(value):-1;
    }
    /* Overwrite after each submitted view at this tick: the final file is
       the composed display, unlike the older world-only capture. */
    if(frame_capture_tick>=0 && NFSHS_HostCurrentGameTicks()==frame_capture_tick)
        host_write_ppm("nfshs-composed.ppm");
    if(capture_enabled<0) capture_enabled=GetEnvironmentVariableA("NFSHS_CAPTURE",0,0)>0;
    ++present_count;
    unsigned int nonblack=0;
    if(capture_enabled) for(int y=0;y<display_height;y++) for(int x=0;x<display_width;x++)
        if(g_framebuffer[y*HOST_FB_W+x]&0x00ffffffu) ++nonblack;
    if(capture_enabled && ((present_count%60u)==0 || nonblack>best_nonblack)) {
        const char *name=nonblack>best_nonblack?"nfshs-best.ppm":"nfshs-frame.ppm";
        host_write_ppm(name);
        if(nonblack>best_nonblack) best_nonblack=nonblack;
    }
#endif
    if(!g_window)return; RECT rc;GetClientRect(g_window,&rc);BITMAPINFO bi;memset(&bi,0,sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=HOST_FB_W;bi.bmiHeader.biHeight=-HOST_FB_H;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(g_window);StretchDIBits(dc,0,0,rc.right,rc.bottom,0,0,display_width,display_height,g_framebuffer,&bi,DIB_RGB_COLORS,SRCCOPY);ReleaseDC(g_window,dc);
}

/* VRAM is the only render target. Scan out DISPENV, independently of the last
 * DRAWENV: the map and tach have their own offsets and clipping rectangles.
 * MIPS Hud_CreateHudViews 0x800D1C90..0x800D1D14 / SetDefDrawEnv 0x800F2264,
 * 0x800F22B0: clip origin and drawing offset are both the subview VRAM origin. */
static void host_refresh_scanout(void)
{
    int sx=g_display_env.disp.x;
    int sy=g_display_env.disp.y;
    int width=g_display_env.disp.w;
    int height=g_display_env.disp.h;
    if(width<=0 || width>HOST_FB_W) width=HOST_FB_W;
    if(height<=0 || height>HOST_FB_H) height=HOST_FB_H;
    if(g_display_env.isrgb24) {
        const unsigned char *bytes=(const unsigned char *)g_vram;
        for(int y=0;y<height;y++) {
            int byteOffset=((sy+y)*HOST_VRAM_W+sx)*2;
            for(int x=0;x<width;x++) {
                int offset=byteOffset+x*3;
                if(offset<0 || offset+2>=HOST_VRAM_W*HOST_VRAM_H*2) {
                    g_framebuffer[y*HOST_FB_W+x]=0;
                } else {
                    g_framebuffer[y*HOST_FB_W+x]=
                        ((unsigned int)bytes[offset+2]<<16) |
                        ((unsigned int)bytes[offset+1]<<8) |
                        bytes[offset];
                }
            }
        }
    } else {
        for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
            int vx=sx+x, vy=sy+y;
            g_framebuffer[y*HOST_FB_W+x]=
                ((unsigned)vx<HOST_VRAM_W && (unsigned)vy<HOST_VRAM_H) ?
                rgb555(g_vram[vy*HOST_VRAM_W+vx]) : 0;
        }
    }
}

static void host_scanout_vram(void) { host_present(); }

static int host_fast_dma_address(uintptr_t address)
{
    if(host_psyq_ot_tail_contains(address)) return 1;
    if(host_added_prim_resolve((unsigned int)address)==address) return 1;
    if(address<0x01000000u && host_readable((void *)address)) return 1;
    for(unsigned int i=0;i<g_host_ot_range_count;i++)
        if(address>=g_host_ot_ranges[i].begin && address+4<=g_host_ot_ranges[i].end) return 1;
    if(g_host_packet_begin && address>=g_host_packet_begin && address+4<=g_host_packet_end) return 1;
    if(gHostObjectArena && address>=(uintptr_t)gHostObjectArena &&
       address+4<=(uintptr_t)gHostObjectArena+gHostObjectArenaUsed) return 1;
    return 0;
}

/* Minimal linked-list DMA consumer for performance tests.  The verbose path
 * below remains the authoritative corruption/provenance validator; this path
 * performs only the GPU-visible work once those contracts are being tested. */
static void host_draw_ot_fast(unsigned long *start)
{
    static int render_every=-1;
    static unsigned int draw_index;
    if(render_every<0) {
        char value[32];
        render_every=1;
#if NFSHS_DIAGNOSTICS
        if(GetEnvironmentVariableA("NFSHS_TEST_RENDER_EVERY",value,sizeof(value)))
            render_every=atoi(value);
#endif
        if(render_every<0) render_every=0;
    }
    unsigned int current=draw_index++;
    if(render_every==0 || current%(unsigned int)render_every) return;
    unsigned char *cur=(unsigned char *)start;
    int rendered=0;
    for(int count=0;count<100000 && host_fast_dma_address((uintptr_t)cur);count++) {
        unsigned int tag=*(unsigned int *)cur;
        unsigned int raw_next=tag&0x00ffffffu;
        unsigned int packet_words=tag>>24;
        if(packet_words) {
            if(g_frame_pending_clear) {
                unsigned int clear_color=g_draw_env.isbg ?
                    ((unsigned int)g_draw_env.r0<<16)|((unsigned int)g_draw_env.g0<<8)|g_draw_env.b0 : 0;
                host_clear_draw_target(clear_color);
                g_frame_pending_clear=0;
            }
            g_host_current_packet=cur;
            host_draw_packet(cur);
            g_host_current_packet=0;
            rendered=1;
        }
        if(raw_next==0x00ffffffu) break;
        unsigned int next=raw_next&0x00fffffcu;
        uintptr_t candidate=host_ot_trace_next(cur,(unsigned char *)start,next);
        if(!host_fast_dma_address(candidate) || candidate==(uintptr_t)cur) break;
        cur=(unsigned char *)candidate;
    }
    if(rendered) host_present();
}

static void host_draw_ot(unsigned long *start)
{
#if NFSHS_DIAGNOSTICS

    static int headless = -1;
    static int headless_raster = -1;
    static int render_delay_initialized;
    static DWORD render_delay_started;
    static DWORD render_delay_ms;
    static int trace_only = -1;
    static int fast_frontend = -1;
    static int capture_world = -1;
    static int capture_tick = -2;
    static int captured_world;
    static unsigned int capture_world_index;
    static unsigned int world_draw_index;
    static unsigned int diagnostic_calls;
    static unsigned int reference_gt4_traces;
    static unsigned int capture_gt4_traces;
    host_trace_ot(start);
    if(g_host_ot_trace_in_range) host_load_initial_vram();
    if (headless < 0) {
        char value[8];
        headless = GetEnvironmentVariableA("NFSHS_HEADLESS",value,sizeof(value)) != 0;
    }
    if (headless_raster < 0) {
        char value[8];
        headless_raster = GetEnvironmentVariableA("NFSHS_HEADLESS_RASTER",value,sizeof(value)) != 0;
    }
    if (!render_delay_initialized) {
        char value[32];
        render_delay_started=GetTickCount();
        if(GetEnvironmentVariableA("NFSHS_RENDER_DELAY_MS",value,sizeof(value)))
            render_delay_ms=(DWORD)strtoul(value,0,10);
        render_delay_initialized=1;
    }
    if (trace_only < 0) {
        char value[8];
        trace_only = GetEnvironmentVariableA("NFSHS_TRACE_ONLY",value,sizeof(value)) != 0;
    }
    if (fast_frontend < 0) {
        char value[8];
        fast_frontend = GetEnvironmentVariableA("NFSHS_FAST_FRONTEND",value,sizeof(value)) != 0;
    }
    if (capture_world < 0) {
        char value[32];
        capture_world=GetEnvironmentVariableA("NFSHS_CAPTURE",0,0)>0;
        capture_world_index=0;
        if(GetEnvironmentVariableA("NFSHS_CAPTURE_FRAME",value,sizeof(value)))
            capture_world_index=(unsigned int)strtoul(value,0,10);
    }
    if (capture_tick == -2) {
        char value[32];
        capture_tick=-1;
        if(GetEnvironmentVariableA("NFSHS_CAPTURE_TICK",value,sizeof(value)))
            capture_tick=atoi(value);
    }
    if ((headless && !headless_raster) ||
        (render_delay_ms && GetTickCount()-render_delay_started<render_delay_ms)) return;
    if(host_fast_test_enabled() && !host_test_verbose_enabled() && !trace_only &&
       !capture_world && capture_tick<0) {
        host_draw_ot_fast(start);
        return;
    }
    g_host_draw_call_id=diagnostic_calls;
    g_host_probe_writes=0;
    g_host_probe_last_packet=0;
    g_host_probe_last_color=0;
    DWORD draw_started=GetTickCount();
    unsigned long long pixels_before=g_host_pixel_writes;
    unsigned int texels_before=g_texel_reads, transparent_before=g_texel_transparent;
    memset(g_tpage_reads,0,sizeof(g_tpage_reads));
    memset(g_tpage_transparent,0,sizeof(g_tpage_transparent));
    unsigned int generation=++g_host_dma_generation;
    if(!generation) {
        memset(g_host_dma_seen_generation,0,sizeof(g_host_dma_seen_generation));
        generation=++g_host_dma_generation;
    }
    unsigned char *cur=(unsigned char*)start;
    uintptr_t previous=0;
    int count=0, rendered=0, rasterized=0;
    int codes20=0,codes24=0,codes28=0,codes2c=0,codes30=0,codes34=0,codes38=0,codes3c=0,codes60=0,codes64=0;
    int gt3MinX=32767,gt3MaxX=-32768,gt3MinY=32767,gt3MaxY=-32768;
    int gt4MinX=32767,gt4MaxX=-32768,gt4MinY=32767,gt4MaxY=-32768;
    unsigned int gt4TpMin=0xffff,gt4TpMax=0,gt4ClMin=0xffff,gt4ClMax=0;
    unsigned int suspicious_gt3=0;
    unsigned int bright_gt4=0;
    unsigned int near_material_gt3=0,near_material_gt4=0;
    unsigned int corrupt_ot_entries=0;
    while(host_readable(cur)&&count++<100000) {
        unsigned int seen_slot=((unsigned int)(uintptr_t)cur>>2)&32767u;
        { unsigned int remaining=32768;
        while((g_host_dma_seen_generation[seen_slot]==generation &&
              g_host_dma_seen[seen_slot]!=(uintptr_t)cur) && --remaining)
            seen_slot=(seen_slot+1)&32767u;
    }
        if(g_host_dma_seen_generation[seen_slot]==generation) {
            uintptr_t repeat_caller=0,repeat_ot=0;
            if(previous) {
                unsigned int current_slot=((unsigned int)previous>>2)&16383u;
                { unsigned int remaining=16384;
        while((g_host_added_generation[current_slot]==g_host_add_generation &&
                      g_host_added_prims[current_slot]!=previous) && --remaining)
            current_slot=(current_slot+1)&16383u;
    }
                if(g_host_added_generation[current_slot]==g_host_add_generation) {
                    repeat_caller=g_host_added_callers[current_slot];
                    repeat_ot=g_host_added_ots[current_slot];
                } else {
                    unsigned int all_slot=host_history_slot((uintptr_t)previous);
                    if(g_host_all_added_prims[all_slot]) {
                        repeat_caller=g_host_all_added_callers[all_slot];
                        repeat_ot=g_host_all_added_ots[all_slot];
                    }
                }
            }
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("DMA_CYCLE start=%p cur=%p first=%u again=%d firstPrev=%p repeatPrev=%p words=%08x,%08x,%08x,%08x packet=%p..%p",
                start,cur,g_host_dma_seen_index[seen_slot],count,
                (void*)g_host_dma_seen_predecessor[seen_slot],(void*)previous,
                ((unsigned int*)cur)[0],((unsigned int*)cur)[1],((unsigned int*)cur)[2],((unsigned int*)cur)[3],
                (void*)g_host_packet_begin,(void*)g_host_packet_end));
            if(previous && host_readable((void*)previous))
            {
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("DMA_CYCLE_PREV prim=%p caller=%p ot=%p words=%08x,%08x,%08x,%08x",
                    (void*)previous,(void*)repeat_caller,(void*)repeat_ot,
                    ((unsigned int*)previous)[0],((unsigned int*)previous)[1],
                    ((unsigned int*)previous)[2],((unsigned int*)previous)[3]));
            }
            break;
        }
        g_host_dma_seen_generation[seen_slot]=generation;
        g_host_dma_seen[seen_slot]=(uintptr_t)cur;
        g_host_dma_seen_index[seen_slot]=(unsigned int)count;
        g_host_dma_seen_predecessor[seen_slot]=previous;
        unsigned int tag=*(unsigned int*)cur;
        unsigned int raw_next=tag&0x00ffffffu;
        {
            unsigned int addSlot=((unsigned int)(uintptr_t)cur>>2)&16383u;
            { unsigned int remaining=16384;
        while((g_host_added_generation[addSlot]==g_host_add_generation &&
                  g_host_added_prims[addSlot]!=(uintptr_t)cur) && --remaining)
            addSlot=(addSlot+1)&16383u;
    }
            if(g_host_added_generation[addSlot]==g_host_add_generation &&
               raw_next!=g_host_added_links[addSlot]) {
                static unsigned int changed_link_count;
                if(changed_link_count<64) {
                    NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("DMA_TAG_MUTATED n=%u prim=%p caller=%p ot=%p expected=%06x actual=%06x tag=%08x",
                        changed_link_count++,cur,(void*)g_host_added_callers[addSlot],
                        (void*)g_host_added_ots[addSlot],g_host_added_links[addSlot],raw_next,tag));
                }
            }
        }
        /* GPU linked-list DMA fetches words; MADR bits 0..1 are not part of
           the effective address.  Some original lwl/lwr-built tags retain
           garbage in those bits, which real PSX hardware ignores. */
        unsigned int next=raw_next&0x00fffffcu;
        unsigned int packet_words=tag>>24;
        if (packet_words && (uintptr_t)cur < g_host_packet_begin && corrupt_ot_entries < 32) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("CORRUPT_OT_ENTRY n=%u entry=%p prev=%p start=%p index=%d tag=%08x next=%06x data=%08x,%08x,%08x,%08x prevdata=%08x,%08x,%08x,%08x",
                corrupt_ot_entries,cur,(void *)previous,start,(int)(((unsigned long *)start-(unsigned long *)cur)),tag,next,
                ((unsigned int *)cur)[0],((unsigned int *)cur)[1],
                ((unsigned int *)cur)[2],((unsigned int *)cur)[3],
                previous?((unsigned int *)previous)[0]:0,previous?((unsigned int *)previous)[1]:0,
                previous?((unsigned int *)previous)[2]:0,previous?((unsigned int *)previous)[3]:0));
            ++corrupt_ot_entries;
        }
        int code=packet_words ? (cur[7]&0xfc) : -1;
        if(capture_world && world_draw_index==capture_world_index && packet_words) {
            const int *xyoffs=0;
            int nxy=0;
            static const int xy_f3[]={8,12,16},xy_ft3[]={8,16,24};
            static const int xy_f4[]={8,12,16,20},xy_ft4[]={8,16,24,32};
            static const int xy_g3[]={8,16,24},xy_gt3[]={8,20,32};
            static const int xy_g4[]={8,16,24,32},xy_gt4[]={8,20,32,44};
            if(code==0x20){xyoffs=xy_f3;nxy=3;} else if(code==0x24){xyoffs=xy_ft3;nxy=3;}
            else if(code==0x28){xyoffs=xy_f4;nxy=4;} else if(code==0x2c){xyoffs=xy_ft4;nxy=4;}
            else if(code==0x30){xyoffs=xy_g3;nxy=3;} else if(code==0x34){xyoffs=xy_gt3;nxy=3;}
            else if(code==0x38){xyoffs=xy_g4;nxy=4;} else if(code==0x3c){xyoffs=xy_gt4;nxy=4;}
            if(xyoffs) {
                int minx=32767,maxx=-32768,miny=32767,maxy=-32768;
                for(int vi=0;vi<nxy;vi++){int x=host_s16(cur,xyoffs[vi]),y=host_s16(cur,xyoffs[vi]+2);if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;}
                if(maxy-miny>180 && maxx>=120 && minx<=360) {
                    uintptr_t addCaller=0,addOt=0;
                    unsigned int addSlot=((unsigned int)(uintptr_t)cur>>2)&16383u;
                    { unsigned int remaining=16384;
        while((g_host_added_generation[addSlot]==g_host_add_generation &&
                          g_host_added_prims[addSlot]!=(uintptr_t)cur) && --remaining)
            addSlot=(addSlot+1)&16383u;
    }
                    if(g_host_added_generation[addSlot]==g_host_add_generation) {
                        addCaller=g_host_added_callers[addSlot]; addOt=g_host_added_ots[addSlot];
                    }
                    NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TALL_PRIM code=%02x words=%u ptr=%p caller=%p ot=%p bounds=(%d..%d,%d..%d) rgb=%06x tp=%04x cl=%04x uv=(%u,%u)(%u,%u)(%u,%u)(%u,%u) data=%08x,%08x,%08x,%08x",
                        code,packet_words,cur,(void*)addCaller,(void*)addOt,minx,maxx,miny,maxy,host_rgb(cur,4),
                        code==0x3c?*(unsigned short*)(cur+26):0,code==0x3c?*(unsigned short*)(cur+14):0,
                        cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],cur[48],cur[49],
                        ((unsigned int*)cur)[1],((unsigned int*)cur)[2],((unsigned int*)cur)[3],((unsigned int*)cur)[4]));
                }
            }
        }
        if(code==0x20)codes20++;else if(code==0x24)codes24++;else if(code==0x28)codes28++;
        else if(code==0x2c)codes2c++;else if(code==0x30)codes30++;else if(code==0x34){
            codes34++;
            if((*(unsigned short*)(cur+26)==0x0004 && *(unsigned short*)(cur+14)==0x07c5) ||
               (*(unsigned short*)(cur+26)==0x0084 && *(unsigned short*)(cur+14)==0x1e00))
                ++near_material_gt3;
            int xs[3]={host_s16(cur,8),host_s16(cur,20),host_s16(cur,32)};
            int ys[3]={host_s16(cur,10),host_s16(cur,22),host_s16(cur,34)};
            if(capture_world && world_draw_index==capture_world_index && suspicious_gt3<768) {
                uintptr_t addCaller=0,addOt=0;
                unsigned int addSlot=((unsigned int)(uintptr_t)cur>>2)&16383u;
                { unsigned int remaining=16384;
        while((g_host_added_generation[addSlot]==g_host_add_generation &&
                      g_host_added_prims[addSlot]!=(uintptr_t)cur) && --remaining)
            addSlot=(addSlot+1)&16383u;
    }
                if(g_host_added_generation[addSlot]==g_host_add_generation) {
                    addCaller=g_host_added_callers[addSlot]; addOt=g_host_added_ots[addSlot];
                }
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("CAP_GT3 n=%u ptr=%p caller=%p ot=%p xy=(%d,%d)(%d,%d)(%d,%d) tp=%04x cl=%04x tw=%05x uv=(%u,%u)(%u,%u)(%u,%u) rgb=%06x,%06x,%06x",
                    suspicious_gt3,cur,(void*)addCaller,(void*)addOt,
                    xs[0],ys[0],xs[1],ys[1],xs[2],ys[2],
                    *(unsigned short*)(cur+26),*(unsigned short*)(cur+14),g_texture_window,
                    cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],
                    host_rgb(cur,4),host_rgb(cur,16),host_rgb(cur,28)));
                ++suspicious_gt3;
            }
            for(int vi=0;vi<3;vi++){if(xs[vi]<gt3MinX)gt3MinX=xs[vi];if(xs[vi]>gt3MaxX)gt3MaxX=xs[vi];if(ys[vi]<gt3MinY)gt3MinY=ys[vi];if(ys[vi]>gt3MaxY)gt3MaxY=ys[vi];}
            long long area=(long long)xs[0]*(ys[1]-ys[2])+(long long)xs[1]*(ys[2]-ys[0])+(long long)xs[2]*(ys[0]-ys[1]);
            /* In the slot-3 reference race frame world GT3 vertices start at
               y=132.  Record native GT3s entering the sky as well as very large
               triangles on the requested capture frame; these are almost
               exclusively the near-plane subdivision path. */
            if(capture_world && world_draw_index==capture_world_index &&
               (ys[0]<100 || ys[1]<100 || ys[2]<100 || area>80000 || area<-80000) &&
               suspicious_gt3<64) {
                unsigned short cl=*(unsigned short*)(cur+14),tp=*(unsigned short*)(cur+26);
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("GT3_LARGE n=%u area=%lld xy=(%d,%d)(%d,%d)(%d,%d) uv=(%u,%u)(%u,%u)(%u,%u) tp=%04x cl=%04x rgb=%06x,%06x,%06x",
                    suspicious_gt3,area,xs[0],ys[0],xs[1],ys[1],xs[2],ys[2],cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],tp,cl,
                    host_rgb(cur,4),host_rgb(cur,16),host_rgb(cur,28)));
                ++suspicious_gt3;
            }
        }
        else if(code==0x38)codes38++;else if(code==0x3c){
            codes3c++;
            int xs[4]={host_s16(cur,8),host_s16(cur,20),host_s16(cur,32),host_s16(cur,44)};
            int ys[4]={host_s16(cur,10),host_s16(cur,22),host_s16(cur,34),host_s16(cur,46)};
            for(int vi=0;vi<4;vi++){if(xs[vi]<gt4MinX)gt4MinX=xs[vi];if(xs[vi]>gt4MaxX)gt4MaxX=xs[vi];if(ys[vi]<gt4MinY)gt4MinY=ys[vi];if(ys[vi]>gt4MaxY)gt4MaxY=ys[vi];}
            unsigned int tp=*(unsigned short*)(cur+26),cl=*(unsigned short*)(cur+14);
            if((tp==0x0004 && cl==0x07c5) || (tp==0x0084 && cl==0x1e00)) {
                ++near_material_gt4;
                static unsigned int road_ot_packets;
                if(road_ot_packets<192) {
                    int transparent[4],u[4]={cur[12],cur[24],cur[36],cur[48]},v[4]={cur[13],cur[25],cur[37],cur[49]};
                    unsigned int texel[4];
                    for(int ri=0;ri<4;ri++) texel[ri]=host_texel(u[ri],v[ri],(unsigned short)tp,(unsigned short)cl,&transparent[ri]);
                    NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ROAD_OT_PACKET n=%u call=%u ptr=%p tp=%04x cl=%04x "
                                  "xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) texel=%04x/%d,%04x/%d,%04x/%d,%04x/%d",
                        road_ot_packets++,diagnostic_calls,cur,tp,cl,
                        xs[0],ys[0],xs[1],ys[1],xs[2],ys[2],xs[3],ys[3],
                        texel[0],transparent[0],texel[1],transparent[1],
                        texel[2],transparent[2],texel[3],transparent[3]));
                }
            }
            if(capture_world && world_draw_index==capture_world_index && capture_gt4_traces<768) {
                uintptr_t addCaller=0,addOt=0;
                unsigned int addSlot=((unsigned int)(uintptr_t)cur>>2)&16383u;
                { unsigned int remaining=16384;
        while((g_host_added_generation[addSlot]==g_host_add_generation &&
                      g_host_added_prims[addSlot]!=(uintptr_t)cur) && --remaining)
            addSlot=(addSlot+1)&16383u;
    }
                if(g_host_added_generation[addSlot]==g_host_add_generation) {
                    addCaller=g_host_added_callers[addSlot]; addOt=g_host_added_ots[addSlot];
                } else {
                    addSlot=((unsigned int)(uintptr_t)cur>>2)&16383u;
                    { unsigned int remaining=16384;
        while((g_host_prev_added_generation[addSlot]==g_host_prev_add_generation &&
                          g_host_prev_added_prims[addSlot]!=(uintptr_t)cur) && --remaining)
            addSlot=(addSlot+1)&16383u;
    }
                    if(g_host_prev_added_generation[addSlot]==g_host_prev_add_generation) {
                        addCaller=g_host_prev_added_callers[addSlot]; addOt=g_host_prev_added_ots[addSlot];
                    }
                }
                if(!addCaller) {
                    unsigned int allSlot=host_history_slot((uintptr_t)cur);
                    if(g_host_all_added_prims[allSlot]) {
                        addCaller=g_host_all_added_callers[allSlot]; addOt=g_host_all_added_ots[allSlot];
                    }
                }
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("CAP_GT4 n=%u ptr=%p caller=%p ot=%p xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) tp=%04x cl=%04x tw=%05x uv=(%u,%u)(%u,%u)(%u,%u)(%u,%u) rgb=%06x,%06x,%06x,%06x",
                    capture_gt4_traces,cur,(void*)addCaller,(void*)addOt,xs[0],ys[0],xs[1],ys[1],xs[2],ys[2],xs[3],ys[3],tp,cl,
                    g_texture_window,
                    cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],cur[48],cur[49],
                    host_rgb(cur,4),host_rgb(cur,16),host_rgb(cur,28),host_rgb(cur,40)));
                ++capture_gt4_traces;
            }
            if(tp<gt4TpMin)gt4TpMin=tp;if(tp>gt4TpMax)gt4TpMax=tp;if(cl<gt4ClMin)gt4ClMin=cl;if(cl>gt4ClMax)gt4ClMax=cl;
            if(capture_world && world_draw_index==capture_world_index && bright_gt4<32 &&
               cur[4]>240 && cur[5]>240 && cur[6]>240 &&
               (ys[0]<80||ys[1]<80||ys[2]<80||ys[3]<80)) {
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("BRIGHT_GT4 call=%u n=%u ptr=%p xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) rgb=%06x,%06x,%06x,%06x tp=%04x cl=%04x uv=(%u,%u)(%u,%u)(%u,%u)(%u,%u)",
                    diagnostic_calls,bright_gt4,cur,xs[0],ys[0],xs[1],ys[1],xs[2],ys[2],xs[3],ys[3],
                    host_rgb(cur,4),host_rgb(cur,16),host_rgb(cur,28),host_rgb(cur,40),tp,cl,
                    cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],cur[48],cur[49]));
                ++bright_gt4;
            }
            /* The slot-3 PSX reference repeatedly emits this road texture.
               Log its complete packet plus the addressed VRAM/CLUT values so
               host asset upload and packet reconstruction can be compared
               without stepping through every frame manually. */
            if (reference_gt4_traces<12 && host_s16(cur,8)>-1024 && host_s16(cur,8)<1536) {
                unsigned int colors[4],transparent[4];
                for(int vi=0;vi<4;vi++) {
                    int u=cur[12+vi*12],v=cur[13+vi*12];
                    int tr=0;
                    colors[vi]=host_texel(u,v,(unsigned short)tp,(unsigned short)cl,&tr);
                    transparent[vi]=(unsigned int)tr;
                }
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("GT4_TEX n=%u tp=%04x depth=%u cl=%04x tw=%05x xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) uv=(%u,%u)(%u,%u)(%u,%u)(%u,%u) colors=%04x,%04x,%04x,%04x transparent=%u,%u,%u,%u rgb=%06x,%06x,%06x,%06x",
                    reference_gt4_traces,tp,(tp>>7)&3,cl,g_texture_window,
                    host_s16(cur,8),host_s16(cur,10),host_s16(cur,20),host_s16(cur,22),host_s16(cur,32),host_s16(cur,34),host_s16(cur,44),host_s16(cur,46),
                    cur[12],cur[13],cur[24],cur[25],cur[36],cur[37],cur[48],cur[49],
                    colors[0],colors[1],colors[2],colors[3],transparent[0],transparent[1],transparent[2],transparent[3],
                    host_rgb(cur,4),host_rgb(cur,16),host_rgb(cur,28),host_rgb(cur,40)));
                ++reference_gt4_traces;
            }
        }else if(code==0x60)codes60++;
        else if(code==0x64)codes64++;
        /* Diagnostics must submit the same GPU stream as the fast walker.
           The old whitelist dropped E3-led DR_ENV, including the tach's
           28x28 clipping area (Draw_StopRenderingView, MIPS 0x800BE218). */
        if(packet_words) {
            if (g_frame_pending_clear) {
                unsigned int clear_color=g_draw_env.isbg ?
                    ((unsigned int)g_draw_env.r0<<16)|((unsigned int)g_draw_env.g0<<8)|g_draw_env.b0 : 0;
                host_clear_draw_target(clear_color);
                g_frame_pending_clear=0;
            }
            int capture_tick_ready=(capture_tick>=0 &&
                                    NFSHS_HostCurrentGameTicks()>=capture_tick &&
                                    !captured_world);
            if (!trace_only && !(fast_frontend && !g_host_race_started) &&
                ((!capture_world && capture_tick<0) ||
                 (capture_world && world_draw_index==capture_world_index) ||
                 capture_tick_ready)) {
                g_host_current_packet=cur;
                host_draw_packet(cur);
                g_host_current_packet=0;
                ++rasterized;
            }
            rendered++;
        }
        if(raw_next==0x00ffffffu)break;
        uintptr_t candidate=host_ot_trace_next(cur,(unsigned char *)start,next);
        if(!candidate||candidate==(uintptr_t)cur)break;
        previous=(uintptr_t)cur;
        cur=(unsigned char*)candidate;
    }
    if(g_host_probe_last_packet) {
        const unsigned char *pp=g_host_probe_last_packet;
        unsigned int slot=((unsigned int)(uintptr_t)pp>>2)&16383u;
        { unsigned int remaining=16384;
        while((g_host_added_generation[slot]==g_host_add_generation && g_host_added_prims[slot]!=(uintptr_t)pp) && --remaining)
            slot=(slot+1)&16383u;
    }
        uintptr_t caller=(g_host_added_generation[slot]==g_host_add_generation)?g_host_added_callers[slot]:0;
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("PIXEL_FINAL tick=%d call=%u packet=%p caller=%p code=%02x color=%06x xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) tp=%04x cl=%04x uv=(%u,%u)(%u,%u)(%u,%u)(%u,%u)",
            NFSHS_HostCurrentGameTicks(),diagnostic_calls,pp,(void*)caller,pp[7]&0xfc,g_host_probe_last_color,
            host_s16(pp,8),host_s16(pp,10),host_s16(pp,20),host_s16(pp,22),host_s16(pp,32),host_s16(pp,34),host_s16(pp,44),host_s16(pp,46),
            *(unsigned short*)(pp+26),*(unsigned short*)(pp+14),pp[12],pp[13],pp[24],pp[25],pp[36],pp[37],pp[48],pp[49]));
    }
    if (rasterized) host_present();
    if(codes3c>100 && count>1000) {
        if(world_draw_index==0) for(unsigned int tp=0;tp<1024;tp++) if(g_tpage_reads[tp])
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TPAGE_USE tp=%04x reads=%u transparent=%u",tp,g_tpage_reads[tp],g_tpage_transparent[tp]));
        if((capture_world || capture_tick>=0) && !captured_world &&
           ((capture_world && world_draw_index>=capture_world_index) ||
            (capture_tick>=0 && NFSHS_HostCurrentGameTicks()>=capture_tick))) {
            host_write_ppm("nfshs-world.ppm");
            host_write_vram_ppm("nfshs-vram.ppm");
            host_write_texture_ppm("nfshs-tpage0084-clut1e40.ppm",0x0084,0x1e40);
            host_write_texture_ppm("nfshs-tpage0005-clut2205.ppm",0x0005,0x2205);
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("WORLD_CAPTURE call=%u world=%u tick=%d",diagnostic_calls,world_draw_index,
                          NFSHS_HostCurrentGameTicks()));
            captured_world=1;
            capture_world=0;
            capture_tick=-1;
        }
        ++world_draw_index;
    }
    DWORD draw_elapsed=GetTickCount()-draw_started;
    diagnostic_calls++;
    if (draw_elapsed > 100 || (diagnostic_calls & 127U)==0)
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("host_draw_ot count=%d rendered=%d pixels=%u elapsed=%u codes=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d near=%u,%u gt3xy=(%d..%d,%d..%d) gt4xy=(%d..%d,%d..%d) gt4tp=%04x..%04x cl=%04x..%04x texels=%u transparent=%u tpage=%04x loads=%u words=%u",
          count,rendered,(unsigned)(g_host_pixel_writes-pixels_before),(unsigned)draw_elapsed,codes20,codes24,codes28,codes2c,codes30,codes34,codes38,codes3c,codes60,codes64,
          near_material_gt3,near_material_gt4,
          gt3MinX,gt3MaxX,gt3MinY,gt3MaxY,gt4MinX,gt4MaxX,gt4MinY,gt4MaxY,gt4TpMin,gt4TpMax,gt4ClMin,gt4ClMax,
          g_texel_reads-texels_before,g_texel_transparent-transparent_before,
          (unsigned)g_active_tpage,g_vram_load_calls,g_vram_load_words));

#else
    host_draw_ot_fast(start);
#endif
}

#if NFSHS_DIAGNOSTICS
extern "C" void NFSHS_HostLog(const char *fmt, ...)
{
#if NFSHS_DIAGNOSTICS

    if (host_fast_test_enabled() && !host_test_verbose_enabled()) {
        /* Geometry probes can produce tens of thousands of formatted disk
           writes per second.  Fast tests retain lifecycle, state and failure
           evidence; NFSHS_TEST_VERBOSE restores every diagnostic. */
        if (strncmp(fmt,"UNHANDLED",9) && strncmp(fmt,"TEST_",5) &&
            strncmp(fmt,"AUTOTEST_",9) && strncmp(fmt,"startup",7) &&
            strncmp(fmt,"WORLD_CAPTURE",13) &&
            strncmp(fmt,"main ",5) && strncmp(fmt,"Nfs2_",5) &&
            strncmp(fmt,"Sim_",4) && strncmp(fmt,"autopilot",9) &&
            strncmp(fmt,"DUCK_REPLAY",11) &&
            strncmp(fmt,"EAC_REPLAY",10) &&
            strncmp(fmt,"pad keys",8) && strncmp(fmt,"open ",5))
            return;
    }
    if (g_host_log_fd == -2)
        g_host_log_fd = _open("nfshs-runtime.log", _O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,
                              _S_IREAD|_S_IWRITE);
    if (g_host_log_fd < 0) return;
    char line[768];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf(line, sizeof(line)-2, fmt, ap);
    va_end(ap);
    if (n < 0) n = sizeof(line)-2;
    line[n++]='\r'; line[n++]='\n';
    _write(g_host_log_fd, line, n);

#else

#endif
}
#endif

#if NFSHS_DIAGNOSTICS
extern "C" void NFSHS_HostTraceMapMarker(int player,int index,int shape,int x,int y,
    int scaleX,int scaleY,int offX,int offY,int mapCos,int mapSin,int slice)
{
#if NFSHS_DIAGNOSTICS

    char enabled[2],first[16],last[16];
    if (!GetEnvironmentVariableA("NFSHS_MAP_MARKER_TRACE",enabled,sizeof(enabled))) return;
    int tick=NFSHS_HostCurrentGameTicks();
    int start=GetEnvironmentVariableA("NFSHS_MAP_MARKER_TRACE_START",first,sizeof(first)) ? atoi(first) : 0;
    int end=GetEnvironmentVariableA("NFSHS_MAP_MARKER_TRACE_END",last,sizeof(last)) ? atoi(last) : 0x7fffffff;
    if (tick<start || tick>end) return;
    NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TEST_MAP tick=%d player=%d index=%d shape=%d xy=%d,%d scale=%d,%d off=%d,%d trig=%d,%d slice=%d",
        tick,player,index,shape,x,y,scaleX,scaleY,offX,offY,mapCos,mapSin,slice));

#else

#endif
}
#endif

extern "C" int NFSHS_HostIsHeadless(void)
{
    char value[2];
    return GetEnvironmentVariableA("NFSHS_HEADLESS", value, sizeof(value)) != 0;
}

#if NFSHS_DIAGNOSTICS
extern "C" void NFSHS_HostTraceOtProducerSpan(const char *domain,const void *begin,const void *end)
{
#if NFSHS_DIAGNOSTICS

    static int fd=-2;
    if(fd==-2) {
        char path[MAX_PATH];
        fd=GetEnvironmentVariableA("NFSHS_OT_PRODUCER_SPANS",path,sizeof(path)) ?
            _open(path,_O_CREAT|_O_TRUNC|_O_WRONLY|_O_BINARY,0666) : -1;
    }
    if(fd<0) return;
    char line[256];
    int count=_snprintf(line,sizeof(line),
        "{\"tick\":%d,\"domain\":\"%s\",\"begin\":%u,\"end\":%u}\n",
        NFSHS_HostCurrentGameTicks(),domain,(unsigned int)(uintptr_t)begin,(unsigned int)(uintptr_t)end);
    if(count>0 && count<(int)sizeof(line)) _write(fd,line,count);

#else

#endif
}
#endif

extern "C" void NFSHS_HostRaceStart(Car_tObj *playerCar)
{
#if NFSHS_DIAGNOSTICS

#ifdef AP_WIN
    g_host_duck_replay_run=0;
    g_host_duck_replay_finished=0;
#endif
    /* Test-only placement uses the game's own complete placement routine so
       Newton, road slice/quad, orientation, wheel and collision state remain
       synchronized.  With no startSlice parameter this boundary is inert. */
    int startSlice=NFSHS_HostTestRaceParam("startSlice",-1);
    if (playerCar && startSlice >= 0) {
        if (startSlice < gNumSlices) {
            int lateral=NFSHS_HostTestRaceParam("startLat",0);
            int direction=NFSHS_HostTestRaceParam("startDirection",1);
            int speed=NFSHS_HostTestRaceParam("startSpeed",0);
            int rotation=NFSHS_HostTestRaceParam("startRotation",0);
            if (direction != -1) direction=1;
            AILife_PlaceCarAtLocation(playerCar,startSlice,lateral,direction,speed,rotation);
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TEST_PLACE_CAR slice=%d lat=%d direction=%d speed=%d rotation=%d",
                          startSlice,lateral,direction,speed,rotation));
        } else {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("TEST_PLACE_CAR_REJECT slice=%d numSlices=%d",startSlice,gNumSlices));
        }
    }
    /* Input remains input: headless rendering must not silently press Cross.
       Automated gas is available explicitly through NFSHS_AUTOPILOT. */
    g_host_race_started=1;

#else

#endif
}

extern "C" void NFSHS_HostPumpTimers(void)
{
    static unsigned long long lastLogged = 0;
    ensure_window();
    pump_messages();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    /* The PSX libpad ISR updates the two PadInitDirect buffers asynchronously.
       The native backend owns that hardware boundary, so refresh the same
       buffers here before original timer subscribers (including PAD_update)
       consume them. */
    NFSHS_HostPadPoll();
    unsigned long long wanted;
    if (host_fast_test_enabled()) {
        /* One systemtask pump represents one real PSX root-counter tick.  This
           makes timedwait finish immediately in wall-clock time without
           deleting the callbacks or changing the counter values they see. */
        wanted = g_host_timer_ticks + 1;
    } else {
        wanted = (unsigned long long)
            (((now.QuadPart - g_qpc_start.QuadPart) * 128) / g_qpc_frequency.QuadPart);
    }
    /* Avoid an unbounded catch-up burst after a debugger pause. */
    if (wanted > g_host_timer_ticks + 16) g_host_timer_ticks = wanted - 16;
    while (g_host_timer_ticks < wanted) {
        ++g_host_timer_ticks;
        ++gTicks;
        tmrint();
    }
    if (g_host_timer_ticks >= lastLogged + 128) {
        lastLogged = g_host_timer_ticks;
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("host timer=%u libticks=%d gTicks=%d",
                      (unsigned)g_host_timer_ticks,libticks,gTicks));
    }
}

#define host_log NFSHS_HostLog

static LRESULT CALLBACK nfs4_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) { g_window=0; PostQuitMessage(0); ExitProcess(0); return 0; }
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        unsigned short button=host_key_mask(wp);
        if (msg == WM_KEYDOWN) g_pad_buttons |= button;
        else g_pad_buttons &= (unsigned short)~button;
        NFSHS_DIAGNOSTIC_CALL(host_log("WM_KEY%s vk=%u mask=%04x buttons=%04x",
            msg == WM_KEYDOWN ? "DOWN" : "UP",(unsigned)wp,button,g_pad_buttons));
        return 0;
    }
    if (msg == WM_KILLFOCUS) {
        g_pad_buttons = 0;
        return 0;
    }
    if (msg == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); host_present(); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void ensure_window(void)
{
    static int initialized;
    if (g_window || initialized) return;
    initialized=1;
    host_request_timer_resolution();
#if NFSHS_DIAGNOSTICS
    g_host_collect_render_stats=!host_fast_test_enabled() || host_test_verbose_enabled();
#endif
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(nfshs_unhandled_exception);
    {
        char value[8];
        if(GetEnvironmentVariableA("NFSHS_HEADLESS",value,sizeof(value))) {
            QueryPerformanceFrequency(&g_qpc_frequency);
            QueryPerformanceCounter(&g_qpc_start);
            return;
        }
    }
    HINSTANCE instance = GetModuleHandleA(0);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = nfs4_window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "NFSHSNativeWindow";
    RegisterClassA(&wc);
    g_window = CreateWindowA(wc.lpszClassName, "Need for Speed: High Stakes - native port",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 660, 520,
        0, 0, instance, 0);
    QueryPerformanceFrequency(&g_qpc_frequency);
    QueryPerformanceCounter(&g_qpc_start);
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static void set_packet(void *packet, unsigned char words, unsigned char opcode)
{
    if (!packet) return;
    ((unsigned char *)packet)[3] = words;
    ((unsigned char *)packet)[7] = opcode;
}

extern "C" {

/* ---- Win32 lifecycle / timing ------------------------------------------------ */
int NFSHS_HostResetGraph(int) { ensure_window(); return 0; }
void NFSHS_HostFlushCache(void) {}
void NFSHS_HostResetCallback(void) { g_vsync_callback = 0; }
void SetVideoMode(int mode) { g_video_mode = mode; }
int GetVideoMode(void) { return g_video_mode; }
int NFSHS_HostVSync(int mode)
{
    ensure_window(); pump_messages();
    if (mode >= 0) {
        if (host_fast_test_enabled()) ++g_host_fast_vsync_count;
        else Sleep(16);
    }
    if (g_vsync_callback) g_vsync_callback();
    if (mode >= 0 && !waveout_is_running()) PsyQSpuAdvance(735);
    if (host_fast_test_enabled())
        return (int)g_host_fast_vsync_count;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    return (int)(((now.QuadPart - g_qpc_start.QuadPart) * 60) / g_qpc_frequency.QuadPart);
}
void *VSyncCallback(void *cb) { void *old=(void *)g_vsync_callback; g_vsync_callback=(void(*)(void))cb; return old; }
int GetRCnt(int) { return NFSHS_HostVSync(-1); }
int SetRCnt(int, unsigned short, int) { return 1; }
int StartRCnt(int) { return 1; }
long OpenEvent(...) { return 1; }
long EnableEvent(long) { return 1; }
long DisableEvent(long) { return 1; }
void EnterCriticalSection(void) {}
void ExitCriticalSection(void) {}
void *DMACallback(int, void *cb) { return cb; }
void *InterruptCallback(int, void *cb) { return cb; }

/* ---- GPU packet construction and display state ------------------------------ */
PsxDrawEnv *SetDefDrawEnv(PsxDrawEnv *e,int x,int y,int w,int h)
{
    memset(e,0,sizeof(*e)); e->clip.x=x; e->clip.y=y; e->clip.w=w; e->clip.h=h;
    e->ofs[0]=x; e->ofs[1]=y; e->tpage=10; e->dtd=1;
    e->dfe=(unsigned char)(g_video_mode ? h<289 : h<257); return e;
}
PsxDispEnv *SetDefDispEnv(PsxDispEnv *e,int x,int y,int w,int h)
{ memset(e,0,sizeof(*e)); e->disp.x=x;e->disp.y=y;e->disp.w=w;e->disp.h=h; return e; }
void SetDrawEnv(void *p,PsxDrawEnv *env);
PsxDrawEnv *PutDrawEnv(PsxDrawEnv *e)
{
    if(e) {
        /* PsyQ 0x800EDD70..0x800EDDB4 builds and submits DRAWENV.dr_env;
           copying the struct alone leaves the GPU's ABR/window/mask stale. */
        unsigned int packet[16]={0};
        SetDrawEnv(packet,e);
        packet[0]|=0x00ffffffu;
        /* DRAWENV declares dr_env as u_long; do not alias that array through
           unsigned int* under the host compiler's -O2 strict aliasing. */
        memcpy(e->dr_env,packet,sizeof(packet));
        g_draw_env=*e;
        host_apply_draw_environment_packet((const unsigned char *)packet);
    }
    return e;
}
PsxDispEnv *PutDispEnv(PsxDispEnv *e) { if(e)g_display_env=*e; return e; }
void NFSHS_HostSetDispMask(int enabled) { ensure_window(); if(enabled) host_scanout_vram(); }
long NFSHS_HostDrawSync(long) { pump_messages(); return 0; }
void NFSHS_HostDrawOTag(unsigned long *ot) { ensure_window(); pump_messages(); host_draw_ot(ot); }
struct HostPsyQOtTail {
    unsigned long termTag;
    unsigned long termPayload[4];
    unsigned long linkTag;
    unsigned long linkPayload[4];
};
static HostPsyQOtTail g_host_psyq_ot_tail = {
    0x04ffffffUL, {0,0,0,0}, 0,
    {0x80000000UL,0,0,0x00010002UL}
};
static uintptr_t host_psyq_ot_tail_resolve(unsigned int next)
{
    uintptr_t term=(uintptr_t)&g_host_psyq_ot_tail.termTag;
    uintptr_t link=(uintptr_t)&g_host_psyq_ot_tail.linkTag;
    next&=0x00fffffcu;
    if(next==((unsigned int)term&0x00fffffcu)) return term;
    if(next==((unsigned int)link&0x00fffffcu)) return link;
    return 0;
}
static int host_psyq_ot_tail_contains(uintptr_t address)
{
    uintptr_t begin=(uintptr_t)&g_host_psyq_ot_tail;
    return address>=begin && address+4<=begin+sizeof(g_host_psyq_ot_tail);
}
unsigned long *NFSHS_HostClearOTagR(unsigned long *ot,int n)
{
    if (!ot || n<=0) return ot;
    if(n>65536 || !NFSHS_HostReadableRange(ot,(unsigned int)n*4u)) {
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ClearOTagR rejected ot=%p n=%d",ot,n));
        return ot;
    }
    /* Retail PsyQ ClearOTagR (MIPS 0x800EDC08) does not end ot[0]
       directly.  It points through the fixed SYS.obj tail at
       0x8012375C and 0x80123748: two four-word DMA packets containing
       the harmless GP0 0x80 self-copy and zero commands. */
    g_host_psyq_ot_tail.linkTag = 0x04000000UL |
        ((unsigned long)(uintptr_t)&g_host_psyq_ot_tail.termTag & 0x00ffffffUL);
    for(int i=0;i<n;i++) {
        ot[i]=i ? ((unsigned long)(uintptr_t)&ot[i-1]&0x00ffffffUL) :
            ((unsigned long)(uintptr_t)&g_host_psyq_ot_tail.linkTag & 0x00ffffffUL);
    }
    if (g_host_ot_range_count < sizeof(g_host_ot_ranges)/sizeof(g_host_ot_ranges[0])) {
        g_host_ot_ranges[g_host_ot_range_count].begin=(uintptr_t)ot;
        g_host_ot_ranges[g_host_ot_range_count].end=(uintptr_t)(ot+n);
        ++g_host_ot_range_count;
    }
    return ot;
}
void NFSHS_HostBeginPacketArena(void *begin,void *end)
{
    g_host_packet_begin=(uintptr_t)begin;
    g_host_packet_end=(uintptr_t)end;
}
void NFSHS_HostBeginDMAFrame(void)
{
    g_host_ot_range_count=0;
#if NFSHS_DIAGNOSTICS
    g_host_dma_tag_fault_seen=0;
    memcpy(g_host_prev_added_prims,g_host_added_prims,sizeof(g_host_added_prims));
    memcpy(g_host_prev_added_ots,g_host_added_ots,sizeof(g_host_added_ots));
    memcpy(g_host_prev_added_callers,g_host_added_callers,sizeof(g_host_added_callers));
    memcpy(g_host_prev_added_generation,g_host_added_generation,sizeof(g_host_added_generation));
    g_host_prev_add_generation=g_host_add_generation;
#endif
    if(!++g_host_add_generation) {
        memset(g_host_added_generation,0,sizeof(g_host_added_generation));
        ++g_host_add_generation;
    }
#if NFSHS_DIAGNOSTICS
    g_host_last_added_prim=0;
    g_host_last_added_end=0;
    g_host_packet_overlap_count=0;
#endif
}

#if NFSHS_DIAGNOSTICS
void NFSHS_HostValidateOTPhase(const char *phase,void *otBase,int otSize,void *usedEnd)
{
#if NFSHS_DIAGNOSTICS

    if (!phase || !otBase || otSize <= 0 || otSize > 65536) return;
    uintptr_t otBegin=(uintptr_t)otBase;
    uintptr_t otEnd=otBegin+(uintptr_t)otSize*4u;
    uintptr_t packetUsedEnd=(uintptr_t)usedEnd;
    unsigned char *cur=(unsigned char *)(otEnd-4u);
    uintptr_t previous=0;
    uintptr_t seen[8192];
    int seenCount=0;
    while (host_readable(cur) && seenCount < (int)(sizeof(seen)/sizeof(seen[0]))) {
        uintptr_t address=(uintptr_t)cur;
        for (int i=0;i<seenCount;i++) if (seen[i]==address) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_PHASE_CYCLE phase=%s step=%d cur=%p prev=%p ot=%p..%p packet=%p..%p",
                phase,seenCount,cur,(void *)previous,(void *)otBegin,(void *)otEnd,
                (void *)g_host_packet_begin,(void *)g_host_packet_end));
            return;
        }
        seen[seenCount++]=address;
        unsigned int tag=*(unsigned int *)cur;
        unsigned int rawNext=tag&0x00ffffffu;
        if (rawNext==0x00ffffffu) return;
        unsigned int next=rawNext&0x00fffffcu;
        uintptr_t base=address&~(uintptr_t)0xffffffu;
        uintptr_t candidate=base|next;
        if (!host_readable((void *)candidate)) {
            candidate=(otBegin&~(uintptr_t)0xffffffu)|next;
        }
        if (!host_readable((void *)candidate)) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_PHASE_UNREADABLE phase=%s step=%d cur=%p prev=%p tag=%08x next=%06x",
                phase,seenCount,cur,(void *)previous,tag,next));
            return;
        }
        int inOt=candidate>=otBegin && candidate<otEnd && ((candidate-otBegin)&3u)==0;
        int currentPacket=candidate>=g_host_packet_begin && candidate<packetUsedEnd;
        int previousPacket=0;
        unsigned int slot=((unsigned int)candidate>>2)&16383u;
        unsigned int probe=slot;
        { unsigned int remaining=16384;
        while((g_host_added_generation[probe]==g_host_add_generation &&
              g_host_added_prims[probe]!=candidate) && --remaining)
            probe=(probe+1)&16383u;
    }
        if(g_host_added_generation[probe]==g_host_add_generation) currentPacket=1;
        probe=slot;
        { unsigned int remaining=16384;
        while((g_host_prev_added_generation[probe]==g_host_prev_add_generation &&
              g_host_prev_added_prims[probe]!=candidate) && --remaining)
            probe=(probe+1)&16383u;
    }
        if(g_host_prev_added_generation[probe]==g_host_prev_add_generation) previousPacket=1;
        if (!inOt && !currentPacket) {
            uintptr_t owner=0,ownerOt=0;
            uintptr_t predecessorWriter=0,predecessorPrim=0;
            for(unsigned int wi=0;wi<16384;wi++) {
                if(g_host_added_generation[wi]==g_host_add_generation &&
                   g_host_added_ots[wi]==previous) {
                    predecessorWriter=g_host_added_callers[wi];
                    predecessorPrim=g_host_added_prims[wi];
                }
            }
            unsigned int allSlot=host_history_slot((uintptr_t)candidate);
            if(g_host_all_added_prims[allSlot]) {
                owner=g_host_all_added_callers[allSlot];
                ownerOt=g_host_all_added_ots[allSlot];
            }
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_PHASE_FOREIGN phase=%s step=%d cur=%p prev=%p tag=%08x next=%p previousFrame=%d owner=%p ownerOt=%p prevWriter=%p prevPrim=%p ot=%p..%p packet=%p..%p",
                phase,seenCount,cur,(void *)previous,tag,(void *)candidate,previousPacket,
                (void *)owner,(void *)ownerOt,(void *)predecessorWriter,(void *)predecessorPrim,
                (void *)otBegin,(void *)otEnd,
                (void *)g_host_packet_begin,(void *)packetUsedEnd));
            if ((uintptr_t)cur >= g_host_packet_begin+48u && host_readable(cur-48u)) {
                unsigned int *w=(unsigned int *)cur;
                NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_PHASE_FOREIGN_WORDS phase=%s at=%p m12=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x cur=%08x,%08x,%08x,%08x",
                    phase,cur,w[-12],w[-11],w[-10],w[-9],w[-8],w[-7],w[-6],w[-5],
                    w[-4],w[-3],w[-2],w[-1],w[0],w[1],w[2],w[3]));
            }
            return;
        }
        previous=address;
        cur=(unsigned char *)candidate;
    }
    if(seenCount==(int)(sizeof(seen)/sizeof(seen[0])))
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("OT_PHASE_LIMIT phase=%s steps=%d",phase,seenCount));

#else

#endif
}
#endif
void NFSHS_HostEndPacketArena(void *usedEnd)
{
    NFSHS_DIAGNOSTIC_CALL(host_check_linked_tags((uintptr_t)__builtin_return_address(0)));
    uintptr_t end=(uintptr_t)usedEnd;
    if(end>=g_host_packet_begin && end<=g_host_packet_end)
        g_host_packet_end=end;
}
#if NFSHS_DIAGNOSTICS
void NFSHS_HostSetAddPrimCaller(void *caller)
{
#if NFSHS_DIAGNOSTICS

    g_host_addprim_caller_override=(uintptr_t)caller;

#else

#endif
}
#endif
void NFSHS_HostAddPrim(void *otPtr,void *primPtr)
{
    unsigned int *ot=(unsigned int *)otPtr,*prim=(unsigned int *)primPtr;
    if(!ot || !prim) return;
#if NFSHS_DIAGNOSTICS
    uintptr_t caller=g_host_addprim_caller_override ? g_host_addprim_caller_override :
        (uintptr_t)__builtin_return_address(0);
    g_host_addprim_caller_override=0;
    host_check_linked_tags(caller);
    static unsigned int bad_old_head_count;
    unsigned int oldHead=*ot&0x00ffffffu;
    if (!host_dma_address_is_current(oldHead) && bad_old_head_count < 64) {
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ADDPRIM_BAD_OLD_HEAD n=%u prim=%p ot=%p caller=%p old=%06x tag=%08x packet=%p..%p ranges=%u",
            bad_old_head_count++,primPtr,otPtr,(void *)caller,oldHead,*prim,
            (void *)g_host_packet_begin,(void *)g_host_packet_end,g_host_ot_range_count));
    }
    unsigned int allSlot=host_history_slot((uintptr_t)prim);
    if(allSlot==32768) {
        memset(g_host_all_added_prims,0,sizeof(g_host_all_added_prims));
        memset(g_host_all_added_ots,0,sizeof(g_host_all_added_ots));
        memset(g_host_all_added_callers,0,sizeof(g_host_all_added_callers));
        allSlot=host_history_slot((uintptr_t)prim);
    }
    g_host_all_added_prims[allSlot]=(uintptr_t)prim;
    g_host_all_added_ots[allSlot]=(uintptr_t)ot;
    g_host_all_added_callers[allSlot]=caller;
    static unsigned int facade_prim_count;
    const unsigned char *primBytes=(const unsigned char *)prim;
    if ((primBytes[7]&0xfc)==0x3c &&
        *(const unsigned short *)(primBytes+26)==0x0084 &&
        *(const unsigned short *)(primBytes+14)==0x1e40 &&
        facade_prim_count<128) {
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("FACADE_ADD n=%u prim=%p caller=%p ot=%p xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d)",
            facade_prim_count++,primPtr,(void *)caller,otPtr,
            host_s16(primBytes,8),host_s16(primBytes,10),host_s16(primBytes,20),host_s16(primBytes,22),
            host_s16(primBytes,32),host_s16(primBytes,34),host_s16(primBytes,44),host_s16(primBytes,46)));
    }
    static unsigned int bad_prim_range_count;
    if (g_host_packet_begin &&
        ((uintptr_t)prim < g_host_packet_begin || (uintptr_t)prim >= g_host_packet_end) &&
        bad_prim_range_count < 128) {
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ADDPRIM_OUTSIDE_ARENA n=%u prim=%p ot=%p caller=%p tag=%08x packet=%p..%p",
            bad_prim_range_count,primPtr,otPtr,(void *)caller,*prim,
            (void *)g_host_packet_begin,(void *)g_host_packet_end));
        ++bad_prim_range_count;
    }
    if ((uintptr_t)prim >= g_host_packet_begin && (uintptr_t)prim < g_host_packet_end) {
        unsigned int bytes=((*prim>>24)+1u)*4u;
        if (g_host_last_added_end && (uintptr_t)prim < g_host_last_added_end &&
            (uintptr_t)prim != g_host_last_added_prim && g_host_packet_overlap_count < 128) {
            NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ADDPRIM_OVERLAP n=%u prim=%p bytes=%u previous=%p previousEnd=%p ot=%p caller=%p tag=%08x",
                g_host_packet_overlap_count,primPtr,bytes,(void *)g_host_last_added_prim,
                (void *)g_host_last_added_end,otPtr,(void *)caller,*prim));
            ++g_host_packet_overlap_count;
        }
        g_host_last_added_prim=(uintptr_t)prim;
        g_host_last_added_end=(uintptr_t)prim+bytes;
    }
    #endif
    unsigned int slot=((unsigned int)(uintptr_t)prim>>2)&16383u;
    unsigned int probes=0;
    while(g_host_added_generation[slot]==g_host_add_generation &&
          g_host_added_prims[slot]!=(uintptr_t)prim) {
        if(++probes==16384) {
            MessageBoxA(0,"The native DMA address table is full.","NFSHS fatal error",MB_OK|MB_ICONERROR);
            ExitProcess(94);
        }
        slot=(slot+1)&16383u;
    }
    if(g_host_added_generation[slot]==g_host_add_generation) {
#if NFSHS_DIAGNOSTICS
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("ADDPRIM_DUP prim=%p oldOt=%p newOt=%p oldCaller=%p newCaller=%p tag=%08x",
            primPtr,(void*)g_host_added_ots[slot],otPtr,(void*)g_host_added_callers[slot],
            (void*)caller,*prim));
#endif
    } else {
        g_host_added_generation[slot]=g_host_add_generation;
        g_host_added_prims[slot]=(uintptr_t)prim;
#if NFSHS_DIAGNOSTICS
        g_host_added_ots[slot]=(uintptr_t)ot;
        g_host_added_callers[slot]=caller;
        g_host_added_links[slot]=*ot&0x00ffffffu;
#endif
    }
    *prim=(*prim&0xff000000u)|(*ot&0x00ffffffu);
    *ot=(*ot&0xff000000u)|((unsigned int)(uintptr_t)prim&0x00ffffffu);
}

void PsyQGpuHostAddPrim(void *otPtr,void *primPtr,void *caller)
{
    NFSHS_DIAGNOSTIC_CALL(NFSHS_HostSetAddPrimCaller(caller));
    NFSHS_HostAddPrim(otPtr,primPtr);
}
int NFSHS_HostClearImage(PsxRect *r,unsigned char rr,unsigned char gg,unsigned char bb) { return r ? PsyQGpuClearImage(r->x,r->y,r->w,r->h,rr,gg,bb) : -1; }
int NFSHS_HostLoadImage(PsxRect *r,unsigned long *p) {
    if(!r||!p)return-1;
    unsigned short*s=(unsigned short*)p; unsigned int hash=2166136261u,nonzero=0;
    for(int y=0;y<r->h;y++)for(int x=0;x<r->w;x++) {
        unsigned short value=*s++; hash=(hash^value)*16777619u; if(value)++nonzero;
    }
    int result=PsyQGpuLoadImage(r->x,r->y,r->w,r->h,(const unsigned short*)p);
#if NFSHS_DIAGNOSTICS
    ++g_vram_load_calls; g_vram_load_words+=(unsigned int)(r->w*r->h);
    if(g_vram_load_calls<=80 || r->w*r->h>=256 || (g_vram_load_calls&255u)==0)
        NFSHS_DIAGNOSTIC_CALL(NFSHS_HostLog("VRAM_LOAD call=%u rect=(%d,%d %dx%d) words=%d nonzero=%u hash=%08x src=%p",
                      g_vram_load_calls,r->x,r->y,r->w,r->h,r->w*r->h,nonzero,hash,p));
#endif
    return result;
}
unsigned short NFSHS_HostLoadTPage(unsigned long *pixels,int tp,int abr,int x,int y,int w,int h) {
    if(!pixels || w<=0 || h<=0 || tp<0 || tp>2) return 0;
    /* PsyQ's width is in texels while LoadImage's rectangle width is in
       16-bit VRAM words: four 4-bpp, two 8-bpp, or one 16-bpp texel. */
    PsxRect rect;
    rect.x=(short)x; rect.y=(short)y;
    rect.w=(short)((w + ((1 << (2-tp))-1)) >> (2-tp));
    rect.h=(short)h;
    if(NFSHS_HostLoadImage(&rect,pixels)<0) return 0;
    return (unsigned short)(((tp&3)<<7)|((abr&3)<<5)|((y&0x100)>>4)|((x&0x3ff)>>6));
}
int NFSHS_HostStoreImage(PsxRect *r,unsigned long *p)
{
    if(!r||!p)return-1;
    unsigned short*d=(unsigned short*)p;
    /* PsyQ _drs, MIPS 0x800EF124..0x800EF148, sends the packed RECT
       coordinates unchanged to GP0(C0). The GPU masks X/Y to 10/9 bits
       and wraps each transfer pixel, including negative/off-edge rectangles.
       Do not replace off-screen reads with zero: lens flare samples use them. */
    return PsyQGpuStoreImage(r->x,r->y,r->w,r->h,d);
}
int NFSHS_HostMoveImage(PsxRect *r,int dx,int dy) { return r ? PsyQGpuMoveImage(r->x,r->y,r->w,r->h,dx,dy) : -1; }
void SetPolyF3(void *p)  { set_packet(p,4,0x20); }
void SetPolyF4(void *p)  { set_packet(p,5,0x28); }
void SetPolyFT4(void *p) { set_packet(p,9,0x2c); }
void SetPolyG4(void *p)  { set_packet(p,8,0x38); }
void SetPolyGT4(void *p) { set_packet(p,12,0x3c); }
void SetSemiTrans(void *p,int on) { if(p) ((unsigned char*)p)[7]=(unsigned char)((((unsigned char*)p)[7]&~2)|(on?2:0)); }
void SetShadeTex(void *p,int on) { if(p) ((unsigned char*)p)[7]=(unsigned char)((((unsigned char*)p)[7]&~1)|(on?1:0)); }
int NFSHS_HostGetClut(int x,int y) { return PsyQGpuGetClut(x,y); }
int NFSHS_HostGetTPage(int tp,int abr,int x,int y) { return PsyQGpuGetTPage(tp,abr,x,y); }
void SetDrawMode(void *p,int dfe,int dtd,int tpage,PsxRect *tw)
{
    set_packet(p,2,0xe1);
    if(!p)return;
    ((unsigned long*)p)[1]=0xe1000000UL|(tpage&0x9ff)|((dtd&1)<<9)|((dfe&1)<<10);
    if(tw) ((unsigned long*)p)[2]=0xe2000000UL|((tw->x>>3&31)<<10)|((tw->y>>3&31)<<15)|(((-tw->w)&255)>>3)|((((-tw->h)&255)>>3)<<5);
    else ((unsigned long*)p)[2]=0;
}
static unsigned int host_draw_area_word(unsigned int command,int x,int y)
{
    /* PsyQ _set_clip_tl/br, 0x800EE898..0x800EE9C4: signed 16-bit
       arguments saturated to the GPU's 1024x512 VRAM, not masked/wrapped. */
    x=(short)x;y=(short)y;
    if(x<0)x=0;if(x>=HOST_VRAM_W)x=HOST_VRAM_W-1;
    if(y<0)y=0;if(y>=HOST_VRAM_H)y=HOST_VRAM_H-1;
    return command|((unsigned int)y<<10)|(unsigned int)x;
}
void SetDrawArea(void *p,PsxRect *r)
{
    if(!p || !r)return;
    /* SetDrawArea 0x800EE330..0x800EE37C: E3(x,y), E4(x+w-1,y+h-1). */
    unsigned int *d=(unsigned int *)p;
    d[0]=(d[0]&0x00ffffffu)|0x02000000u;
    d[1]=host_draw_area_word(0xe3000000u,r->x,r->y);
    d[2]=host_draw_area_word(0xe4000000u,r->x+r->w-1,r->y+r->h-1);
}
void SetDrawEnv(void *p,PsxDrawEnv *env)
{
    if(!p || !env) return;
    unsigned int *d=(unsigned int *)p;
    int x0=env->clip.x,y0=env->clip.y;
    int x1=(unsigned short)env->clip.x+(unsigned short)env->clip.w-1;
    int y1=(unsigned short)env->clip.y+(unsigned short)env->clip.h-1;
    d[1]=host_draw_area_word(0xe3000000u,x0,y0);
    d[2]=host_draw_area_word(0xe4000000u,x1,y1);
    d[3]=0xe5000000u|((unsigned int)(env->ofs[1]&0x7ff)<<11)|
         (unsigned int)(env->ofs[0]&0x7ff);
    d[4]=0xe1000000u|(env->tpage&0x9ffu)|(env->dtd?0x200u:0u)|(env->dfe?0x400u:0u);
    {
        unsigned char *tw=(unsigned char *)&env->tw;
        short *tws=(short *)&env->tw;
        d[5]=0xe2000000u|((unsigned int)(tw[0]>>3)<<10)|
             ((unsigned int)(tw[2]>>3)<<15)|
             ((unsigned int)((-tws[3]&0xff)>>3)<<5)|
             (unsigned int)((-tws[2]&0xff)>>3);
    }
    d[6]=0xe6000000u;
    int words=6;
    if(env->isbg) {
        int rx=env->clip.x,ry=env->clip.y;
        unsigned int color=((unsigned int)env->b0<<16)|((unsigned int)env->g0<<8)|env->r0;
        /* NFS4's linked PsyQ 4.3 body at 0x800EE588..0x800EE5E8 always
           subtracts DRAWENV.ofs and emits GP0 0x60; it has no aligned 0x02
           fast-fill branch. */
        rx-=env->ofs[0]; ry-=env->ofs[1]; d[7]=0x60000000u|color;
        d[8]=((unsigned int)(unsigned short)ry<<16)|(unsigned short)rx;
        d[9]=((unsigned int)(unsigned short)env->clip.h<<16)|(unsigned short)env->clip.w;
        words=9;
    }
    d[0]=(d[0]&0x00ffffffu)|((unsigned int)words<<24);
}
void SetDrawMove(void *p,PsxRect *,int,int) { set_packet(p,5,0x80); }
void SetDrawStp(void *p,int on)
{
    if(!p)return;
    /* 0x800EE394..0x800EE3B8: DR_STP is E6 + NOP, not an opcode flag. */
    unsigned int *words=(unsigned int *)p;
    words[0]=(words[0]&0x00ffffffu)|0x02000000u;
    words[1]=0xe6000000u|(on?1u:0u);
    words[2]=0;
}
void SetTexWindow(void *p,PsxRect *tw)
{
    set_packet(p,2,0xe2);
    if(!p)return;
    ((unsigned long*)p)[1]=tw ? 0xe2000000UL|((tw->x>>3&31)<<10)|((tw->y>>3&31)<<15)|(((-tw->w)&255)>>3)|((((-tw->h)&255)>>3)<<5) : 0xe2000000UL;
    ((unsigned long*)p)[2]=0;
}

/* ---- GTE helpers used by gameplay and packet generation --------------------- */
void NFSHS_GTE_WriteControl(int,int);
void NFSHS_GTE_SetGeomScreen(int);
void NFSHS_HostInitGeom(void) {
    memset(&g_rotation,0,sizeof(g_rotation));
    g_rotation.m[0][0]=g_rotation.m[1][1]=g_rotation.m[2][2]=4096;
    NFSHS_GTE_WriteControl(26,0x3e8);
    NFSHS_GTE_WriteControl(27,-0x1062);
    NFSHS_GTE_WriteControl(28,0x1400000);
    NFSHS_GTE_WriteControl(24,0);
    NFSHS_GTE_WriteControl(25,0);
}
void NFSHS_HostSetGeomScreen(int h) { g_geom_screen=h; NFSHS_GTE_SetGeomScreen(h); }
extern void NFSHS_GTE_SetFarColor(int,int,int);
void SetFarColor(int r,int g,int b) { NFSHS_GTE_SetFarColor(r,g,b); }
void SetFogNear(int a,int dvpw) {
    if(dvpw) NFSHS_GTE_WriteControl(27,-(a*320)/dvpw);
    NFSHS_GTE_WriteControl(28,0x1400000);
}
extern "C" const int _rsincos[];
static void host_psyq_sincos(int angle,int *s,int *c)
{
    int packed;
    if(angle<0){packed=_rsincos[-angle&0xfff];*s=-(int)(short)packed;}
    else {packed=_rsincos[angle&0xfff];*s=(int)(short)packed;}
    *c=packed>>16;
}
PsxMatrix *NFSHS_HostRotMatrix(PsxSVector *r,PsxMatrix *m)
{
    int sx,cx,sy,cy,sz,cz;
    host_psyq_sincos(r->vx,&sx,&cx); host_psyq_sincos(r->vy,&sy,&cy); host_psyq_sincos(r->vz,&sz,&cz);
    int nsy=-sy;
    m->m[0][2]=(short)sy;
    m->m[1][2]=(short)(-(cy*sx)>>12);
    m->m[2][2]=(short)(cy*cx>>12);
    m->m[0][0]=(short)(cz*cy>>12);
    m->m[0][1]=(short)(-(sz*cy)>>12);
    int cz_nsy=cz*nsy>>12;
    m->m[1][0]=(short)(sz*cx>>12)-(short)(cz_nsy*sx>>12);
    m->m[2][0]=(short)(sz*sx>>12)+(short)(cz_nsy*cx>>12);
    int sz_nsy=sz*nsy>>12;
    m->m[1][1]=(short)(cz*cx>>12)+(short)(sz_nsy*sx>>12);
    m->m[2][1]=(short)(cz*sx>>12)-(short)(sz_nsy*cx>>12);
    return m;
}
PsxMatrix *RotMatrixZ(long r,PsxMatrix *m)
{
    int s,c; host_psyq_sincos((int)r,&s,&c);
    int m00=m->m[0][0],m01=m->m[0][1],m02=m->m[0][2];
    int m10=m->m[1][0],m11=m->m[1][1],m12=m->m[1][2];
    m->m[0][0]=(short)((c*m00-s*m10)>>12); m->m[0][1]=(short)((c*m01-s*m11)>>12); m->m[0][2]=(short)((c*m02-s*m12)>>12);
    m->m[1][0]=(short)((s*m00+c*m10)>>12); m->m[1][1]=(short)((s*m01+c*m11)>>12); m->m[1][2]=(short)((s*m02+c*m12)>>12);
    return m;
}
int NFSHS_GTE_VectorNormal(const void *,void *);
int VectorNormal(PsxVector *v,PsxVector *o)
{ return NFSHS_GTE_VectorNormal(v,o); }
long VectorNormalS(PsxVector *v,PsxSVector *o)
{ PsxVector q; int n=VectorNormal(v,&q);o->vx=(short)q.vx;o->vy=(short)q.vy;o->vz=(short)q.vz;return n; }

/* ---- Controller: keyboard mapped to a digital PSX pad ----------------------- */
void PadInitDirect(unsigned char *p1,unsigned char *p2) { PsyQPadInitDirect(p1,p2); }
void PadStartCom(void) { PsyQPadStart(); }
void PadStopCom(void) { PsyQPadStop(); }
int PadGetState(int port) { return PsyQPadGetState(port); }
int PadInfoMode(...) { return 0; }
void PadSetAct(...) {}
int PadSetActAlign(...) { return 1; }
void PadSetMainMode(...) {}
void NFSHS_HostPadPoll(void)
{
    pump_messages(); unsigned short keys=0xffff;
#if NFSHS_DIAGNOSTICS
    // Headless input tests supply menu commands/PAD replay themselves. Do not
    // let unrelated desktop typing inject keys into that opt-in test run.
    static int testNoLiveInput=-1;
    if(testNoLiveInput<0)testNoLiveInput=getenv("NFSHS_TEST_NO_LIVE_INPUT")!=0;
    if(!testNoLiveInput)
#endif
    {
        keys &= (unsigned short)~g_pad_buttons;
        if (g_host_race_gas) keys &= (unsigned short)~0x4000;
    }
    
#if NFSHS_DIAGNOSTICS
{
        DWORD now=host_automation_millis();
        if (!g_auto_initialized) {
            GetEnvironmentVariableA("NFSHS_AUTOPILOT",g_auto_sequence,sizeof(g_auto_sequence));
            g_auto_stage_start=now;
            g_auto_initialized=1;
            if (g_auto_sequence[0]) NFSHS_DIAGNOSTIC_CALL(host_log("autopilot sequence=%s",g_auto_sequence));
        }
        if (g_auto_sequence[g_auto_index]) {
            /* 'g' is a terminal, continuously-held accelerator stage.  Menu
               confirmations use short 'x' pulses; once the race is entered,
               `...,g` keeps the same PSX Cross bit down indefinitely. */
            if (g_auto_active &&
                (g_auto_sequence[g_auto_index] != 'g' || !g_host_race_started) &&
                now-g_auto_stage_start >= 1000) {
                g_auto_active=0;
                /* Before Sim_Init has entered the race, terminal `g` must
                   continue producing press/release edges.  A permanently
                   held Cross cannot advance frontend states which sample a
                   fresh key-down.  Once the race starts it becomes the
                   intended continuously held accelerator. */
                if (g_auto_sequence[g_auto_index] != 'g') g_auto_index++;
                g_auto_stage_start=now;
            }
            while (!g_auto_active && g_auto_sequence[g_auto_index] &&
                   (g_auto_sequence[g_auto_index]==' ' || g_auto_sequence[g_auto_index]==',')) g_auto_index++;
            if (!g_auto_active && g_auto_sequence[g_auto_index] && now-g_auto_stage_start >= 1500) {
                g_auto_active=1; g_auto_stage_start=now;
                NFSHS_DIAGNOSTIC_CALL(host_log("autopilot key=%c mask=%04x",g_auto_sequence[g_auto_index],host_auto_mask(g_auto_sequence[g_auto_index])));
            }
            if (g_auto_active) keys &= (unsigned short)~host_auto_mask(g_auto_sequence[g_auto_index]);
        }
    }
#endif

#ifdef AP_WIN
    /* Replay overrides only the emulated hardware packet.  With the opt-in
       environment variable absent this branch is inert and keyboard handling
       above remains byte-for-byte the normal native-port path. */
    NFSHS_DIAGNOSTIC_CALL(host_duck_replay_keys(NFSHS_HostCurrentGameTicks(),&keys));
#endif
    static unsigned short last_keys=0xffff;
    if(keys!=last_keys){NFSHS_DIAGNOSTIC_CALL(host_log("pad keys=%04x",keys));last_keys=keys;}
    PsyQPadPublishDigital(0,1,keys);
    /* Only one keyboard-backed controller exists.  Advertising the same pad
       on port two made every frontend pulse execute twice, eventually drawing
       an uninitialized player-two car-select screen. */
    PsyQPadPublishDigital(0x10,0,0xffff);
}

/* ---- Host file calls --------------------------------------------------------- */
static void host_path(const char *input,char *out,int cap)
{
    while(*input=='\\' || *input=='/') ++input;
    /* The native executable runs from bin while all extracted disc files live
       below bin\\DATA.  Keep the original game filenames untouched above this
       boundary and add the host data root only at the final file-system call. */
    _snprintf(out,cap,"DATA\\%s",input); out[cap-1]=0;
    for(char *p=out;*p;p++){ if(*p=='/')*p='\\'; if(*p==';'){*p=0;break;} }
}
int PCinit(void) { return 0; }
int PCopen(char *name,int mode,int) { char p[520];host_path(name,p,sizeof(p));int fd=_open(p,mode?(_O_RDWR|_O_BINARY):(_O_RDONLY|_O_BINARY));NFSHS_DIAGNOSTIC_CALL(host_log("open fd=%d mode=%d path=%s",fd,mode,p));return fd; }
int PCcreat(char *name,int) { char p[520];host_path(name,p,sizeof(p));return _open(p,_O_CREAT|_O_TRUNC|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE); }
int PCclose(int fd) { return _close(fd); }
int PCread(int fd,void *p,int n) { int r=_read(fd,p,n); NFSHS_DIAGNOSTIC_CALL(static int calls; if(calls++<200)host_log("read fd=%d dst=%p want=%d got=%d",fd,p,n,r)); return r; }
int PCwrite(int fd,const void *p,int n) { return _write(fd,p,n); }
long PClseek(int fd,long off,int whence) { long r=_lseek(fd,off,whence); NFSHS_DIAGNOSTIC_CALL(static int calls; if(calls++<200)host_log("seek fd=%d off=%ld whence=%d -> %ld",fd,off,whence,r)); return r; }

/* ---- CD surface.  Synchronous command behavior is sufficient for extracted files. */
int CdInit(void) { return 1; } int CdReset(int) { return 1; } void CdSetDebug(int) {}
int CdControl(unsigned char, unsigned char *, unsigned char *r) { if(r) memset(r,0,8); return 1; }
int CdControlB(unsigned char c,unsigned char *p,unsigned char *r) { return CdControl(c,p,r); }
void *CdReadyCallback(void *cb) { return cb; } void *CdDataCallback(void *cb) { return cb; }
int CdSync(int,unsigned char *r) { if(r)memset(r,0,8);return 2; } int CdDataSync(int) { return 0; }
int CdDiskReady(int) { return 2; } int CdFlush(void) { return 1; } int CdGetDiskType(void) { return 2; }
int CdGetToc(void *) { return 0; } int CdGetSector(void *,int) { return 0; }
int CdRead2(int,unsigned long *,int) { return 0; }
int CdPosToInt(PsxCdlLoc *p) { return ((p->minute*60+p->second)*75+p->sector)-150; }
PsxCdlLoc *CdIntToPos(int i,PsxCdlLoc *p) { i+=150;p->minute=(unsigned char)(i/4500);i%=4500;p->second=(unsigned char)(i/75);p->sector=(unsigned char)(i%75);p->track=0;return p; }
PsxCdlFile *CdSearchFile(PsxCdlFile *,char *) { return 0; }

/* No persistent memory-card image is configured on Windows.  Preserve PsyQ's
 * accepted-command/async-completion contract and let the game's CRC check
 * reject the deterministic blank medium through its normal no-save path. */
static int g_host_memcard_pending;
static long g_host_memcard_command;
static long g_host_memcard_result;
int MemCardInit(...) { g_host_memcard_pending=0; g_host_memcard_command=0; g_host_memcard_result=0; return 1; }
int MemCardStart(...) { return 1; }
int MemCardStop(...) { return 1; } int MemCardEnd(...) { return 1; }
int MemCardExist(...) { g_host_memcard_command=1; g_host_memcard_result=0; g_host_memcard_pending=1; return 1; }
int MemCardAccept(...) { return 0; }
long MemCardSync(long mode,long *cmd,long *result)
{
    (void)mode;
    if(!g_host_memcard_pending) return -1;
    g_host_memcard_pending=0;
    if(cmd) *cmd=g_host_memcard_command;
    if(result) *result=g_host_memcard_result;
    return 1;
}
int MemCardFormat(...) { return 0; }
int MemCardCreateFile(...) { return -1; } int MemCardDeleteFile(...) { return -1; }
long MemCardReadFile(long channel,char *name,void *data,long offset,long bytes)
{
    (void)channel; (void)name; (void)offset;
    /* No persistent card image is configured yet.  Model a completed virtual
     * read with deterministic blank media; the game's own CRC validation then
     * follows its normal no-save path. */
    if(data && bytes>0) memset(data,0,(size_t)bytes);
    g_host_memcard_command=3; /* PsyQ file I/O command completion. */
    g_host_memcard_result=0;
    g_host_memcard_pending=1;
    return 1;
}
int MemCardWriteFile(...) { return -1; }
int MemCardGetDirentry(...) { return 0; }

/* ---- Explicitly disabled media ---------------------------------------------- */
void DecDCTReset(...) {} void DecDCTin(...) {} int DecDCTinSync(...) { return 0; }
void DecDCTout(...) {} void *DecDCToutCallback(void *p) { return p; }
void DecDCTvlc(...) {} int DecDCTvlcSize(...) { return 0; }
void StSetRing(...) {} void StUnSetRing(...) {} void StSetStream(...) {} void StClearRing(...) {}
void StFreeRing(...) {} void *StGetNext(...) { return 0; } void StCdInterrupt(...) {} int StCdIntrFlag=0;
int FntFlush(...) { return 0; }
int play_movie(...) { return 0; }
void mdecreset(...) {}
void *SetSp(void *p) { return p; }

/* Explicit PSX libgcc calls present in the recovered source.  Native x86 can
 * implement them directly; these are not compiler-emitted helper calls. */
double __adddf3(double a,double b) { return a+b; }
double __divdf3(double a,double b) { return a/b; }
float __divsf3(float a,float b) { return a/b; }
double __extendsfdf2(float a) { return (double)a; }
int __fixdfsi(double a) { return (int)a; }
int __fixsfsi(float a) { return (int)a; }
double __floatsidf(int a) { return (double)a; }
float __floatsisf(int a) { return (float)a; }
int __ltdf2(double a,double b) { return a<b ? -1 : (a>b ? 1 : 0); }
double __muldf3(double a,double b) { return a*b; }
float __mulsf3(float a,float b) { return a*b; }
float __truncdfsf2(double a) { return (float)a; }
}

extern "C" { extern void Platform_InitMemory(void); }
extern "C" { extern char *Platform_ReserveMemory(int,char *); }
extern "C" { extern char *Platform_TempReserveMemory(int,char *); }

static int host_platform_memory_self_test(void) {
    Platform_InitMemory();
    char *a = Platform_ReserveMemory(1,(char *)0);
    char *b = Platform_ReserveMemory(5,(char *)0);
    char *peek = Platform_TempReserveMemory(7,(char *)0);
    char *c = Platform_ReserveMemory(7,(char *)0);
    if (!a || !b || !c) return 1;
    if (b - a != 4 || c - b != 8) return 2;
    if (peek != c) return 3;
    if (((uintptr_t)a & 3U) || ((uintptr_t)b & 3U) || ((uintptr_t)c & 3U)) return 4;
    return 0;
}

extern "C" void NFSHS_ConfigurePsyQHost(void);

extern "C" {
void __main(void) {
    NFSHS_ConfigurePsyQHost();
#if NFSHS_DIAGNOSTICS

    char allocatorTest[2];
    if (GetEnvironmentVariableA("NFSHS_ALLOCATOR_SELFTEST",allocatorTest,sizeof(allocatorTest)))
        ExitProcess((UINT)NFSHS_HostAllocatorSelfTest());
    char platformMemoryTest[2];
    if (GetEnvironmentVariableA("NFSHS_PLATFORM_MEMORY_SELFTEST",platformMemoryTest,
                                sizeof(platformMemoryTest)))
        ExitProcess((UINT)host_platform_memory_self_test());

#else

#endif
}
void __pure_virtual(void) { ExitProcess(3); }

}


