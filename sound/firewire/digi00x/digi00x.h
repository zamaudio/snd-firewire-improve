/*
 * digi00x.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_DIGI00X_H_INCLUDED
#define SOUND_DIGI00X_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* TODO: remove when merging to upstream. */
#include "../../../backport.h"

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rawmidi.h>
//#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "../lib.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"

#define DG00X_MIX                0xffffe0000300ull
#define DG00X_MIX_ANALOG_1L      (0x00 | DG00X_MIX)
#define DG00X_MIX_ANALOG_1R      (0x04 | DG00X_MIX) 
#define DG00X_MIX_ANALOG_2L      (0x08 | DG00X_MIX)
#define DG00X_MIX_ANALOG_2R      (0x0c | DG00X_MIX)
#define DG00X_MIX_ANALOG_3L      (0x10 | DG00X_MIX)
#define DG00X_MIX_ANALOG_3R      (0x14 | DG00X_MIX)
#define DG00X_MIX_ANALOG_4L      (0x18 | DG00X_MIX)
#define DG00X_MIX_ANALOG_4R      (0x1c | DG00X_MIX)
#define DG00X_MIX_ANALOG_5L      (0x20 | DG00X_MIX)
#define DG00X_MIX_ANALOG_5R      (0x24 | DG00X_MIX)
#define DG00X_MIX_ANALOG_6L      (0x28 | DG00X_MIX)
#define DG00X_MIX_ANALOG_6R      (0x2c | DG00X_MIX)
#define DG00X_MIX_ANALOG_7L      (0x30 | DG00X_MIX)
#define DG00X_MIX_ANALOG_7R      (0x34 | DG00X_MIX)
#define DG00X_MIX_ANALOG_8L      (0x38 | DG00X_MIX)
#define DG00X_MIX_ANALOG_8R      (0x3c | DG00X_MIX)
#define DG00X_MIX_SPDIF_1L       (0x40 | DG00X_MIX)
#define DG00X_MIX_SPDIF_1R       (0x44 | DG00X_MIX)
#define DG00X_MIX_SPDIF_2L       (0x48 | DG00X_MIX)
#define DG00X_MIX_SPDIF_2R       (0x4c | DG00X_MIX)
#define DG00X_MIX_ADAT_1L        (0x50 | DG00X_MIX)
#define DG00X_MIX_ADAT_1R        (0x54 | DG00X_MIX)
#define DG00X_MIX_ADAT_2L        (0x58 | DG00X_MIX)
#define DG00X_MIX_ADAT_2R        (0x5c | DG00X_MIX)
#define DG00X_MIX_ADAT_3L        (0x60 | DG00X_MIX)
#define DG00X_MIX_ADAT_3R        (0x64 | DG00X_MIX)
#define DG00X_MIX_ADAT_4L        (0x68 | DG00X_MIX)
#define DG00X_MIX_ADAT_4R        (0x6c | DG00X_MIX)
#define DG00X_MIX_ADAT_5L        (0x70 | DG00X_MIX)
#define DG00X_MIX_ADAT_5R        (0x74 | DG00X_MIX)
#define DG00X_MIX_ADAT_6L        (0x78 | DG00X_MIX)
#define DG00X_MIX_ADAT_6R        (0x7c | DG00X_MIX)
#define DG00X_MIX_ADAT_7L        (0x80 | DG00X_MIX)
#define DG00X_MIX_ADAT_7R        (0x84 | DG00X_MIX)
#define DG00X_MIX_ADAT_8L        (0x88 | DG00X_MIX)
#define DG00X_MIX_ADAT_8R        (0x8c | DG00X_MIX)

#define DG00X_MIX_NONE           0x00000000
#define DG00X_MIX_1_TO_STEREO    0x18000000
#define DG00X_MIX_1_TO_1         0x20000000

/*
 * The double-oh-three algorithm was discovered by Robin Gareus and Damien
 * Zammit in 2012, with reverse-engineering for Digi 003 Rack.
 */
struct dot_state {
	__u8 carry;
	__u8 idx;
	unsigned int off;
};

struct snd_dg00x {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;
	unsigned int clock;

	struct mutex mutex;
	spinlock_t lock;

	struct amdtp_stream tx_stream;
	struct fw_iso_resources tx_resources;

	struct dot_state state;
	struct amdtp_stream rx_stream;
	struct fw_iso_resources rx_resources;

	unsigned int playback_substreams;
	unsigned int capture_substreams;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* For asynchronous messages. */
	u32 msg;

