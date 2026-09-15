#ifndef PSX_H
#define PSX_H

/* Portable PsyQ compatibility surface. Copy psx.c and psx.h together into a
 * host project. With no configuration, ResetGraph creates the built-in Win32
 * window and VSync drives its event loop at 50 Hz. Other hosts install
 * PSX_HOST_CALLBACKS with psx_configure before calling ResetGraph. Game code
 * continues to use the original PsyQ names and packet ABI unchanged. */

#include <stddef.h>
#include <stdint.h>
void psx_gte_write_h(uint16_t h);

/* WinUser maps its LoadImage name to LoadImageA/W. PsyQ owns the unsuffixed
 * API name in programs that include this wrapper. */
#if defined(LoadImage)
    #undef LoadImage
#endif

/* Fixed-width host types used by the PsyQ-compatible API. */
typedef uint8_t uint8;
typedef int8_t sint8;
typedef uint16_t uint16;
typedef int16_t sint16;
typedef uint32_t uint32;
typedef int32_t sint32;
typedef uint64_t uint64;
typedef int64_t sint64;
typedef intptr_t intptr;

#if INTPTR_MAX == INT32_MAX
    #define AP_32BIT 1
#endif

/* Win32 maps LoadImage to LoadImageA/W, which collides with PsyQ libgpu. */
#if defined(LoadImage)
    #undef LoadImage
#endif

/* Public PsyQ-compatible ABI. Game code includes only this file; it has no
 * dependency on an installed PsyQ SDK or on this project's game headers. */
#define ONE 4096

#define PADLup (1 << 12)
#define PADLdown (1 << 14)
#define PADLleft (1 << 15)
#define PADLright (1 << 13)
#define PADRup (1 << 4)
#define PADRdown (1 << 6)
#define PADRleft (1 << 7)
#define PADRright (1 << 5)
#define PADi (1 << 9)
#define PADj (1 << 10)
#define PADk (1 << 8)
#define PADl (1 << 3)
#define PADm (1 << 1)
#define PADn (1 << 2)
#define PADo (1 << 0)
#define PADh (1 << 11)
#define PADL1 PADn
#define PADL2 PADo
#define PADR1 PADl
#define PADR2 PADm
#define PADstart PADh
#define PADselect PADk
#define _PAD(controller, buttons) ((buttons) << ((controller) << 4))

#define MODE_NTSC 0
#define MODE_PAL 1

typedef struct
{
    sint16 m[3][3];
    sint32 t[3];
} MATRIX;

typedef struct
{
    sint32 vx;
    sint32 vy;
    sint32 vz;
    sint32 pad;
} VECTOR;

typedef struct
{
    sint16 vx;
    sint16 vy;
    sint16 vz;
    sint16 pad;
} SVECTOR;

typedef struct
{
    uint8 r;
    uint8 g;
    uint8 b;
    uint8 cd;
} CVECTOR;

typedef struct
{
    sint16 vx;
    sint16 vy;
} DVECTOR;

typedef struct
{
    sint16 x;
    sint16 y;
    sint16 w;
    sint16 h;
} PSX_RECT;

typedef struct
{
    uint32 tag;
    uint32 code[15];
} DR_ENV;

typedef struct
{
    PSX_RECT clip;
    sint16 ofs[2];
    PSX_RECT tw;
    uint16 tpage;
    uint8 dtd;
    uint8 dfe;
    uint8 isbg;
    uint8 r0;
    uint8 g0;
    uint8 b0;
    DR_ENV dr_env;
} DRAWENV;

typedef struct
{
    PSX_RECT disp;
    PSX_RECT screen;
    uint8 isinter;
    uint8 isrgb24;
    uint8 pad0;
    uint8 pad1;
} DISPENV;

