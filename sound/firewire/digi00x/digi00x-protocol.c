/*
 * digi00x-protocol.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 * Copyright (C) 2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Damien Zammit <damien@zamaudio.com>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

/*
 * The double-oh-three algorism is invented by Robin Gareus and Damien Zammit
 * in 2012, with reverse-engineering for Digi 003 Rack.
 */

struct dot_state {
	__u8 carry;
	__u8 idx;
	unsigned int off;
};

#define BYTE_PER_SAMPLE (4)
#define MAGIC_DOT_BYTE (2)

#define MAGIC_BYTE_OFF(x) (((x) * BYTE_PER_SAMPLE) + MAGIC_DOT_BYTE)

/*
 * double-oh-three look up table
 *
 * @param idx index byte (audio-sample data) 0x00..0xff
 * @param off channel offset shift
 * @return salt to XOR with given data
 */
static const __u8 dot_scrt(const __u8 idx, const unsigned int off)
{
	/*
	 * the length of the added pattern only depends on the lower nibble
	 * of the last non-zero data
	 */
	static const __u8 len[16] = {0, 1, 3, 5, 7, 9, 11, 13, 14,
				     12, 10, 8, 6, 4, 2, 0};

	/*
	 * the lower nibble of the salt. Interleaved sequence.
	 * this is walked backwards according to len[]
	 */
	static const __u8 nib[15] = {0x8, 0x7, 0x9, 0x6, 0xa, 0x5, 0xb, 0x4,
				     0xc, 0x3, 0xd, 0x2, 0xe, 0x1, 0xf};

	/* circular list for the salt's hi nibble. */
	static const __u8 hir[15] = {0x0, 0x6, 0xf, 0x8, 0x7, 0x5, 0x3, 0x4,
				     0xc, 0xd, 0xe, 0x1, 0x2, 0xb, 0xa};

	/*
	 * start offset for upper nibble mapping.
	 * note: 9 is /special/. In the case where the high nibble == 0x9,
	 * hir[] is not used and - coincidentally - the salt's hi nibble is
	 * 0x09 regardless of the offset.
	 */
	static const __u8 hio[16] = {0, 11, 12, 6, 7, 5, 1, 4,
				     3, 0x00, 14, 13, 8, 9, 10, 2};

	const __u8 ln = idx & 0xf;
	const __u8 hn = (idx >> 4) & 0xf;
	const __u8 hr = (hn == 0x9) ? 0x9 : hir[(hio[hn] + off) % 15];

	if (len[ln] < off)
		return 0x00;

	return ((nib[14 + off - len[ln]]) | (hr << 4));
}

static inline void dot_state_reset(struct dot_state *state)
{
	state->carry = 0x00;
	state->idx   = 0x00;
	state->off   = 0;
}

static void dot_encode_step(struct dot_state *state, __be32 *const buffer)
{
	__u8 * const data = (__u8 *) buffer;

	if (data[MAGIC_DOT_BYTE] != 0x00) {
		state->off = 0;
		state->idx = data[MAGIC_DOT_BYTE] ^ state->carry;
	}
	data[MAGIC_DOT_BYTE] ^= state->carry;
	state->carry = dot_scrt(state->idx, ++(state->off));
}

