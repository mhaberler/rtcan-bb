obj-m := rtcan_c_can.o
EXTRA_CFLAGS := -I../..xenomai_kernel/usr/xenomai/include -I../../xenomai_kernel/xenomai-2.6.3/ksrc/drivers/can $(ADD_CFLAGS)

KDIR := /home/user/xeno_test/beagle-kernel/kernel

PWD := $(shell pwd)

CROSS=arm-unknown-linux-gnueabi-
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ARCH=arm CROSS_COMPILE=$(CROSS) modules

test:
	echo $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -rf *.mod.c *.ko *.o *.symvers *.order