typedef struct
{
    uint32 addr : 24;
    uint32 len : 8;
    uint8 r0;
    uint8 g0;
    uint8 b0;
    uint8 code;
} P_TAG;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    sint16 x1, y1;
    sint16 x2, y2;
} POLY_F3;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    sint16 x1, y1;
    sint16 x2, y2;
    sint16 x3, y3;
} POLY_F4;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
    sint16 x1, y1;
    uint8 u1, v1;
    uint16 tpage;
    sint16 x2, y2;
    uint8 u2, v2;
    uint16 pad1;
} POLY_FT3;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
    sint16 x1, y1;
    uint8 u1, v1;
    uint16 tpage;
    sint16 x2, y2;
    uint8 u2, v2;
    uint16 pad1;
    sint16 x3, y3;
    uint8 u3, v3;
    uint16 pad2;
} POLY_FT4;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 r1, g1, b1, pad1;
    sint16 x1, y1;
    uint8 r2, g2, b2, pad2;
    sint16 x2, y2;
} POLY_G3;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 r1, g1, b1, pad1;
    sint16 x1, y1;
    uint8 r2, g2, b2, pad2;
    sint16 x2, y2;
    uint8 r3, g3, b3, pad3;
    sint16 x3, y3;
} POLY_G4;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
    uint8 r1, g1, b1, pad1;
    sint16 x1, y1;
    uint8 u1, v1;
    uint16 tpage;
    uint8 r2, g2, b2, pad2;
    sint16 x2, y2;
    uint8 u2, v2;
    uint16 pad3;
} POLY_GT3;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
    uint8 r1, g1, b1, pad1;
    sint16 x1, y1;
    uint8 u1, v1;
    uint16 tpage;
    uint8 r2, g2, b2, pad2;
    sint16 x2, y2;
    uint8 u2, v2;
    uint16 pad3;
    uint8 r3, g3, b3, pad4;
    sint16 x3, y3;
    uint8 u3, v3;
    uint16 pad5;
} POLY_GT4;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
    sint16 w, h;
} SPRT;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    uint8 u0, v0;
    uint16 clut;
} SPRT_16;

typedef SPRT_16 SPRT_8;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
    sint16 w, h;
} TILE;

typedef struct
{
    uint32 tag;
    uint8 r0, g0, b0, code;
    sint16 x0, y0;
} TILE_16;

typedef TILE_16 TILE_8;
typedef TILE_16 TILE_1;

typedef struct
{
    uint32 tag;
    uint32 code[2];
} DR_MODE;

typedef DR_MODE DR_TWIN;
typedef DR_MODE DR_AREA;
typedef DR_MODE DR_OFFSET;

typedef struct
{
    uint32 tag;
    uint32 code[5];
} DR_MOVE;

typedef struct
{
    uint32 tag;
    uint32 code[3];
    uint32 p[13];
} DR_LOAD;

typedef struct
{
    uint32 tag;
    uint32 code[1];
} DR_TPAGE;

typedef DR_MODE DR_STP;

