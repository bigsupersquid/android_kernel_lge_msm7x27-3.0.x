# Makefile for 2.6 kernels
# modified for squid3.0.8
#ifneq ($(KERNELRELEASE),)
#obj-m	:= frandom.o
obj-$(CONFIG_HW_FRANDOM)	+= frandom.o

#else
#KDIR := /lib/modules/$(shell uname -r)/build
#PWD := $(shell pwd)

#default:
#	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
#endif

