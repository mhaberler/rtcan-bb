obj-m := rtcan_c_can.o
EXTRA_CFLAGS := -I/usr/src/linux-headers-3.8.13-xenomai-r70/include/xenomai/ -I/home/mah/rtcan/can

#scp -r mah.priv.at:bb-kernel/KERNEL/drivers/xenomai/can .
#-I../..xenomai_kernel/usr/xenomai/include -I../../xenomai_kernel/xenomai-2.6.3/ksrc/drivers/can $(ADD_CFLAGS)

KDIR := /lib/modules/$(shell uname -r)/build
#/home/user/xeno_test/beagle-kernel/kernel

PWD := $(shell pwd)

#CROSS=arm-unknown-linux-gnueabi-
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ARCH=arm  modules

test:
	echo $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -rf *.mod.c *.ko *.o *.symvers *.order