#define setVector(vector, _x, _y, _z) ((vector)->vx = (_x), (vector)->vy = (_y), (vector)->vz = (_z))
#define setRECT(rect, _x, _y, width, height) ((rect)->x = (_x), (rect)->y = (_y), (rect)->w = (width), (rect)->h = (height))
#define setRGB0(packet, red, green, blue) ((packet)->r0 = (red), (packet)->g0 = (green), (packet)->b0 = (blue))
#define setRGB1(packet, red, green, blue) ((packet)->r1 = (red), (packet)->g1 = (green), (packet)->b1 = (blue))
#define setRGB2(packet, red, green, blue) ((packet)->r2 = (red), (packet)->g2 = (green), (packet)->b2 = (blue))
#define setRGB3(packet, red, green, blue) ((packet)->r3 = (red), (packet)->g3 = (green), (packet)->b3 = (blue))
#define setXY0(packet, _x, _y) ((packet)->x0 = (_x), (packet)->y0 = (_y))
#define setXY2(packet, _x0, _y0, _x1, _y1) ((packet)->x0 = (_x0), (packet)->y0 = (_y0), (packet)->x1 = (_x1), (packet)->y1 = (_y1))
#define setXY3(packet, _x0, _y0, _x1, _y1, _x2, _y2) (setXY2(packet, _x0, _y0, _x1, _y1), (packet)->x2 = (_x2), (packet)->y2 = (_y2))
#define setXY4(packet, _x0, _y0, _x1, _y1, _x2, _y2, _x3, _y3) (setXY3(packet, _x0, _y0, _x1, _y1, _x2, _y2), (packet)->x3 = (_x3), (packet)->y3 = (_y3))
#define setWH(packet, width, height) ((packet)->w = (width), (packet)->h = (height))
#define setUV0(packet, _u, _v) ((packet)->u0 = (_u), (packet)->v0 = (_v))
#define setUV3(packet, _u0, _v0, _u1, _v1, _u2, _v2) ((packet)->u0 = (_u0), (packet)->v0 = (_v0), (packet)->u1 = (_u1), (packet)->v1 = (_v1), (packet)->u2 = (_u2), (packet)->v2 = (_v2))
#define setUV4(packet, _u0, _v0, _u1, _v1, _u2, _v2, _u3, _v3) (setUV3(packet, _u0, _v0, _u1, _v1, _u2, _v2), (packet)->u3 = (_u3), (packet)->v3 = (_v3))
#define setlen(packet, length) (((P_TAG *)(packet))->len = (uint8)(length))
#define setaddr(packet, address) (((P_TAG *)(packet))->addr = (uint32)(intptr)(address))
#define setcode(packet, command) (((P_TAG *)(packet))->code = (uint8)(command))
#define getlen(packet) ((uint8)((P_TAG *)(packet))->len)
#define getaddr(packet) ((uint32)((P_TAG *)(packet))->addr)
#define getcode(packet) ((uint8)((P_TAG *)(packet))->code)
#define nextPrim(packet) ((void *)(intptr)(getaddr(packet) | 0x80000000UL))
#define isendprim(packet) (getaddr(packet) == 0xffffffUL)
#define addPrim(ot, packet) (setaddr(packet, getaddr(ot)), setaddr(ot, packet))
#define addPrims(ot, first, last) (setaddr(last, getaddr(ot)), setaddr(ot, first))
#define termPrim(packet) setaddr(packet, 0xffffffffUL)
/* Original: SLES_004.74 FUN_800B262C (primitive_set_semitransparency). */
#define setSemiTrans(packet, enabled) ((enabled) ? setcode(packet, getcode(packet) | 2) : setcode(packet, getcode(packet) & ~2))
/* Original: SLES_004.74 FUN_800B2654 (primitive_set_texture_shading). */
#define setShadeTex(packet, raw_texture) ((raw_texture) ? setcode(packet, getcode(packet) | 1) : setcode(packet, getcode(packet) & ~1))
#define getTPage(depth, abr, x, y) ((((depth) & 3) << 7) | (((abr) & 3) << 5) | (((y) & 0x100) >> 4) | (((x) & 0x3ff) >> 6) | (((y) & 0x200) << 2))
#define getClut(x, y) (((y) << 6) | (((x) >> 4) & 0x3f))
#define setTPage(packet, depth, abr, x, y) ((packet)->tpage = (uint16)getTPage(depth, abr, x, y))
#define setClut(packet, x, y) ((packet)->clut = (uint16)getClut(x, y))
#define psx_draw_mode(dfe, dtd, tpage) (0xe1000000UL | ((dtd) ? 0x0200UL : 0) | ((dfe) ? 0x0400UL : 0) | ((tpage) & 0x9ff))
#define psx_texture_window(texture_window) ((texture_window) ? (0xe2000000UL | (((texture_window)->y & 0xff) >> 3 << 15) | (((texture_window)->x & 0xff) >> 3 << 10) | ((~((texture_window)->h - 1) & 0xff) >> 3 << 5) | ((~((texture_window)->w - 1) & 0xff) >> 3)) : 0)
#define setDrawMode(packet, dfe, dtd, tpage, texture_window) (setlen(packet, 2), ((uint32 *)(packet))[1] = psx_draw_mode(dfe, dtd, tpage), ((uint32 *)(packet))[2] = psx_texture_window((PSX_RECT *)(texture_window)))
#define setTexWindow(packet, texture_window) (setlen(packet, 2), ((uint32 *)(packet))[1] = psx_texture_window((PSX_RECT *)(texture_window)), ((uint32 *)(packet))[2] = 0)
#define setPolyF3(packet) (setlen(packet, 4), setcode(packet, 0x20))
#define setPolyFT3(packet) (setlen(packet, 7), setcode(packet, 0x24))
#define setPolyG3(packet) (setlen(packet, 6), setcode(packet, 0x30))
#define setPolyGT3(packet) (setlen(packet, 9), setcode(packet, 0x34))
#define setPolyF4(packet) (setlen(packet, 5), setcode(packet, 0x28))
/* Original: SLES_004.74 FUN_800B26E0 (primitive_initialize_poly_ft4). */
#define setPolyFT4(packet) (setlen(packet, 9), setcode(packet, 0x2c))
#define setPolyG4(packet) (setlen(packet, 8), setcode(packet, 0x38))
#define setPolyGT4(packet) (setlen(packet, 12), setcode(packet, 0x3c))
#define setSprt8(packet) (setlen(packet, 3), setcode(packet, 0x74))
#define setSprt16(packet) (setlen(packet, 3), setcode(packet, 0x7c))
#define setSprt(packet) (setlen(packet, 4), setcode(packet, 0x64))
#define setTile1(packet) (setlen(packet, 2), setcode(packet, 0x68))
#define setTile8(packet) (setlen(packet, 2), setcode(packet, 0x70))
#define setTile16(packet) (setlen(packet, 2), setcode(packet, 0x78))
#define setTile(packet) (setlen(packet, 3), setcode(packet, 0x60))

