/*
 * 003_pcm.c - pcm driver for Digidesign 003Rack
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
#include "003_lowlevel.h"
#include <linux/spinlock.h>
#include <linux/wait.h>

static DEFINE_MUTEX(devices_mutex);

static int digi_hw_lock(struct snd_efw *digi)
{
        int err;

        spin_lock_irq(&digi->lockhw);

        if (digi->dev_lock_count == 0) {
                digi->dev_lock_count = -1;
                err = 0;
        } else {
                err = -EBUSY;
        }

        spin_unlock_irq(&digi->lockhw);

        return err;
}

static int digi_hw_unlock(struct snd_efw *digi)
{
        int err;

        spin_lock_irq(&digi->lockhw);
        
        if (digi->dev_lock_count == -1) {
                digi->dev_lock_count = 0;
                err = 0;
        } else {
                err = -EBADFD;
        }

        spin_unlock_irq(&digi->lockhw);

        return err;
}       

static void digi_lock_changed(struct snd_efw *digi)
{
        digi->dev_lock_changed = true;
        wake_up(&digi->hwdep_wait);
}

static int digi_try_lock(struct snd_efw *digi)
{
        int err;

        spin_lock_irq(&digi->lockhw);

        if (digi->dev_lock_count < 0) {
                err = -EBUSY;
                goto out;
        }

        if (digi->dev_lock_count++ == 0)
                digi_lock_changed(digi);
        err = 0;

out:
        spin_unlock_irq(&digi->lockhw);

        return err;
}

static void digi_unlock(struct snd_efw *digi)
{
        spin_lock_irq(&digi->lockhw);

        if (WARN_ON(digi->dev_lock_count <= 0))
                goto out;

        if (--digi->dev_lock_count == 0)
                digi_lock_changed(digi);

out:
        spin_unlock_irq(&digi->lockhw);
}

static int
pcm_init_hw_params(struct snd_efw *efw,
		   struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_SYNC_START |
			SNDRV_PCM_INFO_FIFO_IN_FRAMES |
			/* for Open Sound System compatibility */
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = SNDRV_PCM_FMTBIT_S32,
		.channels_min = 19,
		.channels_max = 19,
		.buffer_bytes_max = 1024 * 1024 * 1024,
		.period_bytes_min = 256,
		.period_bytes_max = 1024 * 1024 * 1024 / 2,
		.periods_min = 2,
		.periods_max = 32,
		.fifo_size = 0,
	};
	
	int err, i;

	substream->runtime->hw = hardware;
	substream->runtime->delay = substream->runtime->hw.fifo_size;

        //substream->runtime->hw.rates = 0;
        substream->runtime->hw.rates |= snd_pcm_rate_to_rate_bit(48000);
        snd_pcm_limit_hw_rates(substream->runtime);

	substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
	substream->runtime->hw.channels_min = 19;
	substream->runtime->hw.channels_max = 19;

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		goto end;

/*
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (err < 0)
		goto end;
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (err < 0)
		goto end;
*/

	/* time for period constraint */
	err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					500, UINT_MAX);
	if (err < 0)
		goto end;

	err = 0;
	printk("END HW_PARAMS");
end:
	return err;
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	int err;

	printk("START PCM_OPEN\n");
	/* common hardware information */
	err = pcm_init_hw_params(efw, substream);
	if (err < 0)
		goto end;

	printk("DONE HW_PARAMS\n");
	
	substream->runtime->hw.channels_min = 19;
	substream->runtime->hw.channels_max = 19;
	substream->runtime->hw.rate_min = 48000;
	substream->runtime->hw.rate_max = 48000;

	printk("PRE RACK_INIT\n");
	rack_init(efw);
	printk("DONE RACK_INIT\n");

	snd_pcm_set_sync(substream);
	printk("DONE SET_SYNC\n");
	
	return 0;

end:
	printk("ERROR PCM_OPEN: HW_PARAMS\n");
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
				params_buffer_bytes(hw_params));
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;

	snd_efw_stream_stop_duplex(efw);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_efw_stream_start_duplex(efw, &efw->tx_stream, runtime->rate);
	if (err < 0)
		goto end;

	amdtp_stream_set_pcm_format(&efw->tx_stream, runtime->format);
	amdtp_stream_pcm_prepare(&efw->tx_stream);
end:
	return err;
}
static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_efw_stream_start_duplex(efw, &efw->rx_stream, runtime->rate);
	if (err < 0)
		goto end;

	amdtp_stream_set_pcm_format(&efw->rx_stream, runtime->format);
	amdtp_stream_pcm_prepare(&efw->rx_stream);
end:
	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_efw *efw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&efw->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&efw->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_efw *efw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&efw->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&efw->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_efw *efw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&efw->tx_stream);
}
static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_efw *efw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&efw->rx_stream);
}

static struct snd_pcm_ops pcm_capture_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_capture_prepare,
	.trigger	= pcm_capture_trigger,
	.pointer	= pcm_capture_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
};

static struct snd_pcm_ops pcm_playback_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_playback_prepare,
	.trigger	= pcm_playback_trigger,
	.pointer	= pcm_playback_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
	.mmap		= snd_pcm_lib_mmap_vmalloc,
};

int snd_efw_create_pcm_devices(struct snd_efw *efw)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(efw->card, efw->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = efw;
	snprintf(pcm->name, sizeof(pcm->name), "%s PCM", efw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

end:
	return err;
}

