/*
 * MultiRexZ80 native Win32 frontend support header.
 */
#ifndef MULTIREXZ80_WIN32_MULTIREXZ80_H_
#define MULTIREXZ80_WIN32_MULTIREXZ80_H_

#include <stdint.h>

#define HOST_WIDTH_RESOLUTION 960
#define HOST_HEIGHT_RESOLUTION 720

#define VIDEO_WIDTH_SMS 256
#define VIDEO_HEIGHT_SMS 192
#define VIDEO_WIDTH_GG 160
#define VIDEO_HEIGHT_GG 144

#define LOCK_VIDEO   do { } while (0);
#define UNLOCK_VIDEO do { } while (0);

#define SOUND_FREQUENCY 44100

void smsp_state(uint8_t slot_number, uint8_t mode);

#endif /* MULTIREXZ80_WIN32_MULTIREXZ80_H_ */