/* PsyQ declares these primitive initializers as macros. Keep the familiar
 * uppercase spellings used by reconstructed game code without adding wrapper
 * functions or another packet-initialization semantic. */
#define SetDrawMode(packet, dfe, dtd, tpage, texture_window) setDrawMode(packet, dfe, dtd, tpage, texture_window)
#define SetPolyF3(packet) setPolyF3(packet)
#define SetPolyF4(packet) setPolyF4(packet)
#define SetPolyFT3(packet) setPolyFT3(packet)
#define SetPolyFT4(packet) setPolyFT4(packet)
#define SetPolyG3(packet) setPolyG3(packet)
#define SetPolyG4(packet) setPolyG4(packet)
#define SetPolyGT3(packet) setPolyGT3(packet)
#define SetPolyGT4(packet) setPolyGT4(packet)
#define SetSemiTrans(packet, enabled) setSemiTrans(packet, enabled)
#define SetShadeTex(packet, raw_texture) setShadeTex(packet, raw_texture)
#define SetSprt(packet) setSprt(packet)
#define SetSprt16(packet) setSprt16(packet)
#define SetSprt8(packet) setSprt8(packet)
#define SetTexWindow(packet, texture_window) setTexWindow(packet, texture_window)
#define SetTile(packet) setTile(packet)
#define SetTile1(packet) setTile1(packet)
#define SetTile16(packet) setTile16(packet)
#define SetTile8(packet) setTile8(packet)

typedef char PsxUint8MustBe1Byte[sizeof(uint8) == 1 ? 1 : -1];
typedef char PsxSint8MustBe1Byte[sizeof(sint8) == 1 ? 1 : -1];
typedef char PsxUint16MustBe2Bytes[sizeof(uint16) == 2 ? 1 : -1];
typedef char PsxSint16MustBe2Bytes[sizeof(sint16) == 2 ? 1 : -1];
typedef char PsxUint32MustBe4Bytes[sizeof(uint32) == 4 ? 1 : -1];
typedef char PsxSint32MustBe4Bytes[sizeof(sint32) == 4 ? 1 : -1];
typedef char PsxUint64MustBe8Bytes[sizeof(uint64) == 8 ? 1 : -1];
typedef char PsxSint64MustBe8Bytes[sizeof(sint64) == 8 ? 1 : -1];
typedef char PsxIntptrMustMatchPointer[sizeof(intptr) == sizeof(void *) ? 1 : -1];
typedef char PsxPolyFt3SizeMustBe20[sizeof(POLY_FT3) == 0x20 ? 1 : -1];
typedef char PsxPolyFt4SizeMustBe28[sizeof(POLY_FT4) == 0x28 ? 1 : -1];
typedef char PsxPolyFt4ClutMustBe0e[offsetof(POLY_FT4, clut) == 0x0e ? 1 : -1];
typedef char PsxPolyFt4TpageMustBe16[offsetof(POLY_FT4, tpage) == 0x16 ? 1 : -1];

