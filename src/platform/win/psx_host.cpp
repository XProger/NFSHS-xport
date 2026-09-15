#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#undef __builtin_offsetof
#endif

#include "psx.h"
#include "audio_waveout.h"

extern "C"
{
    int NFSHS_HostResetGraph(int mode);
    void NFSHS_HostFlushCache(void);
    int NFSHS_HostVSync(int mode);
    long NFSHS_HostDrawSync(long mode);
    void NFSHS_HostDrawOTag(unsigned long *ot);
    unsigned long *NFSHS_HostClearOTagR(unsigned long *ot, int count);
    void NFSHS_HostAddPrim(void *ot, void *prim);
    int NFSHS_HostClearImage(void *rectangle, unsigned char red, unsigned char green, unsigned char blue);
    int NFSHS_HostLoadImage(void *rectangle, unsigned long *pixels);
    int NFSHS_HostStoreImage(void *rectangle, unsigned long *pixels);
    int NFSHS_HostMoveImage(void *rectangle, int x, int y);
}

static sint32 host_initialize(void *, const char *, sint32, sint32)
{
    return NFSHS_HostResetGraph(0);
}

static sint32 host_poll(void *)
{
    NFSHS_HostVSync(-1);
    return 1;
}

static void host_wait_vblank(void *)
{
    NFSHS_HostVSync(0);
}

static sint32 host_load_image(void *, PSX_RECT *rectangle, uint32 *pixels)
{
    return NFSHS_HostLoadImage(rectangle, (unsigned long *)pixels);
}

static sint32 host_move_image(void *, PSX_RECT *rectangle, sint32 x, sint32 y)
{
    return NFSHS_HostMoveImage(rectangle, x, y);
}

static sint32 host_store_image(void *, PSX_RECT *rectangle, uint32 *pixels)
{
    return NFSHS_HostStoreImage(rectangle, (unsigned long *)pixels);
}

static sint32 host_clear_image(void *, PSX_RECT *rectangle, uint8 red, uint8 green, uint8 blue)
{
    return NFSHS_HostClearImage(rectangle, red, green, blue);
}

static uint32 *host_clear_ot(void *, uint32 *ot, sint32 count, sint32 reverse)
{
    if (reverse)
        return (uint32 *)NFSHS_HostClearOTagR((unsigned long *)ot, count);
    if (ot == 0 || count <= 0)
        return ot;
    for (sint32 index = 0; index + 1 < count; ++index)
        ot[index] = (uint32)(intptr)&ot[index + 1] & 0x00ffffff;
    ot[count - 1] = 0x00ffffff;
    return ot;
}

static void host_add_prim(void *, void *ot, void *prim)
{
    NFSHS_HostAddPrim(ot, prim);
}

static void host_draw_ot(void *, uint32 *ot)
{
    NFSHS_HostDrawOTag((unsigned long *)ot);
}

static sint32 host_draw_sync(void *, sint32 mode)
{
    return (sint32)NFSHS_HostDrawSync(mode);
}

static void host_flush_cache(void *)
{
    NFSHS_HostFlushCache();
}

static void host_sound_initialize(void *)
{
    PsyQSpuInit();
    waveout_init();
}

static void host_sound_shutdown(void *)
{
    waveout_shutdown();
    PsyQSpuShutdown();
}

static void host_sound_set_master_volume(void *, sint16 left, sint16 right)
{
    PsyQSpuWriteRegister(0x180, (uint16)left);
    PsyQSpuWriteRegister(0x182, (uint16)right);
}

extern "C" void NFSHS_ConfigurePsyQHost(void)
{
    PSX_CONFIG config;
    memset(&config, 0, sizeof(config));
    config.window_title = "Need for Speed: High Stakes - native port";
    config.window_width = 640;
    config.window_height = 480;
    config.refresh_rate = 60;
    config.host.initialize = host_initialize;
    config.host.poll = host_poll;
    config.host.wait_vblank = host_wait_vblank;
    config.host.load_image = host_load_image;
    config.host.move_image = host_move_image;
    config.host.store_image = host_store_image;
    config.host.clear_image = host_clear_image;
    config.host.clear_ot = host_clear_ot;
    config.host.add_prim = host_add_prim;
    config.host.draw_ot = host_draw_ot;
    config.host.draw_sync = host_draw_sync;
    config.host.flush_cache = host_flush_cache;
    config.host.sound_initialize = host_sound_initialize;
    config.host.sound_shutdown = host_sound_shutdown;
    config.host.sound_set_master_volume = host_sound_set_master_volume;
    psx_configure(&config);
    SsInit();
    atexit(SsEnd);
}
