obj-m := rtcan_c_can.o
EXTRA_CFLAGS := -I/usr/src/linux-headers-$(shell uname -r)/include/xenomai/ -I/home/mah/rtcan-bb/can

KDIR := /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ARCH=arm  modules

test:
	echo $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -rf *.mod.c *.ko *.o *.symvers *.order


install: rtcan_c_can.ko
	cp rtcan_c_can.ko /lib/modules/$(shell uname -r)/kernel/drivers/xenomai/can
	depmod -a
