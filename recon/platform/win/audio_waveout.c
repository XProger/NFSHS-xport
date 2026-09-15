#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "audio_waveout.h"

enum
{
    WAVEOUT_BUFFER_COUNT = 2,
    WAVEOUT_SAMPLE_RATE = 44100,
    WAVEOUT_BUFFER_FRAMES = (WAVEOUT_SAMPLE_RATE / 30) * 2
};

typedef struct WAVEOUT_STATE
{
    HWAVEOUT device;
    WAVEHDR headers[WAVEOUT_BUFFER_COUNT];
    sint16 samples[WAVEOUT_BUFFER_COUNT][WAVEOUT_BUFFER_FRAMES * 2];
    HANDLE semaphore;
    HANDLE thread;
    volatile LONG running;
    uint8 prepared[WAVEOUT_BUFFER_COUNT];
} WAVEOUT_STATE;

static WAVEOUT_STATE output;

static void CALLBACK wave_callback(HWAVEOUT device, UINT message, DWORD_PTR instance,
                                   DWORD_PTR parameter1, DWORD_PTR parameter2)
{
    WAVEOUT_STATE *state = (WAVEOUT_STATE *)instance;
    (void)device;
    (void)parameter1;
    (void)parameter2;
    if (message == WOM_DONE && state != 0 && state->semaphore != 0 &&
        InterlockedCompareExchange(&state->running, 1, 1) != 0)
        ReleaseSemaphore(state->semaphore, 1, 0);
}

static DWORD WINAPI wave_thread(void *argument)
{
    WAVEOUT_STATE *state = (WAVEOUT_STATE *)argument;
    while (InterlockedCompareExchange(&state->running, 1, 1) != 0)
    {
        sint32 index;
        WaitForSingleObject(state->semaphore, INFINITE);
        if (InterlockedCompareExchange(&state->running, 1, 1) == 0)
            break;
        for (index = 0; index < WAVEOUT_BUFFER_COUNT; ++index)
        {
            WAVEHDR *header = state->headers + index;
            if (!(header->dwFlags & WHDR_DONE))
                continue;
            if (state->prepared[index])
            {
                if (waveOutUnprepareHeader(state->device, header, sizeof(*header)) != MMSYSERR_NOERROR)
                    continue;
                state->prepared[index] = 0;
            }
            PsyQSpuRender(state->samples[index], WAVEOUT_BUFFER_FRAMES);
            header->dwFlags = 0;
            header->dwLoops = 0;
            if (waveOutPrepareHeader(state->device, header, sizeof(*header)) != MMSYSERR_NOERROR)
                continue;
            state->prepared[index] = 1;
            if (waveOutWrite(state->device, header, sizeof(*header)) != MMSYSERR_NOERROR)
            {
                waveOutUnprepareHeader(state->device, header, sizeof(*header));
                state->prepared[index] = 0;
            }
        }
    }
    return 0;
}

void waveout_shutdown(void)
{
    sint32 index;
    if (output.device == 0 && output.semaphore == 0)
        return;
    InterlockedExchange(&output.running, 0);
    if (output.device != 0)
        waveOutReset(output.device);
    if (output.semaphore != 0)
        ReleaseSemaphore(output.semaphore, 1, 0);
    if (output.thread != 0)
    {
        WaitForSingleObject(output.thread, INFINITE);
        CloseHandle(output.thread);
        output.thread = 0;
    }
    if (output.device != 0)
    {
        for (index = 0; index < WAVEOUT_BUFFER_COUNT; ++index)
            if (output.prepared[index])
                waveOutUnprepareHeader(output.device, output.headers + index, sizeof(WAVEHDR));
        waveOutClose(output.device);
        output.device = 0;
    }
    if (output.semaphore != 0)
    {
        CloseHandle(output.semaphore);
        output.semaphore = 0;
    }
}

sint32 waveout_init(void)
{
    WAVEFORMATEX format;
    MMRESULT result;
    sint32 index;
    if (InterlockedCompareExchange(&output.running, 1, 1) != 0)
        return 1;
    memset(&output, 0, sizeof(output));
    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = WAVEOUT_SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(format.nChannels * sizeof(sint16));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    output.semaphore = CreateSemaphore(0, 0, WAVEOUT_BUFFER_COUNT, 0);
    if (output.semaphore == 0)
        return 0;
    result = waveOutOpen(&output.device, WAVE_MAPPER, &format, (DWORD_PTR)wave_callback,
                         (DWORD_PTR)&output, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        CloseHandle(output.semaphore);
        memset(&output, 0, sizeof(output));
        return 0;
    }
    InterlockedExchange(&output.running, 1);
    output.thread = CreateThread(0, 0, wave_thread, &output, 0, 0);
    if (output.thread == 0)
    {
        waveout_shutdown();
        return 0;
    }
    for (index = 0; index < WAVEOUT_BUFFER_COUNT; ++index)
    {
        WAVEHDR *header = output.headers + index;
        memset(header, 0, sizeof(*header));
        memset(output.samples[index], 0, sizeof(output.samples[index]));
        header->lpData = (LPSTR)output.samples[index];
        header->dwBufferLength = sizeof(output.samples[index]);
        if (waveOutPrepareHeader(output.device, header, sizeof(*header)) != MMSYSERR_NOERROR)
            break;
        output.prepared[index] = 1;
        if (waveOutWrite(output.device, header, sizeof(*header)) != MMSYSERR_NOERROR)
            break;
    }
    if (index != WAVEOUT_BUFFER_COUNT)
    {
        waveout_shutdown();
        return 0;
    }
    return 1;
}

sint32 waveout_is_running(void)
{
    return InterlockedCompareExchange(&output.running, 1, 1) != 0;
}