	/* For asynchronous MIDI controls. */
	struct work_struct midi_control;
	struct snd_rawmidi_substream *in_control;
	struct snd_rawmidi_substream *out_control;
};

#define DG00X_ADDR_BASE		0xffffe0000000ull

#define DG00X_OFFSET_STREAMING_STATE	0x0000
#define DG00X_OFFSET_STREAMING_SET	0x0004
#define DG00X_OFFSET_MIDI_CTL_ADDR	0x0008
/* For LSB of the address		0x000c */
/* unknown				0x0010 */
#define DG00X_OFFSET_MESSAGE_ADDR	0x0014
/* For LSB of the address		0x0018 */
/* unknown				0x001c */
/* unknown				0x0020 */
/* not used			0x0024--0x00ff */
#define DG00X_OFFSET_ISOC_CHANNELS	0x0100
/* unknown				0x0104 */
/* unknown				0x0118 */
/* unknown				0x010c */
#define DG00X_OFFSET_RATE_SET		0x0110
#define DG00X_OFFSET_RATE_GET		0x0114
#define DG00X_OFFSET_CLOCK_SOURCE	0x0118
#define DG00X_OFFSET_OPT_IFACE_MODE	0x011c
/* unknown				0x0120 */
/* unknown				0x0124 */
/* unknown				0x0128 */
/* unknown				0x012c */
/* unknown				0x0138 */

/* values for SND_DG00X_ADDR_OFFSET_RATE */
enum snd_dg00x_rate {
	SND_DG00X_RATE_44100 = 0,
	SND_DG00X_RATE_48000,
	SND_DG00X_RATE_88200,
	SND_DG00X_RATE_96000,
	SND_DG00X_RATE_COUNT,
};

/* values for SND_DG00X_ADDR_OFFSET_CLOCK */
enum snd_dg00x_clock {
	SND_DG00X_CLOCK_INTERNAL = 0,
	SND_DG00X_CLOCK_SPDIF,
	SND_DG00X_CLOCK_ADAT,
	SND_DG00X_CLOCK_WORD,
	SND_DG00X_CLOCK_COUNT,
};

/* values for SND_DG00X_ADDR_OFFSET_OPTICAL_MODE */
enum snd_dg00x_optical_mode {
	SND_DG00X_OPT_IFACE_MODE_ADAT = 0,
	SND_DG00X_OPT_IFACE_MODE_SPDIF,
	SND_DG00X_OPT_IFACE_MODE_COUNT,
};

/* Initialize dot status. */
static inline void snd_dg00x_protocol_init_state(struct dot_state *state)
{
	state->carry = 0x00;
	state->idx = 0x00;
	state->off = 0;
}

void snd_dg00x_protocol_set_pcm_function(struct amdtp_stream *s,
					 snd_pcm_format_t format);
void snd_dg00x_protocol_set_midi_function(struct snd_dg00x *dg00x);
void snd_dg00x_protocol_queue_midi_message(struct snd_dg00x *dg00x);
int snd_dg00x_protocol_add_instance(struct snd_dg00x *dg00x);
void snd_dg00x_protocol_remove_instance(struct snd_dg00x *dg00x);
int snd_dg00x_protocol_register(void);
void snd_dg00x_protocol_unregister(void);

extern const unsigned int snd_dg00x_stream_rates[SND_DG00X_RATE_COUNT];
extern const unsigned int snd_dg00x_stream_clocks[SND_DG00X_CLOCK_COUNT];
extern const unsigned int
snd_dg00x_stream_mbla_data_channels[SND_DG00X_RATE_COUNT];
int snd_dg00x_stream_get_rate(struct snd_dg00x *dg00x, unsigned int *rate);
int snd_dg00x_stream_set_rate(struct snd_dg00x *dg00x, unsigned int rate);
int snd_dg00x_stream_get_clock(struct snd_dg00x *dg00x, unsigned int *clock);
int snd_dg00x_stream_set_clock(struct snd_dg00x *dg00x, unsigned int clock);
int snd_dg00x_stream_get_optical_mode(struct snd_dg00x *dg00x,
                                      enum snd_dg00x_optical_mode *mode);
int snd_dg00x_stream_set_optical_mode(struct snd_dg00x *dg00x,
				      unsigned int mode);
int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x);
int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x, unsigned int rate);
void snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x);

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x);
int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x);
void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x);

void snd_dg00x_proc_init(struct snd_dg00x *dg00x);

int snd_dg00x_create_pcm_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_mixer(struct snd_dg00x *dg00x);

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_hwdep_device(struct snd_dg00x *dg00x);
#endif
