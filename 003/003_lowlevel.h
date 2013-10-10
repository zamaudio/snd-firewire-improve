/*
 * 003_lowlevel.h  Digidesign 003Rack driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (C) 2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Damien Zammit <damien@zamaudio.com>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "003.h"

#define R003_HARDWARE_ADDR      0xffff00000000ULL

#define VENDOR_DIGIDESIGN       0x00a07e
#define VENDOR_DIGIDESIGN_NAME  " "
#define R003_MODEL_ID           0x00ab0000
//#define R003_MODEL_ID           0x00000002
#define R003_MODEL_NAME         " 003Rack "

#define R003_STREAMS_W_REG      0xe0000004
#define R003_STREAMS_R_REG      0xe0000000
#define R003_STREAMS_OFF        0x00000000
#define R003_STREAMS_ON         0x00000001
#define R003_STREAMS_INIT       0x00000002
#define R003_STREAMS_SHUTDOWN   0x00000003

#define R003_SAMPLERATE_REG     0xe0000110
#define R003_SAMPLERATE_44100   0x00000000
#define R003_SAMPLERATE_48000   0x00000001
#define R003_SAMPLERATE_88200   0x00000002
#define R003_SAMPLERATE_96000   0x00000003

#define R003_CLOCKSOURCE_REG    0xe0000118
#define R003_CLOCK_INTERNAL     0x00000000
#define R003_CLOCK_SPDIF        0x00000001
#define R003_CLOCK_ADAT         0x00000002
#define R003_CLOCK_WORDCLOCK    0x00000003

#define R003_MIX                (0xe0000300 | R003_HARDWARE_ADDR)
#define R003_MIX_ANALOG_1L      (0x00 | R003_MIX)
#define R003_MIX_ANALOG_1R      (0x04 | R003_MIX)
#define R003_MIX_ANALOG_2L      (0x08 | R003_MIX)
#define R003_MIX_ANALOG_2R      (0x0c | R003_MIX)
#define R003_MIX_ANALOG_3L      (0x10 | R003_MIX)
#define R003_MIX_ANALOG_3R      (0x14 | R003_MIX)
#define R003_MIX_ANALOG_4L      (0x18 | R003_MIX)
#define R003_MIX_ANALOG_4R      (0x1c | R003_MIX)
#define R003_MIX_ANALOG_5L      (0x20 | R003_MIX)
#define R003_MIX_ANALOG_5R      (0x24 | R003_MIX)
#define R003_MIX_ANALOG_6L      (0x28 | R003_MIX)
#define R003_MIX_ANALOG_6R      (0x2c | R003_MIX)
#define R003_MIX_ANALOG_7L      (0x30 | R003_MIX)
#define R003_MIX_ANALOG_7R      (0x34 | R003_MIX)
#define R003_MIX_ANALOG_8L      (0x38 | R003_MIX)
#define R003_MIX_ANALOG_8R      (0x3c | R003_MIX)
#define R003_MIX_SPDIF_1L       (0x40 | R003_MIX)
#define R003_MIX_SPDIF_1R       (0x44 | R003_MIX)
#define R003_MIX_SPDIF_2L       (0x48 | R003_MIX)
#define R003_MIX_SPDIF_2R       (0x4c | R003_MIX)
#define R003_MIX_ADAT_1L        (0x50 | R003_MIX)
#define R003_MIX_ADAT_1R        (0x54 | R003_MIX)
#define R003_MIX_ADAT_2L        (0x58 | R003_MIX)
#define R003_MIX_ADAT_2R        (0x5c | R003_MIX)
#define R003_MIX_ADAT_3L        (0x60 | R003_MIX)
#define R003_MIX_ADAT_3R        (0x64 | R003_MIX)
#define R003_MIX_ADAT_4L        (0x68 | R003_MIX)
#define R003_MIX_ADAT_4R        (0x6c | R003_MIX)
#define R003_MIX_ADAT_5L        (0x70 | R003_MIX)
#define R003_MIX_ADAT_5R        (0x74 | R003_MIX)
#define R003_MIX_ADAT_6L        (0x78 | R003_MIX)
#define R003_MIX_ADAT_6R        (0x7c | R003_MIX)
#define R003_MIX_ADAT_7L        (0x80 | R003_MIX)
#define R003_MIX_ADAT_7R        (0x84 | R003_MIX)
#define R003_MIX_ADAT_8L        (0x88 | R003_MIX)
#define R003_MIX_ADAT_8R        (0x8c | R003_MIX)

#define R003_MIX_NONE           0x00000000
#define R003_MIX_1_TO_STEREO    0x18000000
#define R003_MIX_1_TO_1         0x20000000

#define BYTESWAP32_CONST(x) ((((x) & 0x000000FF) << 24) |   \
                             (((x) & 0x0000FF00) << 8) |    \
                             (((x) & 0x00FF0000) >> 8) |    \
                             (((x) & 0xFF000000) >> 24))

void write_quadlet(struct snd_efw *digi, unsigned long long int reg, unsigned int data);

unsigned int read_quadlet(struct snd_efw *digi, unsigned long long int reg);

//static inline int poll_until(struct snd_efw *digi, unsigned long long int reg, unsigned int expect);

//static void rack_init_write_814_block(struct snd_efw *digi);

int rack_init(struct snd_efw *digi);

void rack_shutdown(struct snd_efw *digi);

void digi_free_resources(struct snd_efw *digi, struct amdtp_stream *stream);

int digi_allocate_resources(struct snd_efw *digi, enum amdtp_stream_direction direction);
