#include <string.h>
#include "sample.h"
#include "spu_core.h"
#include "spu_gaussian.h"

/* Types. */
typedef struct SPU_voice
{
    SPU_voice_registers registers;
    uint32 current_address;
    uint32 repeat_address;
    uint32 phase;
    sint16 history1;
    sint16 history2;
    sint16 decoded[31];
    uint8 decoded_index;
    uint8 decoded_valid;
    uint8 block_flags;
    uint8 first_block;
    uint16 envelope;
    SPU_adsr_phase adsr_phase;
    uint32 envelope_counter;
    uint16 envelope_increment;
    sint16 envelope_step;
    uint8 envelope_rate;
    uint8 envelope_decreasing;
    uint8 envelope_exponential;
} SPU_voice;

typedef struct SPU_state
{
    uint8 ram[SPU_ram_size];
    SPU_voice voices[SPU_voice_count];
    uint32 end_flags;
    sint16 master_left;
    sint16 master_right;
    uint8 initialized;
} SPU_state;

/* Variables. */
static SPU_state spu;

/* Functions. */
void spu_core_init(void)
{
    memset(&spu, 0, sizeof(spu));
    spu.master_left = 0x3fff;
    spu.master_right = 0x3fff;
    spu.initialized = 1;
}

void spu_core_shutdown(void)
{
    memset(&spu, 0, sizeof(spu));
}

sint32 spu_core_upload(uint32 byte_address, const void *source, uint32 byte_count)
{
    if (!spu.initialized || source == 0 || byte_address >= SPU_ram_size || byte_count > SPU_ram_size - byte_address)
        return 0;
    memcpy(spu.ram + byte_address, source, byte_count);
    return 1;
}
sint32 spu_core_download(uint32 byte_address, void *destination, uint32 byte_count)
{
    if (!spu.initialized || destination == 0 || byte_address >= SPU_ram_size || byte_count > SPU_ram_size - byte_address)
        return 0;
    memcpy(destination, spu.ram + byte_address, byte_count);
    return 1;
}

