/*
 * MultiRexZ80
 *
 * Multi-system Z80 emulator based on SMS Plus GX by Eke-Eke, itself based on
 * SMS Plus by Charles MacDonald.
 *
 * Default project license: GPL-2.0-or-later.  File-specific notices below
 * are retained and take precedence for imported or derived components,
 * including MAME-derived code and other third-party modules.
 */

#ifndef MULTIREXZ80_HEADLESS_PLATFORM_H_
#define MULTIREXZ80_HEADLESS_PLATFORM_H_

#include <stdint.h>

typedef struct multirexz80_headless_platform multirexz80_headless_platform_t;

typedef struct multirexz80_headless_platform_options
{
    const char *input_playback_path;
    const char *input_record_path;
    const char *audio_wav_path;
    const char *trace_path;
    const char *dump_prefix;
    const char *screenshot_path;
    const char *screenshot_prefix;
    const char *video_y4m_path;
    uint32_t dump_every;
    uint32_t screenshot_every;
    uint32_t input_tap_frames;
    uint8_t quiet;
} multirexz80_headless_platform_options_t;

int multirexz80_headless_platform_create(multirexz80_headless_platform_t **out,
                                     const multirexz80_headless_platform_options_t *options);
void multirexz80_headless_platform_destroy(multirexz80_headless_platform_t *platform);
int multirexz80_headless_platform_begin_frame(multirexz80_headless_platform_t *platform, uint64_t frame);
int multirexz80_headless_platform_end_frame(multirexz80_headless_platform_t *platform, uint64_t frame);
int multirexz80_headless_platform_save_final(multirexz80_headless_platform_t *platform, uint64_t frame);

#endif /* MULTIREXZ80_HEADLESS_PLATFORM_H_ */
