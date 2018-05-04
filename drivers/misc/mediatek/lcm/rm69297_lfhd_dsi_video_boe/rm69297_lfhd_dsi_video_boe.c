/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#include <mt-plat/mtk_gpio.h>
#include <mach/gpio_const.h>
#endif

#include "lcm_drv.h"


#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID_RM69297 (0xf5)

#ifdef BUILD_LK
#define GPIO_I2C_EN     (GPIO25 | 0x80000000)
#define GPIO_PWR_EN     (GPIO27 | 0x80000000)
#define GPIO_PWR1_EN    (GPIO28 | 0x80000000)
#else
extern void lcm_set_enp_bias(bool Val);
extern void lcm_set_reset(bool Val);
#endif

static const unsigned int BL_MIN_LEVEL = 20;
static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	lcm_set_reset(v)
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
    lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
    lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
    lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
    lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
    lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
    lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#ifdef BUILD_LK
#define TPS65670_SLAVE_ADDR_WRITE  0x60
static struct mt_i2c_t TPS65670_i2c;

static int tps65670_write_bytes(kal_uint8 addr, kal_uint8 value)
{
    kal_uint32 ret_code = I2C_OK;
    kal_uint8 write_data[2];
    kal_uint16 len;

    write_data[0] = addr;
    write_data[1] = value;

    TPS65670_i2c.id = 3; /* I2C3; */
    /* Since i2c will left shift 1 bit, we need to set FAN5405 I2C address to >>1 */
    TPS65670_i2c.addr = TPS65670_SLAVE_ADDR_WRITE;
    TPS65670_i2c.mode = ST_MODE;
    TPS65670_i2c.speed = 100;
    len = 2;

    ret_code = i2c_write(&TPS65670_i2c, write_data, len);
    printf("%s: i2c_write: ret_code: %d\n", __func__, ret_code);

    return ret_code;
}
#else
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#define I2C_ID_NAME "tps65670"

static const struct of_device_id lcm_of_match[] = {
    {.compatible = "mediatek,I2C_LCD_BIAS"},
    {},
};

/*static struct i2c_client *tps65670_i2c_client;*/
struct i2c_client *tps65670_i2c_client;

/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int tps65670_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tps65670_remove(struct i2c_client *client);
/*****************************************************************************
 * Data Structure
 *****************************************************************************/
struct tps65670_dev {
    struct i2c_client *client;
};

static const struct i2c_device_id tps65670_id[] = {
    {I2C_ID_NAME, 0},
    {}
};

/* #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)) */
/* static struct i2c_client_address_data addr_data = { .forces = forces,}; */
/* #endif */
static struct i2c_driver tps65670_iic_driver = {
    .id_table = tps65670_id,
    .probe = tps65670_probe,
    .remove = tps65670_remove,
    /* .detect               = mt6605_detect, */
    .driver = {
        .owner = THIS_MODULE,
        .name = "tps65670",
        .of_match_table = lcm_of_match,
    },
};

/*****************************************************************************
 * Function
 *****************************************************************************/
static int tps65670_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    LCM_LOGI("tps65670_iic_probe\n");
    LCM_LOGI("TPS: info==>name=%s addr=0x%x\n", client->name, client->addr);
    tps65670_i2c_client = client;
    return 0;
}

static int tps65670_remove(struct i2c_client *client)
{
    LCM_LOGI("tps65670_remove\n");
    tps65670_i2c_client = NULL;
    i2c_unregister_device(client);
    return 0;
}

int tps65670_write_bytes(unsigned char addr, unsigned char value)
{
    int ret = 0;
    struct i2c_client *client = tps65670_i2c_client;
    char write_data[2] = { 0 };
    write_data[0] = addr;
    write_data[1] = value;
    ret = i2c_master_send(client, write_data, 2);
    if (ret < 0)
        LCM_LOGI("tps65670 write data fail !!\n");
    return ret;
}

static int __init tps65670_iic_init(void)
{
    LCM_LOGI("tps65670_iic_init\n");
    i2c_add_driver(&tps65670_iic_driver);
    LCM_LOGI("tps65670_iic_init success\n");
    return 0;
}

static void __exit tps65670_iic_exit(void)
{
    LCM_LOGI("tps65670_iic_exit\n");
    i2c_del_driver(&tps65670_iic_driver);
}

module_init(tps65670_iic_init);
module_exit(tps65670_iic_exit);

MODULE_AUTHOR("Xiaokuan Shi");
MODULE_DESCRIPTION("MTK TPS65670 I2C Driver");
#endif

/* static unsigned char lcd_id_pins_value = 0xFF; */
#define FRAME_WIDTH										(1080)
#define FRAME_HEIGHT									(2160)

/* physical size in um */
#define LCM_PHYSICAL_WIDTH									(68040)
#define LCM_PHYSICAL_HEIGHT									(13608)

#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY	0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	0xFFFE
#define REGFLAG_RESET_HIGH	0xFFFF

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