sint32 spu_core_decode_adpcm_block(const uint8 block[16], sint16 *history1, sint16 *history2, sint16 output[28])
{
    static const sint8 filter_pos[16] = {0, 60, 115, 98, 122, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const sint8 filter_neg[16] = {0, 0, -52, -55, -60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    sint32 filter;
    sint32 shift;
    sint32 previous1;
    sint32 previous2;
    sint32 index;

    if (block == 0 || history1 == 0 || history2 == 0 || output == 0)
        return 0;
    filter = block[0] >> 4;
    shift = block[0] & 15;
    if (shift > 12)
        shift = 9;
    previous1 = *history1;
    previous2 = *history2;

    for (index = 0; index < 28; ++index)
    {
        uint8 packed = block[2 + index / 2];
        sint32 nibble = (index & 1) ? (packed >> 4) : (packed & 15);
        sint32 sample;
        if (nibble & 8)
            nibble -= 16;
        sample = (nibble << 12) >> shift;
        sample += (previous1 * filter_pos[filter]) >> 6;
        sample += (previous2 * filter_neg[filter]) >> 6;
        sample = audio_sample_clamp(sample);
        previous2 = previous1;
        previous1 = sample;
        output[index] = (sint16)sample;
    }

    *history1 = (sint16)previous1;
    *history2 = (sint16)previous2;
    return 28;
}

void spu_core_set_voice_registers(sint32 voice, const SPU_voice_registers *registers)
{
    if (!spu.initialized || registers == 0 || voice < 0 || voice >= SPU_voice_count)
        return;
    spu.voices[voice].registers = *registers;
    spu.voices[voice].repeat_address = (uint32)registers->repeat_address * 8u;
}

void spu_core_set_voice_volume(sint32 voice, sint16 left, sint16 right)
{
    if (!spu.initialized || voice < 0 || voice >= SPU_voice_count)
        return;
    spu.voices[voice].registers.volume_left = left;
    spu.voices[voice].registers.volume_right = right;
}

void spu_core_set_voice_pitch(sint32 voice, uint16 pitch)
{
    if (!spu.initialized || voice < 0 || voice >= SPU_voice_count)
        return;
    spu.voices[voice].registers.pitch = pitch;
}

sint32 spu_core_get_voice_registers(sint32 voice, SPU_voice_registers *registers)
{
    if (!spu.initialized || !registers || voice < 0 || voice >= SPU_voice_count)
        return 0;
    *registers = spu.voices[voice].registers;
    return 1;
}

static void envelope_reset(SPU_voice *voice, uint8 rate, uint8 rate_mask, sint32 decreasing, sint32 exponential)
{
    sint32 base_step = 7 - (rate & 3);
    voice->envelope_rate = rate;
    voice->envelope_decreasing = (uint8)decreasing;
    voice->envelope_exponential = (uint8)exponential;
    voice->envelope_counter = 0;
    voice->envelope_increment = 0x8000;
    voice->envelope_step = (sint16)(decreasing ? ~base_step : base_step);
    if (rate < 44)
    {
        voice->envelope_step = (sint16)(voice->envelope_step << (11 - (rate >> 2)));
    }
    else if (rate >= 48)
    {
        voice->envelope_increment = (uint16)(voice->envelope_increment >> ((rate >> 2) - 11));
        if ((rate & rate_mask) == rate_mask)
            voice->envelope_increment = 0;
        else if (voice->envelope_increment == 0)
            voice->envelope_increment = 1;
    }
}

static void update_adsr(SPU_voice *voice)
{
    uint16 adsr1 = voice->registers.adsr1;
    uint16 adsr2 = voice->registers.adsr2;
    switch (voice->adsr_phase)
    {
        case SPU_adsr_attack:
            envelope_reset(voice, (uint8)((adsr1 >> 8) & 0x7f), 0x7f, 0, (adsr1 & 0x8000) != 0);
            break;
        case SPU_adsr_decay:
            envelope_reset(voice, (uint8)(((adsr1 >> 4) & 0x0f) << 2), 0x7c, 1, 1);
            break;
        case SPU_adsr_sustain:
            envelope_reset(voice, (uint8)((adsr2 >> 6) & 0x7f), 0x7f, (adsr2 & 0x4000) != 0, (adsr2 & 0x8000) != 0);
            break;
        case SPU_adsr_release:
            envelope_reset(voice, (uint8)((adsr2 & 0x1f) << 2), 0x7c, 1, (adsr2 & 0x20) != 0);
            break;
        default:
            voice->envelope_counter = 0;
            voice->envelope_increment = 0;
            voice->envelope_step = 0;
            break;
    }
}

static void tick_adsr(SPU_voice *voice)
{
    sint32 step;
    sint32 level;
    uint32 increment;
    sint32 target;
    if (voice->adsr_phase == SPU_adsr_off || voice->envelope_increment == 0)
        return;
    step = voice->envelope_step;
    level = voice->envelope;
    increment = voice->envelope_increment;
    if (voice->envelope_exponential)
    {
        if (voice->envelope_decreasing)
        {
            step = (step * level) >> 15;
        }
        else if (level >= 0x6000)
        {
            if (voice->envelope_rate < 40)
                step >>= 2;
            else if (voice->envelope_rate >= 44)
                increment >>= 2;
            else
            {
                step >>= 1;
                increment >>= 1;
            }
        }
    }
    voice->envelope_counter += increment;
    if (!(voice->envelope_counter & 0x8000))
        return;
    voice->envelope_counter = 0;
    level += step;
    if (level < 0)
        level = 0;
    if (level > 32767)
        level = 32767;
    voice->envelope = (uint16)level;

    if (voice->adsr_phase == SPU_adsr_attack)
        target = 32767;
    else if (voice->adsr_phase == SPU_adsr_decay)
    {
        target = ((voice->registers.adsr1 & 15) + 1) * 0x800;
        if (target > 32767)
            target = 32767;
    }
    else if (voice->adsr_phase == SPU_adsr_release)
        target = 0;
    else
        return;

    if ((!voice->envelope_decreasing && level >= target) || (voice->envelope_decreasing && level <= target))
    {
        if (voice->adsr_phase == SPU_adsr_attack)
            voice->adsr_phase = SPU_adsr_decay;
        else if (voice->adsr_phase == SPU_adsr_decay)
            voice->adsr_phase = SPU_adsr_sustain;
        else
        {
            voice->adsr_phase = SPU_adsr_off;
            voice->envelope = 0;
        }
        update_adsr(voice);
    }
}

static void decode_voice_block(SPU_voice *voice)
{
    uint8 block[16];
    uint32 index;
    for (index = 0; index < 16; ++index)
        block[index] = spu.ram[(voice->current_address + index) & (SPU_ram_size - 1)];
    voice->decoded[0] = voice->decoded[28];
    voice->decoded[1] = voice->decoded[29];
    voice->decoded[2] = voice->decoded[30];
    spu_core_decode_adpcm_block(block, &voice->history1, &voice->history2, voice->decoded + 3);
    voice->block_flags = block[1];
    if (block[1] & 4)
        voice->repeat_address = voice->current_address;
    voice->decoded_valid = 1;
}

static sint32 interpolate_voice(const SPU_voice *voice)
{
    uint32 fraction = (voice->phase >> 4) & 0xff;
    uint32 sample = 3 + (voice->phase >> 12);
    sint32 result = aa_spu_gaussian[0x0ff - fraction] * voice->decoded[sample - 3];
    result += aa_spu_gaussian[0x1ff - fraction] * voice->decoded[sample - 2];
    result += aa_spu_gaussian[0x100 + fraction] * voice->decoded[sample - 1];
    result += aa_spu_gaussian[0x000 + fraction] * voice->decoded[sample];
    return result >> 15;
}

static sint32 fixed_volume(uint16 bits)
{
    if (bits & 0x8000)
        return 0; /* Volume sweeps are outside AA's proven use. */
    return (sint16)(bits << 1);
}

static sint32 apply_volume(sint32 sample, sint32 volume)
{
    return (sample * volume) >> 15;
}

void spu_core_key_on(uint32 voice_mask)
{
    sint32 index;
    if (!spu.initialized)
        return;
    for (index = 0; index < SPU_voice_count; ++index)
    {
        SPU_voice *voice;
        if (!(voice_mask & (1u << index)))
            continue;
        voice = spu.voices + index;
        voice->current_address = (uint32)(voice->registers.start_address & 0xfffeu) * 8u;
        voice->repeat_address = (uint32)voice->registers.repeat_address * 8u;
        voice->phase = 0;
        voice->history1 = 0;
        voice->history2 = 0;
        memset(voice->decoded, 0, sizeof(voice->decoded));
        voice->decoded_index = 0;
        voice->decoded_valid = 0;
        voice->block_flags = 0;
        voice->first_block = 1;
        voice->envelope = 0;
        voice->adsr_phase = SPU_adsr_attack;
        update_adsr(voice);
        spu.end_flags &= ~(1u << index);
    }
}

void spu_core_set_master_volume(sint16 left, sint16 right)
{
    spu.master_left = left;
    spu.master_right = right;
}
void spu_core_get_master_volume(sint16 *left,sint16 *right)
{
    *left=spu.master_left;*right=spu.master_right;
}

void spu_core_key_off(uint32 voice_mask)
{
    sint32 index;
    if (!spu.initialized)
        return;
    for (index = 0; index < SPU_voice_count; ++index)
    {
        SPU_voice *voice;
        if (!(voice_mask & (1u << index)))
            continue;
        voice = spu.voices + index;
        if (voice->adsr_phase != SPU_adsr_off)
            voice->adsr_phase = SPU_adsr_release;
    }
}

uint32 spu_core_end_flags(void)
{
    return spu.end_flags;
}

SPU_adsr_phase spu_core_voice_phase(sint32 voice)
{
    if (voice < 0 || voice >= SPU_voice_count)
        return SPU_adsr_off;
    return spu.voices[voice].adsr_phase;
}

uint16 spu_core_voice_envelope(sint32 voice)
{
    if (voice < 0 || voice >= SPU_voice_count)
        return 0;
    return spu.voices[voice].envelope;
}

void spu_core_render(sint16 *interleaved_stereo, uint32 frame_count)
{
    uint32 frame;
    if (interleaved_stereo == 0)
        return;
    for (frame = 0; frame < frame_count; ++frame)
    {
        sint32 mix_left = 0;
        sint32 mix_right = 0;
        sint32 index;
        for (index = 0; index < SPU_voice_count; ++index)
        {
            SPU_voice *voice = spu.voices + index;
            sint32 sample;
            sint32 amplitude;
            uint32 step;
            if (voice->adsr_phase == SPU_adsr_off)
                continue;
            if (!voice->decoded_valid)
                decode_voice_block(voice);
            sample = interpolate_voice(voice);
            amplitude = apply_volume(sample, voice->envelope);
            mix_left += apply_volume(amplitude, fixed_volume((uint16)voice->registers.volume_left));
            mix_right += apply_volume(amplitude, fixed_volume((uint16)voice->registers.volume_right));
            tick_adsr(voice);
            step = voice->registers.pitch;
            if (step > 0x3fff)
                step = 0x3fff;
            voice->phase += step;
            if ((voice->phase >> 12) >= 28)
            {
                voice->phase -= 28u << 12;
                voice->decoded_valid = 0;
                voice->first_block = 0;
                voice->current_address = (voice->current_address + 16) & (SPU_ram_size - 1);
                if (voice->block_flags & 1)
                {
                    spu.end_flags |= 1u << index;
                    voice->current_address = voice->repeat_address & (SPU_ram_size - 1);
                    if (!(voice->block_flags & 2))
                    {
                        voice->adsr_phase = SPU_adsr_off;
                        voice->envelope = 0;
                    }
                }
            }
        }
        mix_left = apply_volume(mix_left, fixed_volume((uint16)spu.master_left));
        mix_right = apply_volume(mix_right, fixed_volume((uint16)spu.master_right));
        interleaved_stereo[frame * 2] = audio_sample_clamp(mix_left);
        interleaved_stereo[frame * 2 + 1] = audio_sample_clamp(mix_right);
    }
}

#include "diagnostic_state.h"
int spu_core_state_io(FILE *f,int load) { return FF_STATE(f,spu,load); }
