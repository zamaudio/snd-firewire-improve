/*
 * fireworks_transaction.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto <o-takashi@sakmocchi.jp>
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
 *
 * Mostly based on FFADO's souce, which is licensed under GPL version 2 (and
 * optionally version 3).
 */

/*
 * Fireworks have its own transaction.
 *
 * EFW transaction substance:
 *  At first, 6 data exist. Following to the 6 data, parameters for each
 *  commands exists. Most of parameters are 32 bit. But exception exists
 *  according to command.
 *   data[0]:	Length of transaction substance
 *   data[1]:	EFW transaction version
 *   data[2]:	Sequence number. This is incremented by both host and target
 *   data[3]:	transaction category
 *   data[4]:	transaction command
 *   data[5]:	return value in response.
 *   data[6-]:	parameters
 *
 * Transaction address:
 *  command:	0xecc000000000
 *  response:	0xecc080000000 (default)
 *
 * I note that the address for response can be changed by command. But this
 * module uses the default address.
 */
#include "./fireworks.h"

#define MEMORY_SPACE_EFW_COMMAND	0xecc000000000
#define MEMORY_SPACE_EFW_RESPONSE	0xecc080000000
/* this is for juju convinience? */
#define MEMORY_SPACE_EFW_END		0xecc080000200

#define ERROR_RETRIES 3
#define ERROR_DELAY_MS 5
#define EFC_TIMEOUT_MS 125

