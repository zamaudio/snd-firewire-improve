/*
 * digi00x_control.c - a part of driver for Digidesign 002/003 devices
 *
 * Copyright (c) Damien Zammit <damien@zamaudio.com>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

enum control_action { CTL_READ, CTL_WRITE };

static int snd_dg00x_read_quadlet(struct snd_dg00x *dg00x,
				  unsigned long long reg, u32 *dat)
{
	__be32 data;
	int err;
		
	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 reg, &data, sizeof(data), 0);
	if (err < 0)
		return err;

	*dat = be32_to_cpu(data);
	return err;
}

static int snd_dg00x_write_quadlet(struct snd_dg00x *dg00x,
				   unsigned long long reg, u32 dat)
{
	__be32 data;

	data = cpu_to_be32(dat);
	return snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
				  reg, &data, sizeof(data), 0);
}

static int dg00x_clock_command(struct snd_dg00x *dg00x, int *value,
			       enum control_action action)
{
	int err;
	if (action == CTL_READ) {
		err = snd_dg00x_stream_get_clock(dg00x, value);
	} else {
		err = snd_dg00x_stream_set_clock(dg00x, *value);
	}
	return err;
}

static int dg00x_clock_get(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_dg00x *dg00x;
	dg00x = control->private_data;
	value->value.enumerated.item[0] = dg00x->clock;
	return 0;
}

static int dg00x_clock_put(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_dg00x *dg00x;
	int err;
	int cur_val, new_val;
	dg00x = control->private_data;

	cur_val = dg00x->clock;
	new_val = value->value.enumerated.item[0];

	err = dg00x_clock_command(dg00x, &new_val, CTL_WRITE);
	if (err < 0)
		goto err;
	dg00x->clock = new_val;

err:
	return err < 0 ? err : 1;
}

static int dg00x_mixmatrix_get(struct snd_kcontrol *control,
			       struct snd_ctl_elem_value *value,
			       unsigned long long rawch)
{
	int err;
	u32 left, right;
	int v;
	struct snd_dg00x *dg00x;
	
	dg00x = control->private_data;

	err = snd_dg00x_read_quadlet(dg00x, rawch, &left);
	if (err < 0)
		goto err;
	err = snd_dg00x_read_quadlet(dg00x, rawch+0x04, &right);
	if (err < 0)
		goto err;
	
	if (left == DG00X_MIX_NONE && right == DG00X_MIX_NONE) {
		v = 0;
	} else if (left == DG00X_MIX_1_TO_1 && right == DG00X_MIX_NONE) {
		v = 1;
	} else if (left == DG00X_MIX_NONE && right == DG00X_MIX_1_TO_1) {
		v = 2;
	} else if (left == DG00X_MIX_1_TO_STEREO && 
		   right == DG00X_MIX_1_TO_STEREO) {
		v = 3;
	} else {
		v = 0;
	}
	value->value.enumerated.item[0] = v;
err:
	return err < 0 ? err : 1;
}

static int dg00x_mixmatrix_put(struct snd_kcontrol *control,
				  struct snd_ctl_elem_value *value,
				  unsigned long long rawch)
{
	int err, new_val;
	u32 left, right;
	struct snd_dg00x *dg00x;

	dg00x = control->private_data;
	new_val = value->value.enumerated.item[0];

	switch (new_val) {
	case 0:
		left = DG00X_MIX_NONE;
		right = DG00X_MIX_NONE;
		break;
	case 1:
		left = DG00X_MIX_1_TO_1;
		right = DG00X_MIX_NONE;
		break;
	case 2:
		left = DG00X_MIX_NONE;
	 	right = DG00X_MIX_1_TO_1;
		break;
	default:
	case 3:
		left = DG00X_MIX_1_TO_STEREO;
		right = DG00X_MIX_1_TO_STEREO;
		break;
	}
	err = snd_dg00x_write_quadlet(dg00x, 0xffffe0000124, 1);
	if (err < 0)
		goto err;
	err = snd_dg00x_write_quadlet(dg00x, rawch, left);
	if (err < 0)
		goto err;
	err = snd_dg00x_write_quadlet(dg00x, rawch+0x04, right);
	if (err < 0)
		goto err;
	control->private_value = new_val;
err:
	return err < 0 ? err : 1;
}

static int dg00x_mixmatrix_get_1(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_1L);
}

static int dg00x_mixmatrix_put_1(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_1L);
}

static int dg00x_mixmatrix_get_2(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_2L);
}

static int dg00x_mixmatrix_put_2(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_2L);
}

static int dg00x_mixmatrix_get_3(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_3L);
}

static int dg00x_mixmatrix_put_3(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_3L);
}

static int dg00x_mixmatrix_get_4(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_4L);
}

static int dg00x_mixmatrix_put_4(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_4L);
}

static int dg00x_mixmatrix_get_5(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_5L);
}

static int dg00x_mixmatrix_put_5(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_5L);
}

static int dg00x_mixmatrix_get_6(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_6L);
}

static int dg00x_mixmatrix_put_6(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_6L);
}

static int dg00x_mixmatrix_get_7(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_7L);
}

static int dg00x_mixmatrix_put_7(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_7L);
}

static int dg00x_mixmatrix_get_8(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ANALOG_8L);
}

static int dg00x_mixmatrix_put_8(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ANALOG_8L);
}

static int dg00x_mixmatrix_get_9(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_SPDIF_1L);
}

static int dg00x_mixmatrix_put_9(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_SPDIF_1L);
}

static int dg00x_mixmatrix_get_10(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_SPDIF_2L);
}

static int dg00x_mixmatrix_put_10(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_SPDIF_2L);
}

static int dg00x_mixmatrix_get_11(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_1L);
}

static int dg00x_mixmatrix_put_11(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_1L);
}

static int dg00x_mixmatrix_get_12(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_2L);
}

static int dg00x_mixmatrix_put_12(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_2L);
}

static int dg00x_mixmatrix_get_13(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_3L);
}

static int dg00x_mixmatrix_put_13(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_3L);
}

static int dg00x_mixmatrix_get_14(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_4L);
}

static int dg00x_mixmatrix_put_14(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_4L);
}

static int dg00x_mixmatrix_get_15(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_5L);
}

static int dg00x_mixmatrix_put_15(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_5L);
}

static int dg00x_mixmatrix_get_16(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_6L);
}

static int dg00x_mixmatrix_put_16(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_6L);
}

static int dg00x_mixmatrix_get_17(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_7L);
}

static int dg00x_mixmatrix_put_17(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_7L);
}

static int dg00x_mixmatrix_get_18(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_get(control, value, DG00X_MIX_ADAT_8L);
}

static int dg00x_mixmatrix_put_18(struct snd_kcontrol *control,
				 struct snd_ctl_elem_value *value)
{
	return dg00x_mixmatrix_put(control, value, DG00X_MIX_ADAT_8L);
}

static int dg00x_clock_info(struct snd_kcontrol *control,
			    struct snd_ctl_elem_info *info)
{
	static const char *const texts[4] = {
		"Internal",
		"S/PDIF",
		"ADAT",
		"WordClock"
	};

	return snd_ctl_enum_info(info, 1, ARRAY_SIZE(texts), texts);
}

static int dg00x_mixmatrix_info(struct snd_kcontrol *control,
				struct snd_ctl_elem_info *info)
{
	static const char *const texts[4] = {
		"NONE",
		"1",
		"2",
		"1+2",
	};

	return snd_ctl_enum_info(info, 1, ARRAY_SIZE(texts), texts);
}

static struct snd_kcontrol_new snd_dg00x_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Clock Source",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_clock_info,
		.get = dg00x_clock_get,
		.put = dg00x_clock_put,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 1",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_1,
		.put = dg00x_mixmatrix_put_1,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 2",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_2,
		.put = dg00x_mixmatrix_put_2,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 3",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_3,
		.put = dg00x_mixmatrix_put_3,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 4",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_4,
		.put = dg00x_mixmatrix_put_4,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 5",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_5,
		.put = dg00x_mixmatrix_put_5,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 6",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_6,
		.put = dg00x_mixmatrix_put_6,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 7",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_7,
		.put = dg00x_mixmatrix_put_7,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix 8",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_8,
		.put = dg00x_mixmatrix_put_8,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix S/PDIF 1",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_9,
		.put = dg00x_mixmatrix_put_9,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix S/PDIF 2",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_10,
		.put = dg00x_mixmatrix_put_10,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 1",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_11,
		.put = dg00x_mixmatrix_put_11,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 2",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_12,
		.put = dg00x_mixmatrix_put_12,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 3",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_13,
		.put = dg00x_mixmatrix_put_13,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 4",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_14,
		.put = dg00x_mixmatrix_put_14,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 5",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_15,
		.put = dg00x_mixmatrix_put_15,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 6",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_16,
		.put = dg00x_mixmatrix_put_16,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 7",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_17,
		.put = dg00x_mixmatrix_put_17,
		.private_value = 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mix ADAT 8",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_mixmatrix_info,
		.get = dg00x_mixmatrix_get_18,
		.put = dg00x_mixmatrix_put_18,
		.private_value = 0
	},
};

int snd_dg00x_create_mixer(struct snd_dg00x *dg00x)
{
	unsigned int i;
	int err;

	err = dg00x_clock_command(dg00x, &dg00x->clock, CTL_READ);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(snd_dg00x_controls); ++i) {
		err = snd_ctl_add(dg00x->card,
				  snd_ctl_new1(&snd_dg00x_controls[i], dg00x));
		if (err < 0)
			return err;
	}

	return 0;
}
