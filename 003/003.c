/*
 * 003.c - driver for 003Rack by Digidesign
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013 Damien Zammit
 * Copyright (c) 2013 Takashi Sakamoto
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

MODULE_DESCRIPTION("Digidesign 003 Driver");
MODULE_AUTHOR("Damien Zammit <damien@zamaudio.com>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]   = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]    = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS]  = SNDRV_DEFAULT_ENABLE_PNP;

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

#define MAX_TRIES_AFTER_BUS_RESET 5

static int
get_hardware_info(struct snd_efw *efw)
{
	int err;

	struct snd_efw_hwinfo *hwinfo;
	//char version[12];

	hwinfo = kzalloc(sizeof(struct snd_efw_hwinfo), GFP_KERNEL);
	if (hwinfo == NULL)
		return -ENOMEM;


	/* capabilities */
		efw->dynaddr_support = 0;
		efw->mirroring_support = 0;
		efw->aes_ebu_xlr_support = 0;
		efw->has_dsp_mixer = 0;
		efw->has_fpga = 0;
		efw->has_phantom = 0;

	efw->pcm_capture_channels[0] = 18;
	efw->pcm_capture_channels[1] = 18;
	efw->pcm_capture_channels[2] = 18;
	efw->pcm_playback_channels[0] = 18;
	efw->pcm_playback_channels[1] = 18;
	efw->pcm_playback_channels[2] = 18;

	/* set names */
	strcpy(efw->card->driver, "003 Rack");
	strcpy(efw->card->shortname, "003R");
	snprintf(efw->card->longname, sizeof(efw->card->longname),
		"%s %s at %s, S%d",
		"Digidesign", "003 Rack", 
		dev_name(&efw->unit->device), 100 << efw->device->max_speed);
	strcpy(efw->card->mixername, "003 Rack");

	/* set flag for supported clock source */
	efw->supported_clock_source = 0;
	
	efw->supported_sampling_rate = SNDRV_PCM_RATE_48000;
	
	err = 0;

	kfree(hwinfo);
	return err;
}

static bool match_fireworks_device_name(struct fw_unit *unit)
{
	return true;
}

static void
snd_efw_card_free(struct snd_card *card)
{
	struct snd_efw *efw = card->private_data;

	if (efw->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~(1 << efw->card_index);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&efw->mutex);

	return;
}

static int snd_efw_probe(struct fw_unit *unit,
			 const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_efw *efw;
	int card_index, err;

	mutex_lock(&devices_mutex);

	printk("PROBE STARTED\n");
	if (!match_fireworks_device_name(unit))
		return -ENODEV;

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (!(devices_used & (1 << card_index)) && enable[card_index])
			break;
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	/* create card */
	err = snd_card_create(index[card_index], id[card_index],
				THIS_MODULE, sizeof(struct snd_efw), &card);
	if (err < 0)
		goto end;
	card->private_free = snd_efw_card_free;

	/* initialize myself */
	efw = card->private_data;
	efw->card = card;
	efw->device = fw_parent_device(unit);
	efw->unit = unit;
	efw->card_index = -1;
	mutex_init(&efw->mutex);
	spin_lock_init(&efw->lock);

	/* get hardware information */
	err = get_hardware_info(efw);
	if (err < 0)
		goto error;

	/* get the number of hardware meters */
	//err = get_hardware_meters_count(efw);
	//if (err < 0)
	//	goto error;

	/* create proc interface */
	
	/* create hardware control */

	/* create PCM interface */
	err = snd_efw_create_pcm_devices(efw);
	if (err < 0)
		goto error;

	/* create midi interface */
	err = snd_efw_create_midi_devices(efw);
	if (err < 0)
		goto error;

	err = snd_efw_stream_init_duplex(efw);
	if (err < 0)
		goto error;

	/* register card and device */
	snd_card_set_dev(card, &unit->device);
	err = snd_card_register(card);
	if (err < 0)
		goto error;
	dev_set_drvdata(&unit->device, efw);
	devices_used |= BIT(card_index);
	efw->card_index = card_index;

	/* proved */
	err = 0;
	goto end;

error:
	printk("PROBE FAILURE\n");
	snd_card_free(card);

end:
	mutex_unlock(&devices_mutex);
	return err;
}

