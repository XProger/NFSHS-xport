#ifndef SPU_core_h
#define SPU_core_h

#include "psx.h"

#define SPU_ram_size 0x80000u
#define SPU_voice_count 24
#define SPU_sample_rate 44100

typedef enum SPU_adsr_phase
{
    SPU_adsr_off = 0,
    SPU_adsr_attack,
    SPU_adsr_decay,
    SPU_adsr_sustain,
    SPU_adsr_release
} SPU_adsr_phase;

typedef struct SPU_voice_registers
{
    sint16 volume_left;
    sint16 volume_right;
    uint16 pitch;
    uint16 start_address;
    uint16 adsr1;
    uint16 adsr2;
    uint16 repeat_address;
} SPU_voice_registers;

/* Exposed for deterministic block vectors. `history1` is the immediately
 * preceding decoded sample; `history2` is the sample before it. */

/* BEGIN GENERATED MODULE API */
SPU_adsr_phase spu_core_voice_phase(sint32 voice);
sint32 spu_core_decode_adpcm_block(const uint8 block[16], sint16 *history1, sint16 *history2, sint16 output[28]);
sint32 spu_core_get_voice_registers(sint32 voice, SPU_voice_registers *registers);
sint32 spu_core_upload(uint32 byte_address, const void *source, uint32 byte_count);
sint32 spu_core_download(uint32 byte_address, void *destination, uint32 byte_count);
uint32 spu_core_end_flags(void);
uint16 spu_core_voice_envelope(sint32 voice);
void spu_core_init(void);
void spu_core_key_off(uint32 voice_mask);
void spu_core_key_on(uint32 voice_mask);
void spu_core_render(sint16 *interleaved_stereo, uint32 frame_count);
void spu_core_set_master_volume(sint16 left, sint16 right);
void spu_core_get_master_volume(sint16 *left,sint16 *right);
void spu_core_set_voice_pitch(sint32 voice, uint16 pitch);
void spu_core_set_voice_registers(sint32 voice, const SPU_voice_registers *registers);
void spu_core_set_voice_volume(sint32 voice, sint16 left, sint16 right);
void spu_core_shutdown(void);
/* END GENERATED MODULE API */

#endif