void snd_dg00x_protocol_write_s32(struct amdtp_stream *s,
				  struct snd_pcm_substream *pcm,
				  __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u32 *src;
	static struct dot_state state;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		dot_state_reset(&state);

		for (c = 0; c < channels; ++c) {
			buffer[s->pcm_positions[c]] =
					cpu_to_be32((*src >> 8) | 0x40000000);
			dot_encode_step(&state, &buffer[s->pcm_positions[c]]);
			src++;
		}

		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

void snd_dg00x_protocol_fill_midi(struct amdtp_stream *s,
				  __be32 *buffer, unsigned int frames)
{
	unsigned int f, port;
	u8 *b;

	for (f = 0; f < frames; f++) {
		port = (s->data_block_counter + f) % 4;
		b = (u8 *)&buffer[s->midi_position];

		/*
		 * The device allows to transfer MIDI messages by maximum two
		 * bytes per data channel. But this module transfers one byte
		 * one time because MIDI data rate is quite lower than IEEE
		 * 1394 bus data rate.
		 */
		if (amdtp_midi_ratelimit_per_packet(s, port) &&
		    s->midi[port] != NULL &&
		    snd_rawmidi_transmit(s->midi[port], &b[1], 1) == 1) {
			amdtp_midi_rate_use_one_byte(s, port);
			b[3] = 0x01 | (0x10 << port);
		} else {
			b[1] = 0;
			b[3] = 0;
		}
		b[0] = 0x80;
		b[2] = 0;

		buffer += s->data_block_quadlets;
	}
}

void snd_dg00x_protocol_pull_midi(struct amdtp_stream *s,
				  __be32 *buffer, unsigned int frames)
{
	unsigned int f;
	u8 *b;

	for (f = 0; f < frames; f++) {
		b = (u8 *)&buffer[s->midi_position];

		if (s->midi[0] && (b[3] > 0))
			snd_rawmidi_receive(s->midi[0], b + 1, b[3]);

		buffer += s->data_block_quadlets;
	}
}

struct workqueue_struct *midi_wq;

static void send_midi_control(struct work_struct *work)
{
	struct snd_dg00x *dg00x =
			container_of(work, struct snd_dg00x, midi_control);
	struct fw_device *device = fw_parent_device(dg00x->unit);

	unsigned int len;
	__be32 buf = 0;
	u8 *b = (u8 *)&buf;

	/* Send MIDI control. */
	if (!dg00x->out_control)
		return;

	do {
		len = snd_rawmidi_transmit(dg00x->out_control, b + 1, 2);
		if (len > 0) {
			b[0] = 0x80;
			b[3] = 0xc0 | len;

			/* Don't check transaction status. */
			fw_run_transaction(device->card,
					   TCODE_WRITE_QUADLET_REQUEST,
					   device->node_id, device->generation,
					   device->max_speed,
					   0xffffe0000400, &buf, sizeof(buf));
		}
	} while (len > 0);
}

void snd_dg00x_protocol_queue_midi_message(struct snd_dg00x *dg00x)
{
	queue_work(midi_wq, &dg00x->midi_control);
}

static struct snd_dg00x *instances[SNDRV_CARDS];
static DEFINE_SPINLOCK(instances_lock);

static void handle_unknown_message(struct snd_dg00x *dg00x,
				   unsigned long long offset, u32 *buf)
{
	snd_printk(KERN_INFO"%08llx: %08x\n", offset, be32_to_cpu(*buf));
}

static void handle_midi_control(struct snd_dg00x *dg00x, u32 *buf,
				unsigned int length)
{
	unsigned int i;
	unsigned int len;
	u8 *b;

	if (dg00x->in_control == NULL)
		return;

	length /= 4;

	for (i = 0; i < length; i++) {
		b = (u8 *)&buf[i];
		len = b[3] & 0xf;
		if (len > 0)
			snd_rawmidi_receive(dg00x->in_control, b + 1, len);
	}
}

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	u32 *buf = (__be32 *)data;
	struct fw_device *device;
	struct snd_dg00x *dg00x;
	unsigned int i;

	spin_lock_irq(&instances_lock);
	for (i = 0; i < SNDRV_CARDS; i++) {
		dg00x = instances[i];
		if (dg00x == NULL)
			continue;
		device = fw_parent_device(dg00x->unit);
		if (device->card != card)
			continue;
		smp_rmb();	/* node id vs. generation */
		if (device->node_id != source)
			continue;
		break;
	}

	if (offset == 0xffffe0000000)
		handle_unknown_message(dg00x, offset, buf);
	else if (offset == 0xffffe0000004)
		handle_midi_control(dg00x, buf, length);

	spin_unlock_irq(&instances_lock);
	fw_send_response(card, request, RCODE_COMPLETE);
}

/*
 * Use the same range of address for asynchronous messages from any devices, to
 * save resources on host controller.
 */
static struct fw_address_handler async_handler;

int snd_dg00x_protocol_add_instance(struct snd_dg00x *dg00x)
{
	struct fw_device *device = fw_parent_device(dg00x->unit);
	__be32 data[2];
	unsigned int i;
	int err;

	/* Unknown. 4bytes. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (async_handler.offset >> 32));
	data[1] = cpu_to_be32(async_handler.offset);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				 0xffffe0000014ull, &data, sizeof(data), 0);
	if (err < 0)
		return err;

	/* Asynchronous transactions for MIDI control message. 8 bytes. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (async_handler.offset >> 32));
	data[1] = cpu_to_be32(async_handler.offset + 4);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				 0xffffe0000008ull, &data, sizeof(data), 0);
	if (err < 0)
		return err;

	spin_lock_irq(&instances_lock);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (instances[i] != NULL)
			continue;
		instances[i] = dg00x;
		break;
	}
	spin_unlock_irq(&instances_lock);

	INIT_WORK(&dg00x->midi_control, send_midi_control);

	return 0;
}

void snd_dg00x_protocol_remove_instance(struct snd_dg00x *dg00x)
{
	unsigned int i;

	spin_lock_irq(&instances_lock);
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (instances[i] != dg00x)
			continue;
		instances[i] = NULL;
		break;
	}
	spin_unlock_irq(&instances_lock);
}

int snd_dg00x_protocol_register(void)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	midi_wq = alloc_workqueue("snd-digi00x",
				  WQ_SYSFS | WQ_POWER_EFFICIENT, 0);
	if (midi_wq == NULL)
		return -ENOMEM;

	async_handler.length = 12;
	async_handler.address_callback = handle_message;
	async_handler.callback_data = NULL;

	err = fw_core_add_address_handler(&async_handler,
					  &resp_register_region);
	if (err < 0) {
		destroy_workqueue(midi_wq);
		return err;
	}

	return 0;
}

void snd_dg00x_protocol_unregister(void)
{
	destroy_workqueue(midi_wq);
	fw_core_remove_address_handler(&async_handler);
}
