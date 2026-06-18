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

#ifndef SHARED_H_
#define SHARED_H_

/* Convenience stuff... */
#undef INLINE
#if __STDC_VERSION__ >= 199901L
#    define INLINE static inline
#elif defined(__GNUC__) || defined(__GNUG__)
#    define INLINE static __inline__
#else
#    define INLINE static
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <stdint.h>
#include <malloc.h>
#include <math.h>
#include <limits.h>
#include "static_alloc.h"

#ifndef NGC
#ifndef PATH_MAX
#ifdef  MAX_PATH
#define PATH_MAX    MAX_PATH
#else
#define PATH_MAX    1024
#endif
#endif
#endif

#include "build_features.h"
#include "z80.h"
#include "sms.h"
#include "other/eeprom/93c46/eeprom93c46.h"
#include "pio.h"
#include "memz80.h"
#include "video/sms_vdp/vdp.h"
#include "render.h"
#include "video/tms9918/tms.h"
#include "sound/psg/mame_sn76489/sn76489.h"
#include "sound/opl1/opl.h"
#include "sound/opll/fmintf.h"
#include "sound/mixer/sound.h"
#include "system.h"
#include "video/sega_system1/system1.h"
#include "video/snk_ikari_psychos/snk_psychos.h"
#include "video/taito_l/taito_l.h"
#include "loadrom.h"
#include "config.h"
#include "state.h"
#include "inspect.h"
#include "z80_wrap.h"
#include "sound_output.h"

/* For lock and unlocking screen surface */
#include "multirexz80.h"

#ifndef NOZIP_SUPPORT
#include "miniz.h"
#include "fileio.h"
#include "unzip.h"
#endif

#ifdef SCALE2X_UPSCALER
#include "scale2x.h"
#endif

#endif /* _SHARED_H_ */