struct LCM_setting_table {
    unsigned int cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table lcm_suspend_setting[] = {
    {0x28, 0, {}},
    {REGFLAG_DELAY, 20, {}},
    {0x10, 0, {}},
    {REGFLAG_DELAY, 120, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table init_setting_vdo[] = {
    {0xB0, 2, {0xA5,0x00}},
#ifdef BUILD_LK
    {0xB2, 2, {0x00,0x4C}},
#else
    {0xB2, 2, {0x00,0x4C}},
#endif
    {0x3D, 1, {0x10}},
    {0x55, 1, {0x0C}},
    {0xF8, 8, {0x00,0x08,0x10,0x00,0x22,0x00,0x00,0x2D}},
    {REGFLAG_DELAY, 10, {}},
    {0x11, 0, {} },
    {REGFLAG_DELAY, 20, {}},
    {0xC1,18, {0x00,0x00,0x00,0x19,0x12,0x19,0x19,0x2A,0x2A,0x2A,0x2F,0x2F,0x19,0x19,0x19,0x19,0x19,0x19}},
    {REGFLAG_DELAY, 100, {}},
    {0xB8,17, {0x0A,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x14,0x14,0x29,0x2A,0x0F,0x0F}},
    {0xDE, 5, {0x01,0x2C,0x00,0x77,0x3E}},
    {0x29, 0, {}},
    {REGFLAG_DELAY, 200, {}},
#ifdef BUILD_LK
    {0x51, 2, {0x80,0x00}},
#endif
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table init2_setting_vdo[] = {
    {0xB0, 2, {0xA5,0x00}},
#ifdef BUILD_LK
    {0xB2, 2, {0x00,0x4C}},
#else
    {0xB2, 2, {0x00,0x4C}},
#endif
    {0x3D, 1, {0x10}},
    {0x55, 1, {0x0C}},
    {0xF8, 8, {0x00,0x08,0x10,0x00,0x22,0x00,0x00,0x2D}},
    {REGFLAG_DELAY, 10, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table bl_level[] = {
    {0x51, 2, {0xFF,0x00}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
        unsigned int count, unsigned char force_update)
{
    unsigned int i;
    unsigned cmd;

    for (i = 0; i < count; i++) {
        cmd = table[i].cmd;

        switch (cmd) {
            case REGFLAG_DELAY:
                if (table[i].count <= 10)
                    MDELAY(table[i].count);
                else
                    MDELAY(table[i].count);
                break;
            case REGFLAG_UDELAY:
                UDELAY(table[i].count);
                break;
            case REGFLAG_END_OF_TABLE:
                break;
            default:
                dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
        }
    }
}

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

static void lcm_get_params(LCM_PARAMS *params)
{
    memset(params, 0, sizeof(LCM_PARAMS));

    params->type = LCM_TYPE_DSI;
    params->width = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;
#ifndef BUILD_LK
    params->physical_width = LCM_PHYSICAL_WIDTH/1000;
    params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
    params->physical_width_um = LCM_PHYSICAL_WIDTH;
    params->physical_height_um = LCM_PHYSICAL_HEIGHT;
#endif

    params->dsi.mode = SYNC_EVENT_VDO_MODE; //SYNC_EVENT_VDO_MODE;SYNC_PULSE_VDO_MODE;
    params->dsi.switch_mode_enable = 0;

    /* DSI */
    /* Command mode setting */
    params->dsi.LANE_NUM = LCM_FOUR_LANE;
    /* The following defined the fomat for data coming from LCD engine. */
    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
    params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
    params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
    params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

    /* Highly depends on LCD driver capability. */
    params->dsi.packet_size = 256;
    /* video mode timing */

    params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

    params->dsi.vertical_sync_active = 1;
    params->dsi.vertical_backporch = 15;
    params->dsi.vertical_frontporch = 16;
    //params->dsi.vertical_frontporch_for_low_power = 620;
    params->dsi.vertical_active_line = FRAME_HEIGHT;

    params->dsi.horizontal_sync_active = 24;
    params->dsi.horizontal_backporch = 96;
    params->dsi.horizontal_frontporch = 36;
    params->dsi.horizontal_active_pixel = FRAME_WIDTH;

    params->dsi.cont_clock = 1;

    /*params->dsi.ssc_disable = 1;*/
    params->dsi.PLL_CLOCK = 540;	/* this value must be in MTK suggested table */
    params->dsi.CLK_HS_POST = 36;
    params->dsi.clk_lp_per_line_enable = 0;
    params->dsi.esd_check_enable = 0;
    params->dsi.customization_esd_check_enable = 0;
    params->dsi.lcm_esd_check_table[0].cmd = 0x53;
    params->dsi.lcm_esd_check_table[0].count = 1;
    params->dsi.lcm_esd_check_table[0].para_list[0] = 0x24;
}

static void lcm_init_power(void)
{
#ifdef BUILD_LK
    unsigned char cmd = 0x0;
    unsigned char data = 0xFF;
    int ret = 0;

    mt_set_gpio_mode(GPIO_I2C_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_I2C_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_I2C_EN, GPIO_OUT_ONE);
    MDELAY(10);

    cmd = 0x00;
    data = 0xA1;
    ret = tps65670_write_bytes(cmd, data);
    cmd = 0x01;
    data = 0xB9;
    ret = tps65670_write_bytes(cmd, data);
    cmd = 0x02;
    data = 0x41;
    ret = tps65670_write_bytes(cmd, data);
    cmd = 0x03;
    data = 0xA1;
    ret = tps65670_write_bytes(cmd, data);
    cmd = 0x05;
    data = 0x78;
    ret = tps65670_write_bytes(cmd, data);
    //mt_set_gpio_mode(GPIO_I2C_EN, GPIO_MODE_00);
    //mt_set_gpio_dir(GPIO_I2C_EN, GPIO_DIR_OUT);
    //mt_set_gpio_out(GPIO_I2C_EN, GPIO_OUT_ZERO);
    //MDELAY(10);

    mt_set_gpio_mode(GPIO_PWR1_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR1_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR1_EN, GPIO_OUT_ONE);
    MDELAY(20);
    mt_set_gpio_mode(GPIO_PWR_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR_EN, GPIO_OUT_ZERO);
    MDELAY(20);
#else
    lcm_set_enp_bias(1);
#endif
}

static void lcm_suspend_power(void)
{
    unsigned char cmd = 0x0;
    unsigned char data = 0xFF;
    int ret = 0;

    cmd = 0x05;
    data = 0x30;
    ret = tps65670_write_bytes(cmd, data);
#ifdef BUILD_LK
    mt_set_gpio_mode(GPIO_PWR_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR_EN, GPIO_OUT_ZERO);
    mt_set_gpio_mode(GPIO_PWR1_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR1_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR1_EN, GPIO_OUT_ZERO);
    MDELAY(20);
#else
    lcm_set_enp_bias(0);
#endif
}

static void lcm_resume_power(void)
{
    unsigned char cmd = 0x0;
    unsigned char data = 0xFF;
    int ret = 0;

    SET_RESET_PIN(0);
    cmd = 0x05;
    data = 0x78;
    ret = tps65670_write_bytes(cmd, data);
#ifdef BUILD_LK
    mt_set_gpio_mode(GPIO_PWR1_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR1_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR1_EN, GPIO_OUT_ONE);
    MDELAY(20);
    mt_set_gpio_mode(GPIO_PWR_EN, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_PWR_EN, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO_PWR_EN, GPIO_OUT_ZERO);
    MDELAY(20);
#else
    lcm_set_enp_bias(1);
#endif
}

void lcm_init(void)
{
    SET_RESET_PIN(1);
    MDELAY(1);
    SET_RESET_PIN(0);
    MDELAY(10);
    SET_RESET_PIN(1);
    MDELAY(100);

    push_table(NULL, init_setting_vdo, sizeof(init_setting_vdo) / sizeof(struct LCM_setting_table), 1);
}

void lcm_init2(void)
{
    SET_RESET_PIN(1);
    MDELAY(1);
    SET_RESET_PIN(0);
    MDELAY(10);
    SET_RESET_PIN(1);
    MDELAY(100);

    push_table(NULL, init2_setting_vdo, sizeof(init2_setting_vdo) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
    //push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);
    //MDELAY(10);
    SET_RESET_PIN(0);
    MDELAY(10);
}

static void lcm_resume(void)
{
    lcm_init();
}

static unsigned int lcm_compare_id(void)
{
    unsigned int id = 0, version_id = 0;
    unsigned char buffer[2];
    unsigned int array[16];

    SET_RESET_PIN(1);
    SET_RESET_PIN(0);
    MDELAY(1);

    SET_RESET_PIN(1);
    MDELAY(20);

    array[0] = 0x00023700;	/* read id return two byte,version and id */
    dsi_set_cmdq(array, 1, 1);

    read_reg_v2(0xF4, buffer, 2);
    id = buffer[0];     /* we only need ID */

    read_reg_v2(0xDB, buffer, 1);
    version_id = buffer[0];

    LCM_LOGI("%s,rm69297_id=0x%08x,version_id=0x%x\n", __func__, id, version_id);

    if (id == LCM_ID_RM69297 && version_id == 0x81)
        return 1;
    else
        return 0;
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
    LCM_LOGI("%s,rm69297 backlight: level = %d\n", __func__, level);

    if (level > 0xE0) {
      level = 0xE0;
    } else if (level > 0x65 && level < 0x75) {
      level = 0x75;
    } else if (level > 0x0 && level < 0x05) {
      level = 0x05;
    }
    bl_level[0].para_list[0] = level;

    push_table(handle, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
}

LCM_DRIVER rm69297_lfhd_dsi_vdo_boe_lcm_drv = {
    .name = "rm69297_lfhd_dsi_video_boe_drv",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params = lcm_get_params,
    .init = lcm_init,
    .suspend = lcm_suspend,
    .resume = lcm_resume,
    .compare_id = lcm_compare_id,
    .init_power = lcm_init_power,
    .resume_power = lcm_resume_power,
    .suspend_power = lcm_suspend_power,
    .set_backlight_cmdq = lcm_setbacklight_cmdq,
};