static DEFINE_SPINLOCK(instances_lock);
static struct snd_efw *instances[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static DEFINE_SPINLOCK(transaction_queues_lock);
static LIST_HEAD(transaction_queues);

enum transaction_queue_state {
	STATE_PENDING,
	STATE_BUS_RESET,
	STATE_COMPLETE
};

struct transaction_queue {
	struct list_head list;
	struct fw_unit *unit;
	void *buf;
	unsigned int size;
	u32 seqnum;
	enum transaction_queue_state state;
	wait_queue_head_t wait;
};

int snd_efw_transaction_cmd(struct fw_unit *unit,
			    const void *cmd, unsigned int size)
{
	return snd_fw_transaction(unit, TCODE_WRITE_BLOCK_REQUEST,
				  MEMORY_SPACE_EFW_COMMAND,
				  (void *)cmd, size, 0);
}

int snd_efw_transaction_run(struct fw_unit *unit,
			    const void *cmd, unsigned int cmd_size,
			    void *resp, unsigned int resp_size, u32 seqnum)
{
	struct transaction_queue t;
	unsigned int tries;
	int ret;

	t.unit = unit;
	t.buf = resp;
	t.size = resp_size;
	t.seqnum = seqnum + 1;
	t.state = STATE_PENDING;
	init_waitqueue_head(&t.wait);

	spin_lock_irq(&transaction_queues_lock);
	list_add_tail(&t.list, &transaction_queues);
	spin_unlock_irq(&transaction_queues_lock);

	tries = 0;
	do {
		ret = snd_efw_transaction_cmd(t.unit, (void *)cmd, cmd_size);
		if (ret < 0)
			break;

		wait_event_timeout(t.wait, t.state != STATE_PENDING,
				   msecs_to_jiffies(EFC_TIMEOUT_MS));

		if (t.state == STATE_COMPLETE) {
			ret = t.size;
			break;
		} else if (t.state == STATE_BUS_RESET) {
			msleep(ERROR_DELAY_MS);
		} else if (++tries >= ERROR_RETRIES) {
			dev_err(&t.unit->device, "EFC command timed out\n");
			ret = -EIO;
			break;
		}
	} while(1);

	spin_lock_irq(&transaction_queues_lock);
	list_del(&t.list);
	spin_unlock_irq(&transaction_queues_lock);

	return ret;
}

static void
copy_resp_to_buf(struct snd_efw *efw, void *data, size_t length, int *rcode)
{
	size_t capacity, till_end;
	struct snd_efw_transaction *t;

	spin_lock_irq(&efw->lock);

	t = (struct snd_efw_transaction *)data;
	length = min(t->length * sizeof(t->length), length);

	if (efw->push_ptr < efw->pull_ptr)
		capacity = (unsigned int)(efw->pull_ptr - efw->push_ptr);
	else
		capacity = resp_buf_size -
			((unsigned int)(efw->push_ptr - efw->pull_ptr));

	/* confirm enough space for this response */
	if (capacity < length) {
		*rcode = RCODE_CONFLICT_ERROR;
		goto end;
	}

	/* copy to ring buffer */
	while (length > 0) {
		till_end = resp_buf_size -
			    ((unsigned int)(efw->push_ptr - efw->resp_buf));
		till_end = min(length, till_end);

		memcpy(efw->push_ptr, data, till_end);

		efw->push_ptr += till_end;
		if (efw->push_ptr >= efw->resp_buf + resp_buf_size)
			efw->push_ptr = efw->resp_buf;

		length -= till_end;
		data += till_end;
	}

	/* for hwdep */
	efw->resp_queues++;
	wake_up(&efw->hwdep_wait);

	*rcode = RCODE_COMPLETE;
end:
	spin_unlock_irq(&efw->lock);
}

static void
handle_resp_for_user(struct fw_card *card, int generation, int source,
		     void *data, size_t length, int *rcode)
{
	struct fw_device *device;
	struct snd_efw *efw;
	unsigned int i;

	spin_lock_irq(&instances_lock);

	for (i = 0; i < SNDRV_CARDS; i++) {
		efw = instances[i];
		if (efw == NULL)
			continue;
		device = fw_parent_device(efw->unit);
		if ((device->card != card) ||
		    (device->generation != generation))
			continue;
		smp_rmb();	/* node id vs.generation */
		if (device->node_id != source)
			continue;

		break;
	}
	if (i == SNDRV_CARDS)
		goto end;

	copy_resp_to_buf(efw, data, length, rcode);
end:
	spin_unlock_irq(&instances_lock);
}

static void
handle_resp_for_kernel(struct fw_card *card, int generation, int source,
		       void *data, size_t length, int *rcode, u32 seqnum)
{
	struct fw_device *device;
	struct transaction_queue *t;
	unsigned long flags;

	spin_lock_irqsave(&transaction_queues_lock, flags);
	list_for_each_entry(t, &transaction_queues, list) {
		device = fw_parent_device(t->unit);
		if ((device->card != card) ||
		    (device->generation != generation))
			continue;
		smp_rmb();	/* node_id vs. generation */
		if (device->node_id != source)
			continue;

		if ((t->state == STATE_PENDING) && (t->seqnum == seqnum)) {
			t->state = STATE_COMPLETE;
			t->size = min((unsigned int)length, t->size);
			memcpy(t->buf, data, t->size);
			wake_up(&t->wait);
			*rcode = RCODE_COMPLETE;
		}
	}
	spin_unlock_irqrestore(&transaction_queues_lock, flags);
}

static void
efw_response(struct fw_card *card, struct fw_request *request,
	     int tcode, int destination, int source,
	     int generation, unsigned long long offset,
	     void *data, size_t length, void *callback_data)
{
	int rcode, dummy;
	u32 seqnum;

	rcode = RCODE_TYPE_ERROR;
	if (length < sizeof(struct snd_efw_transaction)) {
		rcode = RCODE_DATA_ERROR;
		goto end;
	} else if (offset != MEMORY_SPACE_EFW_RESPONSE) {
		rcode = RCODE_ADDRESS_ERROR;
		goto end;
	}

	seqnum = be32_to_cpu(((struct snd_efw_transaction *)data)->seqnum);
	if (seqnum > SND_EFW_TRANSACTION_SEQNUM_MAX) {
		handle_resp_for_kernel(card, generation, source,
				       data, length, &rcode, seqnum);
		if (resp_buf_debug)
			handle_resp_for_user(card, generation, source,
					     data, length, &dummy);
	} else {
		handle_resp_for_user(card, generation, source,
				     data, length, &rcode);
	}
end:
	fw_send_response(card, request, rcode);
}

void snd_efw_transaction_register_instance(struct snd_efw *efw)
{
	unsigned int i;

	spin_lock_irq(&instances_lock);

	for (i = 0; i < SNDRV_CARDS; i++) {
		if (instances[i] != NULL)
			continue;
		instances[i] = efw;
		break;
	}

	spin_unlock_irq(&instances_lock);
}

void snd_efw_transaction_unregister_instance(struct snd_efw *efw)
{
	unsigned int i;

	spin_lock_irq(&instances_lock);

	for (i = 0; i < SNDRV_CARDS; i++) {
		if (instances[i] != efw)
			continue;
		instances[i] = NULL;
	}

	spin_unlock_irq(&instances_lock);
}

void snd_efw_transaction_bus_reset(struct fw_unit *unit)
{
	struct transaction_queue *t;

	spin_lock_irq(&transaction_queues_lock);
	list_for_each_entry(t, &transaction_queues, list) {
		if ((t->unit == unit) &&
		    (t->state == STATE_PENDING)) {
			t->state = STATE_BUS_RESET;
			wake_up(&t->wait);
		}
	}
	spin_unlock_irq(&transaction_queues_lock);
}

static struct fw_address_handler resp_register_handler = {
	.length = MEMORY_SPACE_EFW_END - MEMORY_SPACE_EFW_RESPONSE,
	.address_callback = efw_response
};

int snd_efw_transaction_register(void)
{
	static const struct fw_address_region resp_register_region = {
		.start	= MEMORY_SPACE_EFW_RESPONSE,
		.end	= MEMORY_SPACE_EFW_END
	};
	return fw_core_add_address_handler(&resp_register_handler,
					   &resp_register_region);
}

void snd_efw_transaction_unregister(void)
{
	WARN_ON(!list_empty(&transaction_queues));
	fw_core_remove_address_handler(&resp_register_handler);
}