typedef struct PSX_HOST_CALLBACKS
{
    sint32 (*initialize)(void *user, const char *title, sint32 width, sint32 height);
    sint32 (*poll)(void *user);
    uint32 (*pad_read)(void *user, sint32 controller);
    void (*wait_vblank)(void *user);
    sint32 (*present)(void *user, const uint32 *pixels, sint32 width, sint32 height, const char *title);
    sint32 (*load_image)(void *user, PSX_RECT *rectangle, uint32 *pixels);
    sint32 (*move_image)(void *user, PSX_RECT *rectangle, sint32 x, sint32 y);
    sint32 (*store_image)(void *user, PSX_RECT *rectangle, uint32 *pixels);
    sint32 (*clear_image)(void *user, PSX_RECT *rectangle, uint8 red, uint8 green, uint8 blue);
    uint32 *(*clear_ot)(void *user, uint32 *ot, sint32 count, sint32 reverse);
    void (*add_prim)(void *user, void *ot, void *prim);
    void (*add_prims)(void *user, void *ot, void *first, void *last);
    void (*draw_ot)(void *user, uint32 *ot);
    sint32 (*draw_sync)(void *user, sint32 mode);
    void (*flush_cache)(void *user);
    void (*sound_initialize)(void *user);
    void (*sound_shutdown)(void *user);
    void (*sound_set_tick_mode)(void *user, sint32 mode);
    void (*sound_start)(void *user);
    void (*sound_set_master_volume)(void *user, sint16 left, sint16 right);
    void (*sound_set_serial_attributes)(void *user, sint8 serial, sint8 attribute, sint8 value);
    void (*sound_set_serial_volume)(void *user, sint8 serial, sint16 left, sint16 right);
} PSX_HOST_CALLBACKS;

typedef struct PSX_CONFIG
{
    const char *window_title;
    sint32 window_width;
    sint32 window_height;
    sint32 refresh_rate;
    sint32 headless;
    void *host_user;
    PSX_HOST_CALLBACKS host;
} PSX_CONFIG;