static void snd_efw_update(struct fw_unit *unit)
{
	struct snd_efw *efw = dev_get_drvdata(&unit->device);
	int tries, err;

	//snd_efw_command_bus_reset(efw->unit);

	/*
	 * NOTE:
	 * There is a reason the application get error by bus reset during
	 * playing/recording.
	 *
	 * Fireworks doesn't sometimes respond FCP command after bus reset.
	 * Then the normal process to start streaming is failed. Here EFC
	 * identify command is used to check this. When all of trials are
	 * failed, the PCM stream is stopped, then the application fails to
	 * play/record and the users see 'input/output'
	 * error.
	 *
	 * Referring to OHCI1394, the connection should be redo within 1 sec
	 * after bus reset. Inner snd-firewire-lib, FCP commands are retried
	 * three times if failed. If identify commands are executed 5 times,
	 * totally, FCP commands are sent 15 times till completely failed. But
	 * the total time is not assume-able because it's asynchronous
	 * transactions. Here we wait 500msec between each commands. I hope
	 * total time within 1 sec.
	 */
	tries = 0;
	do {
		//err = snd_efw_command_identify(efw);
		err = 0;
		if (err == 0)
			break;
		msleep(100);
	} while (tries++ < MAX_TRIES_AFTER_BUS_RESET);

	if (err < 0) {
		snd_efw_stream_destroy_duplex(efw);
		goto end;
	}

	/*
	 * NOTE:
	 * There is another reason that the application get error by bus reset
	 * during playing/recording.
	 *
	 * As a result of Juju's rediscovering nodes at bus reset, there is a
	 * case of changing node id reflecting identified-tree. Then sometimes
	 * logical devices are removed and re-probed. When
	 * connecting/disconnecting sound cards or disconnecting, this behavior
	 * brings an issue.
	 *
	 * When connecting/disconnecting sound cards or in Firewire bus, if
	 * remove/probe is generated for the current sound cards, the ids for
	 * current sound cards are sometimes changed and character devices are
	 * also changed. Then user-land application fails to play/record and the
	 * users see 'No such device' error.
	 *
	 * Even if all is OK, the sound is not smooth, not fluent. At least,
	 * short noises, at largest, blank sound for 1-3 seconds.
	 */
	snd_efw_stream_update_duplex(efw);
end:
	return;
}

static void snd_efw_remove(struct fw_unit *unit)
{
	struct snd_efw *efw= dev_get_drvdata(&unit->device);

	snd_efw_stream_destroy_duplex(efw);

	snd_card_disconnect(efw->card);
	snd_card_free_when_closed(efw->card);

	return;
}

#define VENDOR_DIGIDESIGN		0x00a07e
#define MODEL_ID_003RACK		0x00ab00

static const struct ieee1394_device_id snd_efw_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID, 
		.vendor_id = VENDOR_DIGIDESIGN,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_efw_id_table);

static struct fw_driver snd_efw_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-003",
		.bus = &fw_bus_type,
	},
	.probe    = snd_efw_probe,
	.update   = snd_efw_update,
	.remove   = snd_efw_remove,
	.id_table = snd_efw_id_table,
};

static int __init snd_efw_init(void)
{
	int err;

	//err = snd_efw_command_register();
	err = 0;
	if (err < 0)
		goto end;

	err = driver_register(&snd_efw_driver.driver);
//	if (err < 0)
//		snd_efw_command_unregister();

end:
	return err;
}

static void __exit snd_efw_exit(void)
{
	driver_unregister(&snd_efw_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_efw_init);
module_exit(snd_efw_exit);
