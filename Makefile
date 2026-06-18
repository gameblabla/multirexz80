PRGNAME     = multirexz80.elf
CC			= gcc

# Possible choices : rs97, k3s (PAP K3S), sdl, amini, fbdev
PORT = sdl
# Possible choices : alsa, pulse (pulseaudio), oss, sdl12 (SDL 1.2 sound output), portaudio, libao
SOUND_OUTPUT = pulse
# PSG backend: only the MAME-derived SN76489 core is shipped.
PSG_ENGINE ?= mame_sn76489
ifneq ($(origin SOUND_ENGINE), undefined)
PSG_ENGINE := $(SOUND_ENGINE)
endif
# Possible choices : z80 (accurate but proprietary), eighty (EightyZ80's core, GPLv2)
Z80_CORE = z80
SCALE2X_UPSCALER = 1
PROFILE_DIR ?= .
PROFILE = 0
ZIP_SUPPORT = 1
STATIC_MEMORY ?= 0
STATIC_MEMORY_BYTES ?= 16777216
ENABLE_ARCADE ?= 1
ENABLE_COLECO ?= 1
ENABLE_SORDM5 ?= 1
FEATURE_DEFS := -DMULTIREXZ80_ENABLE_ARCADE=$(ENABLE_ARCADE) -DMULTIREXZ80_ENABLE_COLECO=$(ENABLE_COLECO) -DMULTIREXZ80_ENABLE_SORDM5=$(ENABLE_SORDM5)
# Set RENDER_BPP=32 for native XRGB8888 output in modern ports.
RENDER_BPP ?= 16

SRCDIR = ./source ./source/other/eeprom/93c46 ./source/other/decode ./source/video/sms_vdp ./source/video/tms9918 ./source/sound/mixer ./source/sound/opll ./source/sound/opl1 ./source/cpu_cores/$(Z80_CORE) ./source/unzip
SRCDIR += ./source/scalers ./source/ports/$(PORT) ./source/ports/sdl12_common ./source/sound/psg/$(PSG_ENGINE) ./source/sound_output/$(SOUND_OUTPUT)

ifeq ($(ENABLE_COLECO),1)
SRCDIR += ./source/platform/coleco
endif
ifeq ($(ENABLE_SORDM5),1)
SRCDIR += ./source/platform/sord_m5
endif
ifeq ($(ENABLE_ARCADE),1)
SRCDIR += ./source/video/sega_system1 ./source/video/snk_ikari_psychos ./source/video/taito_l
else
SRCDIR += ./source/platform/arcade_disabled
endif
VPATH		= $(SRCDIR)
SRC_C		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
SRC_CP		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.cpp))
OBJ_C		= $(notdir $(patsubst %.c, %.o, $(SRC_C)))
OBJ_CP		= $(notdir $(patsubst %.cpp, %.o, $(SRC_CP)))
OBJS		= $(OBJ_C) $(OBJ_CP)

CFLAGS		= -O0 -g3 -Wall -Wextra
CFLAGS		+= $(FEATURE_DEFS) -DLSB_FIRST -std=gnu99 -DALIGN_DWORD -DNOYUV
CFLAGS += -Isource -Isource/other/eeprom/93c46 -Isource/other/decode -Isource/platform/coleco -Isource/platform/sord_m5 -Isource/video/sms_vdp -Isource/video/tms9918 -Isource/video/sega_system1 -Isource/video/snk_ikari_psychos -Isource/video/taito_l -Isource/sound/mixer -Isource/sound/opll -Isource/sound/opl1 -Isource/sound/psg/$(PSG_ENGINE) -Isource/cpu_cores/$(Z80_CORE) -Isource/scalers -Isource/ports/$(PORT) -Isource/ports/sdl12_common -Isource/unzip -Isource/sdl -Isource/sound_output

SRCDIR		+= ./source/text/fb
CFLAGS		+= -Isource/text/fb

ifeq ($(PROFILE), YES)
CFLAGS 		+= -fprofile-generate=$(PROFILE_DIR)
else ifeq ($(PROFILE), APPLY)
CFLAGS		+= -fprofile-use=$(PROFILE_DIR) -fbranch-probabilities
endif

ifeq ($(PSG_ENGINE), mame_sn76489)
CFLAGS 		+= -DMAME_PSG
endif


ifeq ($(ZIP_SUPPORT), 0)
CFLAGS 		+= -DNOZIP_SUPPORT
endif

ifeq ($(STATIC_MEMORY), 1)
CFLAGS 		+= -DMULTIREXZ80_STATIC_MEMORY -DMULTIREXZ80_STATIC_MEMORY_BYTES=$(STATIC_MEMORY_BYTES)
endif

ifeq ($(RENDER_BPP), 32)
CFLAGS 		+= -DMULTIREXZ80_RENDER_32BPP
endif

ifeq ($(SCALE2X_UPSCALER), 1)
CFLAGS 		+= -DSCALE2X_UPSCALER
CFLAGS		+= -Isource/scale2x
SRCDIR		+= ./source/scale2x
endif

OPL_CFLAGS   = -Ofast -fomit-frame-pointer -fno-stack-protector -DNDEBUG
LDFLAGS     = -nodefaultlibs -lc -lgcc -lm -lSDL -Wl,--as-needed -Wl,--gc-sections -flto

ifeq ($(SOUND_OUTPUT), portaudio)
LDFLAGS		+= -lportaudio
endif
ifeq ($(SOUND_OUTPUT), libao)
LDFLAGS		+= -lao
endif
ifeq ($(SOUND_OUTPUT), alsa)
LDFLAGS		+= -lasound
endif
ifeq ($(SOUND_OUTPUT), pulse)
LDFLAGS		+= -lpulse -lpulse-simple
endif

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(CC) $(CFLAGS) -o $(PRGNAME) $^ $(LDFLAGS)

opl.o mame_fmopl.o mame_ymdeltat.o crab_ym2413.o: CFLAGS += $(OPL_CFLAGS)

$(OBJ_C) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PRGNAME) *.o