#if defined(__cplusplus)
extern "C"
{
#endif

    enum { PSYQ_GPU_VRAM_WIDTH = 1024, PSYQ_GPU_VRAM_HEIGHT = 512 };
    enum { PSYQ_SPU_VOICE_COUNT = 24, PSYQ_SPU_REGISTER_BYTES = 0x200 };
    extern uint16_t PsyQ_gpu_vram[PSYQ_GPU_VRAM_WIDTH * PSYQ_GPU_VRAM_HEIGHT];
    extern volatile uint16_t VOICE_00_LEFT_RIGHT[PSYQ_SPU_REGISTER_BYTES / 2];
    int ccos(int angle);
    int csin(int angle);
    int csincos(int angle, int *sine, int *cosine);
    void PsyQGpuResetVram(void);
    int PsyQGpuClearImage(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue);
    int PsyQGpuLoadImage(int x, int y, int width, int height, const uint16_t *pixels);
    int PsyQGpuStoreImage(int x, int y, int width, int height, uint16_t *pixels);
    int PsyQGpuMoveImage(int x, int y, int width, int height, int destination_x, int destination_y);
    uint16_t PsyQGpuGetClut(int x, int y);
    uint16_t PsyQGpuGetTPage(int texture_mode, int blend_mode, int x, int y);
    void PsyQPadInitDirect(uint8_t *first, uint8_t *second);
    void PsyQPadStart(void);
    void PsyQPadStop(void);
    int PsyQPadGetState(int port);
    void PsyQPadPublishDigital(int port, int connected, uint16_t active_low_buttons);
    void PsyQSpuInit(void);
    void PsyQSpuShutdown(void);
    void PsyQSpuSyncVoice(int voice);
    void PsyQSpuSyncAllVoices(void);
    void PsyQSpuKeyOn(uint32_t voice_mask);
    void PsyQSpuKeyOff(uint32_t voice_mask);
    int PsyQSpuDmaWrite(const void *source, uint32_t byte_address, uint32_t byte_count);
    int PsyQSpuDmaRead(void *destination, uint32_t byte_address, uint32_t byte_count);
    void PsyQSpuRender(int16_t *interleaved_stereo, uint32_t frame_count);
    void PsyQSpuAdvance(uint32_t frame_count);
    uint16_t PsyQSpuReadRegister(uint32_t byte_offset);
    void PsyQSpuWriteRegister(uint32_t byte_offset, uint16_t value);

    void AddPrim(void *ot, void *prim);
    void AddPrims(void *ot, void *first, void *last);
    VECTOR *ApplyMatrix(MATRIX *matrix, SVECTOR *input, VECTOR *output);
    VECTOR *ApplyMatrixLV(MATRIX *matrix, VECTOR *input, VECTOR *output);
    VECTOR *ApplyRotMatrix(SVECTOR *input, VECTOR *output);
    SVECTOR *ApplyMatrixSV(MATRIX *matrix, SVECTOR *input, SVECTOR *output);
    sint32 AverageZ3(sint32 z0, sint32 z1, sint32 z2);
    sint32 AverageZ4(sint32 z0, sint32 z1, sint32 z2, sint32 z3);
    sint32 ClearImage(PSX_RECT *rectangle, uint8 red, uint8 green, uint8 blue);
    uint32 *ClearOTag(uint32 *ot, sint32 count);
    uint32 *ClearOTagR(uint32 *ot, sint32 count);
    sint32 DrawSync(sint32 mode);
    void DrawOTag(uint32 *ot);
    void FlushCache(void);
    uint16 GetClut(sint32 x, sint32 y);
    uint16 GetTPage(sint32 depth, sint32 abr, sint32 x, sint32 y);
    void InitGeom(void);
    sint32 LoadImage(PSX_RECT *rectangle, uint32 *pixels);
    uint16 LoadClut(uint32 *pixels, sint32 x, sint32 y);
    uint16 LoadTPage(uint32 *pixels, sint32 depth, sint32 abr, sint32 x, sint32 y, sint32 width, sint32 height);
    MATRIX *MulMatrix0(MATRIX *left, MATRIX *right, MATRIX *output);
    sint32 MoveImage(PSX_RECT *rectangle, sint32 x, sint32 y);
    sint32 NormalClip(sint32 point0, sint32 point1, sint32 point2);
    uint32 PadRead(sint32 controller);
    void PadInit(sint32 mode);
    void PopMatrix(void);
    void PushMatrix(void);
    void ReadGeomOffset(sint32 *x, sint32 *y);
    sint32 ReadGeomScreen(void);
    void ReadRotMatrix(MATRIX *matrix);
    sint32 ResetGraph(sint32 mode);
    sint32 ResetCallback(void);
    MATRIX *RotMatrix(SVECTOR *rotation, MATRIX *matrix);
    MATRIX *RotMatrixYXZ(SVECTOR *rotation, MATRIX *matrix);
    void RotTrans(SVECTOR *input, VECTOR *output, sint32 *flags);
    sint32 RotTransPers(SVECTOR *input, sint32 *screen_xy, sint32 *projection, sint32 *flags);
    void RotTransSV(SVECTOR *input, SVECTOR *output, sint32 *flags);
    MATRIX *ScaleMatrix(MATRIX *matrix, VECTOR *scale);
    MATRIX *ScaleMatrixL(MATRIX *matrix, VECTOR *scale);
    void SetDispMask(sint32 enabled);
    void SetGeomOffset(sint32 x, sint32 y);
    void SetGeomScreen(sint32 distance);
    sint32 SetGraphDebug(sint32 level);
    void SetRotMatrix(MATRIX *matrix);
    void SetTransMatrix(MATRIX *matrix);
    sint32 StoreImage(PSX_RECT *rectangle, uint32 *pixels);
    void SsEnd(void);
    void SsInit(void);
    void SsSetMVol(sint16 left, sint16 right);
    void SsSetSerialAttr(sint8 serial, sint8 attribute, sint8 value);
    void SsSetSerialVol(sint8 serial, sint16 left, sint16 right);
    void SsSetTickMode(sint32 mode);
    void SsStart(void);
    MATRIX *TransMatrix(MATRIX *matrix, VECTOR *translation);
    sint32 VSync(sint32 mode);
    sint32 psx_fixed_cos(sint32 angle);
    sint32 psx_fixed_init(void);
    sint32 psx_fixed_mul12(sint32 value, sint32 coefficient);
    sint32 psx_fixed_sin(sint32 angle);
    void psx_gte_get_state(sint32 *distance, sint32 *offset_x, sint32 *offset_y);
    sint32 psx_gte_project(const SVECTOR *input, sint32 *screen_xy, sint32 *flags);
    void psx_gte_transform(const SVECTOR *input, VECTOR *output, sint32 *flags);
    void psx_configure(const PSX_CONFIG *config);
    sint32 psx_poll(void);
    sint32 psx_quit_requested(void);
    sint32 psx_window_present(const uint32 *pixels, sint32 width, sint32 height, const char *title);
    void psx_set_headless(sint32 headless);
    sint32 psx_is_headless(void);

#if defined(__cplusplus)
}
#endif

#endif
