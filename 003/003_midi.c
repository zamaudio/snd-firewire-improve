/*
 * 003_midi.c - driver for Digidesign 003 midi
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013 Takashi Sakamoto
 * Copyright (c) 2013 Damien Zammit
 *
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */
#include "003.h"

/*
 * According to MMA/AMEI-027, MIDI stream is multiplexed with PCM stream in
 * AMDTP packet. The data rate of MIDI message is much less than PCM so there
 * is a little problem to suspend MIDI streams.
 */

static int
midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	struct amdtp_stream *stream;
	int err;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		stream = &efw->rx_stream;
	else
		stream = &efw->tx_stream;

	/* register pointer */
	amdtp_stream_midi_add(stream, substream);

	/* confirm to start stream in current sampling rate */
	if (!amdtp_stream_running(stream)) {
		err = snd_efw_stream_start_duplex(efw, stream, 48000);
		if (err < 0)
			goto end;
	}

	err = 0;

end:
	return err;
}

static int
midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	struct amdtp_stream *stream, *opposite;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT) {
		stream = &efw->rx_stream;
		opposite = &efw->tx_stream;
	} else {
		stream = &efw->tx_stream;
		opposite = &efw->rx_stream;
	}

	/* unregister pointer */
	amdtp_stream_midi_remove(stream, substream);

	if (!amdtp_stream_midi_running(stream) &&
	    !amdtp_stream_midi_running(opposite) &&
	    !amdtp_stream_pcm_running(stream) &&
	    !amdtp_stream_pcm_running(opposite))
		snd_efw_stream_stop_duplex(efw);

	return 0;
}

static void
midi_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	unsigned long *midi_triggered;
	unsigned long flags;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		midi_triggered = &efw->rx_stream.midi_triggered;
	else
		midi_triggered = &efw->tx_stream.midi_triggered;

	spin_lock_irqsave(&efw->lock, flags);

	/* bit table shows MIDI stream has data or not */
	if (up)
		__set_bit(substream->number, midi_triggered);
	else
		__clear_bit(substream->number, midi_triggered);

	spin_unlock_irqrestore(&efw->lock, flags);

	return;
}

static struct snd_rawmidi_ops midi_output_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_trigger,
};

static struct snd_rawmidi_ops midi_input_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_trigger,
};

static void
set_midi_substream_names(struct snd_efw *efw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	if (str->substream_count > 2)
		return;

	list_for_each_entry(subs, &str->substreams, list)
		snprintf(subs->name, sizeof(subs->name),
			"%s MIDI %d", efw->card->shortname, subs->number + 1);
}

int snd_efw_create_midi_devices(struct snd_efw *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	struct snd_rawmidi_substream *subs;
	int err;

	/* check the number of midi stream */
	if ((efw->midi_input_ports > AMDTP_MAX_CHANNELS_FOR_PCM) |
	    (efw->midi_output_ports > AMDTP_MAX_CHANNELS_FOR_PCM))
		return -EINVAL;

	/* create midi ports */
	err = snd_rawmidi_new(efw->card, efw->card->driver, 0,
			      efw->midi_output_ports, efw->midi_input_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
			"%s MIDI", efw->card->shortname);
	rmidi->private_data = efw;

	if (efw->midi_input_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&midi_input_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(efw, str);
		//amdtp_stream_set_midi(&efw->receive_stream,
		//		      efw->midi_input_ports);
	}

	if (efw->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&midi_output_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		list_for_each_entry(subs, &str->substreams, list)
			efw->tx_stream.midi[subs->number] = subs;

		set_midi_substream_names(efw, str);
		//amdtp_stream_set_midi(&efw->transmit_stream,
		//		      efw->midi_output_ports);
	}

	if ((efw->midi_output_ports > 0) && (efw->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
