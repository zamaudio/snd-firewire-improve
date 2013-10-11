snd-firewire-lib-objs := lib.o iso-resources.o packets-buffer.o \
			 fcp.o amdtp.o 003/digimagic.o
#snd-firewire-speakers-objs := speakers.o
#snd-isight-objs := isight.o
#snd-scs1x-objs := scs1x.o

obj-m += snd-firewire-lib.o
#obj-$(CONFIG_SND_FIREWIRE_SPEAKERS) += snd-firewire-speakers.o
#obj-$(CONFIG_SND_ISIGHT) += snd-isight.o
#obj-$(CONFIG_SND_SCS1X) += snd-scs1x.o

#obj-$(CONFIG_SND_FIREWIRE) += fireworks/
#obj-$(CONFIG_SND_FIREWIRE) += bebob/
#obj-m += fireworks/
#obj-m += bebob/
obj-m += 003/

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -Wall -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -Wall -C $(KDIR) M=$(PWD) clean
