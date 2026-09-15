#ifndef AUDIO_WAVEOUT_H
#define AUDIO_WAVEOUT_H

#include "psx.h"

#if defined(__cplusplus)
extern "C" {
#endif

sint32 waveout_init(void);
void waveout_shutdown(void);
sint32 waveout_is_running(void);

#if defined(__cplusplus)
}
#endif

#endif
