
ifneq ($(strip $(M)),)
	obj-m += tkcore_drv.o
else
	obj-y += tkcore_drv.o
endif

M ?= $(srctree)/drivers/misc/mediatek/tkcore

ccflags-y+=-I$(M)/include/arm_common
ccflags-y+=-I$(M)/include/linux
ccflags-y+=-I$(M)/include
ccflags-y+=-I$(M)/core

ifeq ($(CONFIG_MEDIATEK_SOLUTION), y)
ifeq ($(CONFIG_ARM64), y)
ccflags-y += -DTKCORE_BL
endif
endif

ifeq ($(CONFIG_TRUSTKERNEL_TEE_RPMB_SUPPORT),y)
ccflags-y += -DRPMB_SUPPORT
endif

tkcore_drv-objs:= \
		tee_tz_drv.o \
		tee_smc_xfer.o \
		tee_mem.o \
		handle.o

ifeq ($(CONFIG_ARM),y)
# "smc" assembly intruction requires dedicated "armv7 secure extension"
secext := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_tee_smc-arm.o := -Wa,-march=armv7-a$(secext)
tkcore_drv-objs += \
	tee_smc-arm.o
endif

ifeq ($(CONFIG_ARM64),y)
tkcore_drv-objs += \
	tee_smc-arm64.o
endif

