/*
 * 003_lowlevel.c  Digidesign 003Rack driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (C) 2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Damien Zammit <damien@zamaudio.com>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "003_lowlevel.h"

void write_quadlet(struct snd_efw *digi, unsigned long long int reg, unsigned int data)
{
	data = BYTESWAP32_CONST(data);
	snd_fw_transaction(digi->unit, TCODE_WRITE_QUADLET_REQUEST, reg, &data, 4);
}

unsigned int read_quadlet(struct snd_efw *digi, unsigned long long int reg)
{
	unsigned int data = 0;
	snd_fw_transaction(digi->unit, TCODE_READ_QUADLET_REQUEST, reg, &data, 4);
	return BYTESWAP32_CONST(data);
}

static inline int poll_until(struct snd_efw *digi, unsigned long long int reg, unsigned int expect)
{
	int timeout = 1024;
	while (read_quadlet(digi, reg) != expect && --timeout);
	return ( timeout == 0 );
}

static void rack_init_write_814_block(struct snd_efw *digi)
{
/*
 * write_block_request, offs=0xffffe0000008, data_length=0x0008, extended_tcode=0x0000, data=[ffc2ffff 00000000]
 * write_block_request, offs=0xffffe0000014, data_length=0x0008, extended_tcode=0x0000, data=[ffc2ffff 00000040]
 */
#if 1   /* use transaction */
	__be32 data[2];
	data[0] = BYTESWAP32_CONST(0xffc2ffff);
	data[1] = BYTESWAP32_CONST(0x00000000);
	snd_fw_transaction(digi->unit, TCODE_WRITE_BLOCK_REQUEST, 0xffffe0000008ULL, &data, 8);

	data[0] = BYTESWAP32_CONST(0xffc2ffff);
	data[1] = BYTESWAP32_CONST(0x00000040);
	snd_fw_transaction(digi->unit, TCODE_WRITE_BLOCK_REQUEST, 0xffffe0000014ULL, &data, 8);

#elif 0 /* write two quadlets instead of continuous block */

	write_quadlet(digi, 0xffffe0000008ULL, 0xffc2ffff);
	write_quadlet(digi, 0xffffe000000cULL, 0x00000000);

	write_quadlet(digi, 0xffffe0000014ULL, 0xffc2ffff);
	write_quadlet(digi, 0xffffe0000018ULL, 0x00000040);
#endif
}

int rack_init(struct snd_efw *digi)
{
	/* sweep read all data regs */
	int i;
	for (i=0; i < /* 0x468 */ 0x010; i++) {
		if (i == 4) continue;
		read_quadlet(digi, 0xfffff0000400ULL + i);
	}
	read_quadlet(digi, 0xfffff0000400ULL);

	/* initialization sequence */

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000002);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000000); // set samplerate?

	rack_init_write_814_block(digi);

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000001); // set samplerate?
	write_quadlet(digi, 0xffffe0000100ULL, 0x00000000); // ??
	write_quadlet(digi, 0xffffe0000100ULL, 0x00000001); // ??

	write_quadlet(digi, 0xffffe000011cULL, 0x00000000); //use for x44.1?
	write_quadlet(digi, 0xffffe0000120ULL, 0x00000003); //use for x44.1?

	write_quadlet(digi, 0xffffe0000118ULL, 0x00000000); // set clocksrc

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000001); // start streaming
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000001)) return -1;

	read_quadlet(digi, 0xffffe0000118ULL); // reset clock

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000000);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000124ULL, 0x00000001); //enable midi or low latency?

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000001); // start streaming
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000001)) return -1;

	read_quadlet(digi, 0xffffe0000118ULL); // reset clock

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000000);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000003);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000003)) return -1;

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000002);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000000); // set samplerate?

	rack_init_write_814_block(digi);

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000000); // set samplerate?

	write_quadlet(digi, 0xffffe0000100ULL, 0x00000000); // ??
	write_quadlet(digi, 0xffffe0000100ULL, 0x00000001); // ??

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000001); // start streaming
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000001)) return -1;

	read_quadlet(digi, 0xffffe0000118ULL); // reset clock

	write_quadlet(digi, 0xffffe0000124ULL, 0x00000001); // stop control

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000000);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000003); // shutdown streaming
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000003)) return -1;

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000002);
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000000)) return -1;

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000000); // set samplerate?

	rack_init_write_814_block(digi);

	write_quadlet(digi, 0xffffe0000110ULL, 0x00000001); // set samplerate?

	write_quadlet(digi, 0xffffe0000100ULL, 0x00000000); // ??
	write_quadlet(digi, 0xffffe0000100ULL, 0x00000001); // ??

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000001); // start streaming
	if (poll_until(digi, 0xffffe0000000ULL, 0x00000001)) return -1;

	read_quadlet(digi, 0xffffe0000118ULL); // reset clock

	write_quadlet(digi, 0xffffe0000124ULL, 0x00000001); // stop control

#if 0
	//write_quadlet(digi, 0xffffe0000124ULL, 0x00000000); // start control
	/* No monitoring of inputs */

	write_quadlet(digi, R003_MIX_ANALOG_1L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_1R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_2L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_2R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_3L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_3R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_4L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_4R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_5L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_5R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_6L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_6R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_7L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_7R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_8L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ANALOG_8R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_SPDIF_1L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_SPDIF_1R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_SPDIF_1L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_SPDIF_1R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_1L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_1R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_2L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_2R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_3L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_3R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_4L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_4R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_5L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_5R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_6L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_6R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_7L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_7R, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_8L, R003_MIX_NONE);
	write_quadlet(digi, R003_MIX_ADAT_8R, R003_MIX_NONE);
#endif
	return 0;
}

void rack_shutdown(struct snd_efw *digi)
{
#if 0
	write_quadlet(digi, 0xffffe0000124ULL, 0x00000001);   // stop control
#endif
	write_quadlet(digi, 0xffffe0000004ULL, 0x00000000);   // stop streams
	poll_until(digi, 0xffffe0000000ULL, 0x00000000);

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000001);   // start streams
	poll_until(digi, 0xffffe0000000ULL, 0x00000001);
	write_quadlet(digi, 0xffffe0000118ULL, 0x00000000);

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000000);   // stop streams
	poll_until(digi, 0xffffe0000000ULL, 0x00000000);

	write_quadlet(digi, 0xffffe0000004ULL, 0x00000003);   // shutdown streams
	poll_until(digi, 0xffffe0000000ULL, 0x00000003);
}

void digi_free_resources(struct snd_efw *digi, struct amdtp_stream *stream)
{
	rack_shutdown(digi);
	//fw_iso_resources_free(&stream->conn);
}

int digi_allocate_resources(struct snd_efw *digi, enum amdtp_stream_direction direction)
{
	//int err;
/*
	if (direction == AMDTP_STREAM_RECEIVE) {
		if (digi->receive_stream.conn.allocated)
			return 0;

		err = fw_iso_resources_allocate(&(digi->receive_stream.conn),
			amdtp_stream_get_max_payload(&(digi->receive_stream.strm)),
			fw_parent_device(digi->unit)->max_speed);
	} else {
		if (digi->transmit_stream.conn.allocated)
                        return 0;

                err = fw_iso_resources_allocate(&(digi->transmit_stream.conn),
                        amdtp_stream_get_max_payload(&(digi->transmit_stream.strm)),                       
                        fw_parent_device(digi->unit)->max_speed);
	}

	if (err < 0)
		return err;
*/	
	printk("ALLOCATE FAKE ISO SUCCEEDED\n");
	return 0;
}

