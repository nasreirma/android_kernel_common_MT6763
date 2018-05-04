# In case the platform does NOT support this type of sensors
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
# Vanzo:yucheng on: Sat, 15 Oct 2016 12:49:53 +0800
# added for aosp management to import our variable
project_name:=$(shell echo $(VANZO_INNER_PROJECT_NAME))
ifneq ($(strip $(project_name)),)
-include $(srctree)/../zprojects/$(project_name)/$(project_name).mk
VANZO_PROJECT_HEADERS:=$(srctree)/../zprojects/$(project_name)/kernel-headers
ccflags-y += -I$(VANZO_PROJECT_HEADERS)
endif
# End of Vanzo: yucheng
ifeq ($(CONFIG_CUSTOM_KERNEL_ALSPS_MODULE),y)
obj-y   +=  alsps_module.o
else
obj-y   +=  alsps.o
endif
obj-y   +=  alsps_factory.o
obj-y   +=  aal_control.o
# Vanzo:yucheng on: Sat, 15 Oct 2016 12:50:45 +0800
project_name:=$(shell echo $(VANZO_INNER_PROJECT_NAME))
 
ifneq ($(project_name),)
custom_kernel_alsps:= $(addsuffix /, $(shell echo $(CUSTOM_KERNEL_ALSPS)))
$(info "custom_kernel_alsps:$(custom_kernel_alsps)")
ifneq ($(custom_kernel_alsps),)
obj-y += $(custom_kernel_alsps)
endif
else
# End of Vanzo: yucheng

ifeq ($(CONFIG_MTK_SCP_SENSORHUB_V1),y)
obj-$(CONFIG_MTK_ALSPSHUB)      +=  alspshub/
obj-$(CONFIG_MTK_CM36558)	+=  cm36558/
obj-$(CONFIG_MTK_LTR579_NEW)	+=  ltr579-new/
obj-$(CONFIG_MTK_LTR578)	+=  ltr578/
endif
# Vanzo:yucheng on: Sat, 15 Oct 2016 12:51:46 +0800
# added for new structure config alsps
endif
# End of Vanzo: yucheng
