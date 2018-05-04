#ifndef BF_TEE_SPI_H
#define BF_TEE_SPI_H

#include <linux/types.h>
#include <linux/cdev.h>
//#include "mt_spi.h"
#include <linux/spi/spi.h>

#define BF_DEV_NAME      "blfp"
#define BF_DEV_MAJOR 0	/* assigned */
#define BF_CLASS_NAME    "blfp"

#define NEED_OPT_POWER_ON	//power gpio
/* for netlink use */
#define MAX_NL_MSG_LEN 16
#define NETLINK_BF  30 //29

/*for kernel log*/
#define BLESTECH_LOG


#ifdef BLESTECH_LOG
#define BF_LOG(fmt,arg...)          do{printk("<blestech_fp>[%s:%d]"fmt"\n",__func__, __LINE__, ##arg);}while(0)
#else
#define BF_LOG(fmt,arg...)   	   do{}while(0)
#endif

typedef enum 
{
	BF_NETLINK_CMD_BASE = 100,

	BF_NETLINK_CMD_TEST  = BF_NETLINK_CMD_BASE+1,
	BF_NETLINK_CMD_IRQ = BF_NETLINK_CMD_BASE+2,
	BF_NETLINK_CMD_SCREEN_OFF = BF_NETLINK_CMD_BASE+3,
	BF_NETLINK_CMD_SCREEN_ON = BF_NETLINK_CMD_BASE+4
}fingerprint_socket_cmd_t;


struct bf_device {
	dev_t devno;
	struct cdev cdev;
	struct device *device;
	//struct class *class;
	int device_count;
	struct spi_device *spi;
	struct list_head device_entry;
	u32 reset_gpio;
	u32 irq_gpio;
	u32 irq_num;
	u8 irq_count;
	u8 sig_count;
	s32 report_key;
	u8  need_report;
#ifdef NEED_OPT_POWER_ON
	u32 power_en_gpio;
	u32 power1v8_en_gpio;
#endif
	struct pinctrl *pinctrl_gpios;
	struct pinctrl_state *pins_default,*pins_fp_interrupt;	
	struct pinctrl_state *pins_reset_high, *pins_reset_low;
#ifdef NEED_OPT_POWER_ON
	struct pinctrl_state *pins_power_high, *pins_power_low;
	struct pinctrl_state *pins_power_1v8_high, *pins_power_1v8_low;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#else
	struct notifier_block fb_notify;
#endif
	/* for netlink use */
	struct sock *netlink_socket;
	//struct mt_chip_conf mtk_spi_config;
};

#endif

