/*****************************************************************************
*  Copyright Statement:
*  --------------------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of MediaTek Inc. (C) 2008
*
*  BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
*  THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
*  RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON
*  AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
*  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
*  NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
*  SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
*  SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH
*  THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
*  NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S
*  SPECIFICATION OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
*  BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
*  LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
*  AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
*  OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY BUYER TO
*  MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
*  THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
*  WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF
*  LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING THEREOF AND
*  RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN FRANCISCO, CA, UNDER
*  THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE (ICC).
*
*****************************************************************************/
#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
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
#else
/*#include <mach/mt_pm_ldo.h>*/
#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_gpio.h>
#endif
#endif
#ifdef CONFIG_MTK_LEGACY
#include <cust_gpio_usage.h>
#endif
#ifndef CONFIG_FPGA_EARLY_PORTING
#if defined(CONFIG_MTK_LEGACY)
#include <cust_i2c.h>
#endif
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#ifndef BUILD_LK
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
/* #include <linux/jiffies.h> */
/* #include <linux/delay.h> */
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#endif


// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  										(720)
#define FRAME_HEIGHT 										(1440)

#define REGFLAG_DELAY             							0xAB
#define REGFLAG_END_OF_TABLE      							0xAA   // END OF REGISTERS MARKER

#ifndef FALSE
  #define FALSE (0)
#endif

#ifndef TRUE
  #define TRUE  (1)
#endif

//#define GPIO_LCM_ID_PIN GPIO_LCD_ID_PIN

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    								(lcm_util.set_reset_pin((v)))
#define SET_GPIO_OUT(gpio_num,val)    						(lcm_util.set_gpio_out((gpio_num),(val)))


#define UDELAY(n) 											(lcm_util.udelay(n))
#define MDELAY(n) 											(lcm_util.mdelay(n))

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)										lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)					lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)                                                                                   lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)                                   lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)  
#define GPIO_LCD_ID_PIN  (0x80 | 0x80000000)


 
struct LCM_setting_table {
    unsigned char cmd;
    unsigned char count;
    unsigned char para_list[64];
};

//update initial param for IC ili9881 0.01
static struct LCM_setting_table lcm_initialization_setting[] = {
	
//************* Start Initial Sequence **********//}},
//gout_rstn_vgho_en=1
{0xFF,0x03,{0x98,0x81,0x03}},

{0x01,0x01,{0x00}},
{0x02,0x01,{0x00}},
{0x03,0x01,{0x53}},//73
{0x04,0x01,{0x13}},//73
{0x05,0x01,{0x00}},
{0x06,0x01,{0x04}},
{0x07,0x01,{0x00}},
{0x08,0x01,{0x00}},
{0x09,0x01,{0x14}},
{0x0a,0x01,{0x14}},
{0x0b,0x01,{0x00}},
{0x0c,0x01,{0x01}},
{0x0d,0x01,{0x00}},
{0x0e,0x01,{0x00}},
{0x0f,0x01,{0x14}},
{0x10,0x01,{0x14}},
{0x11,0x01,{0x00}},
{0x12,0x01,{0x00}},
{0x13,0x01,{0x00}},
{0x14,0x01,{0x00}},
{0x15,0x01,{0x00}},
{0x16,0x01,{0x00}}, 
{0x17,0x01,{0x00}},//00
{0x18,0x01,{0x00}},
{0x19,0x01,{0x00}},
{0x1a,0x01,{0x00}},
{0x1b,0x01,{0x00}},
{0x1c,0x01,{0x00}},
{0x1d,0x01,{0x00}},
{0x1e,0x01,{0x44}},
{0x1f,0x01,{0x80}},
{0x20,0x01,{0x02}},
{0x21,0x01,{0x03}},
{0x22,0x01,{0x00}},
{0x23,0x01,{0x00}},
{0x24,0x01,{0x00}},
{0x25,0x01,{0x00}},
{0x26,0x01,{0x00}},
{0x27,0x01,{0x00}},
{0x28,0x01,{0x33}},
{0x29,0x01,{0x03}},
{0x2a,0x01,{0x00}},
{0x2b,0x01,{0x00}},
{0x2c,0x01,{0x00}},
{0x2d,0x01,{0x00}},
{0x2e,0x01,{0x00}},
{0x2f,0x01,{0x00}},
{0x30,0x01,{0x00}},
{0x31,0x01,{0x00}},
{0x32,0x01,{0x00}},
{0x33,0x01,{0x00}},  
{0x34,0x01,{0x04}},
{0x35,0x01,{0x00}},
{0x36,0x01,{0x00}},
{0x37,0x01,{0x00}},
{0x38,0x01,{0x3C}},
{0x39,0x01,{0x00}},
{0x3a,0x01,{0x40}},//40
{0x3b,0x01,{0x40}},//40
{0x3c,0x01,{0x00}},
{0x3d,0x01,{0x00}},
{0x3e,0x01,{0x00}},
{0x3f,0x01,{0x00}},
{0x40,0x01,{0x00}},
{0x41,0x01,{0x00}},
{0x42,0x01,{0x00}},
{0x43,0x01,{0x00}},
{0x44,0x01,{0x00}},


//GIP_2
{0x50,0x01,{0x01}},
{0x51,0x01,{0x23}},
{0x52,0x01,{0x45}},
{0x53,0x01,{0x67}},
{0x54,0x01,{0x89}},
{0x55,0x01,{0xab}},
{0x56,0x01,{0x01}},
{0x57,0x01,{0x23}},
{0x58,0x01,{0x45}},
{0x59,0x01,{0x67}},
{0x5a,0x01,{0x89}},
{0x5b,0x01,{0xab}},
{0x5c,0x01,{0xcd}},
{0x5d,0x01,{0xef}},

//GIP_3
{0x5e,0x01,{0x11}},
{0x5f,0x01,{0x01}},
{0x60,0x01,{0x00}},
{0x61,0x01,{0x15}},
{0x62,0x01,{0x14}},
{0x63,0x01,{0x0C}},
{0x64,0x01,{0x0D}},
{0x65,0x01,{0x0E}},
{0x66,0x01,{0x0F}},
{0x67,0x01,{0x06}},
{0x68,0x01,{0x02}},
{0x69,0x01,{0x02}},
{0x6a,0x01,{0x02}},
{0x6b,0x01,{0x02}},
{0x6c,0x01,{0x02}},
{0x6d,0x01,{0x02}},
{0x6e,0x01,{0x08}},
{0x6f,0x01,{0x02}},
{0x70,0x01,{0x02}},
{0x71,0x01,{0x02}},
{0x72,0x01,{0x02}},
{0x73,0x01,{0x02}},
{0x74,0x01,{0x02}},
{0x75,0x01,{0x01}},
{0x76,0x01,{0x00}},
{0x77,0x01,{0x15}},
{0x78,0x01,{0x14}},
{0x79,0x01,{0x0C}},
{0x7a,0x01,{0x0D}},
{0x7b,0x01,{0x0E}},
{0x7c,0x01,{0x0F}},
{0x7d,0x01,{0x08}},
{0x7e,0x01,{0x02}},
{0x7f,0x01,{0x02}},
{0x80,0x01,{0x02}},
{0x81,0x01,{0x02}},
{0x82,0x01,{0x02}},
{0x83,0x01,{0x02}},
{0x84,0x01,{0x06}},
{0x85,0x01,{0x02}},
{0x86,0x01,{0x02}},
{0x87,0x01,{0x02}},
{0x88,0x01,{0x02}},
{0x89,0x01,{0x02}},
{0x8A,0x01,{0x02}},

//CMD_Page 4
{0xFF,0x03,{0x98,0x81,0x04}},
{0x6C,0x01,{0x15}},
{0x6E,0x01,{0x2B}},
{0x6F,0x01,{0x35}},
{0x35,0x01,{0x1F}},	//47解决闪屏
{0x33,0x01,{0x14}},
{0x3A,0x01,{0x24}},              
{0x8D,0x01,{0x14}},
{0x87,0x01,{0xBA}},                              
{0x26,0x01,{0x76}},            
{0xB2,0x01,{0xD1}},
//{0x00,0x01,{0x80}},
{0xB5,0x01,{0x06}},       

//CMD_Page 1
{0xFF,0x03,{0x98,0x81,0x01}},
{0x22,0x01,{0x09}},//0x0A flip & mirror
{0x31,0x01,{0x00}},
{0x53,0x01,{0xA9}},
{0x55,0x01,{0xB6}},
{0x50,0x01,{0xC7}},//VREG1  5.1V
{0x51,0x01,{0xC4}},//VREG2 -5.1V 
{0x60,0x01,{0x25}},//SDT=2.5us
{0x62,0x01,{0x00}},//EQ
{0x63,0x01,{0x00}}, //PC
{0x2E,0x01,{0xF0}},//F0//1440 GATE NL SEL  


//{0xFF,0x98,0x81,0x01}},
{0xA0,0x01,{0x08}},		
{0xA1,0x01,{0x24}},                     
{0xA2,0x01,{0x3a}},                  
{0xA3,0x01,{0x19}},                  
{0xA4,0x01,{0x1C}},                   
{0xA5,0x01,{0x2f}},                   
{0xA6,0x01,{0x1F}},              
{0xA7,0x01,{0x21}},      
{0xA8,0x01,{0xa5}},                
{0xA9,0x01,{0x1a}},                 
{0xAA,0x01,{0x25}},                 
{0xAB,0x01,{0x89}},                  
{0xAC,0x01,{0x17}},                   
{0xAD,0x01,{0x16}},                  
{0xAE,0x01,{0x49}},                 
{0xAF,0x01,{0x20}},                     
{0xB0,0x01,{0x28}},                   
{0xB1,0x01,{0x54}},                    
{0xB2,0x01,{0x64}},                      
{0xB3,0x01,{0x39}},                   
                                               
{0xC0,0x01,{0x08}},		
{0xC1,0x01,{0x38}},                    
{0xC2,0x01,{0x43}},                    
{0xC3,0x01,{0x0f}},                    
{0xC4,0x01,{0x12}},                    
{0xC5,0x01,{0x26}},                  
{0xC6,0x01,{0x1d}},                   
{0xC7,0x01,{0x1d}},                  
{0xC8,0x01,{0xa0}},                  
{0xC9,0x01,{0x1c}},                 
{0xCA,0x01,{0x2a}},                 
{0xCB,0x01,{0x87}},                   
{0xCC,0x01,{0x1b}},                  
{0xCD,0x01,{0x18}},                   
{0xCE,0x01,{0x4d}},                    
{0xCF,0x01,{0x21}},                    
{0xD0,0x01,{0x26}},                 
{0xD1,0x01,{0x5b}},                   
{0xD2,0x01,{0x6b}},                   
{0xD3,0x01,{0x39}},             
	
{0xFF,0x03,{0x98,0x81,0x00}},
	
{0x35,0x01,{0x00}},

{0x11,1,{0x00}},
{REGFLAG_DELAY, 120, {}},  
{0x29,1,{0x00}},	
{REGFLAG_DELAY, 20, {}},  

{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
    {0xFF,3,{0x98,0x81,0x01}},
    {0x53,1,{0x10}},
    {0xB3,1,{0x3F}},
    {0xD3,1,{0x3F}},
 
    {0xFF,3,{0x98,0x81,0x04}},  ////下BIST，用IC去刷黑
    {0x2D,1,{0x02}}, 
    {0x2F,1,{0x01}},
    {REGFLAG_DELAY, 100, {}},
    {0x2F,1,{0x00}},   ////关BIST

    
    {0xFF,3,{0x98,0x81,0x00}},
    // Display off sequence
    {0x28, 1, {0x00}},
    {REGFLAG_DELAY, 20, {}},
	// Display off sequence
    {0x28, 1, {0x00}},
    {REGFLAG_DELAY, 20, {}},

    // Sleep Mode On
    {0x10, 1, {0x00}},
    {REGFLAG_DELAY, 120, {}},

    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;

    for(i = 0; i < count; i++) {
		
        unsigned cmd;
        cmd = table[i].cmd;
		
        switch (cmd) {
			
            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;
				
            case REGFLAG_END_OF_TABLE :
                break;
				
            default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
       	}
    }
	
}

// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

static void lcm_get_params(LCM_PARAMS *params)
{
    memset(params, 0, sizeof(LCM_PARAMS));
    
    params->type   = LCM_TYPE_DSI;
    
    params->width  = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;
    
    // enable tearing-free
    params->dbi.te_mode				= LCM_DBI_TE_MODE_VSYNC_ONLY;
    params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;
    
    //params->dsi.mode   = SYNC_PULSE_VDO_MODE;
    params->dsi.mode   = BURST_VDO_MODE;	
    
    // DSI
    /* Command mode setting */
    params->dsi.LANE_NUM				= LCM_FOUR_LANE;
    
    //The following defined the fomat for data coming from LCD engine.
    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
    params->dsi.data_format.trans_seq	= LCM_DSI_TRANS_SEQ_MSB_FIRST;
    params->dsi.data_format.padding 	= LCM_DSI_PADDING_ON_LSB;
    params->dsi.data_format.format	  = LCM_DSI_FORMAT_RGB888;

	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active				= 8;
	params->dsi.vertical_backporch					= 16;
	params->dsi.vertical_frontporch					= 16;
	params->dsi.vertical_active_line				= FRAME_HEIGHT; 

	params->dsi.horizontal_sync_active				= 40;
	params->dsi.horizontal_backporch				= 100;
	params->dsi.horizontal_frontporch				= 70;
	params->dsi.horizontal_active_pixel				= FRAME_WIDTH;


	params->dsi.PLL_CLOCK = 212;

	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1; //modify for open esd
	params->dsi.lcm_esd_check_table[0].cmd          = 0x0A;
	params->dsi.lcm_esd_check_table[0].count        = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
}

static void lcm_init(void)
{
	SET_RESET_PIN(1);
	MDELAY(15);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(120);

    push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
	push_table(lcm_deep_sleep_mode_in_setting, sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);   //wqtao. enable
	SET_RESET_PIN(1);
	MDELAY(15);
    SET_RESET_PIN(0);	
    MDELAY(20);	
    SET_RESET_PIN(1);
	MDELAY(120);
}

static void lcm_resume(void)
{
    lcm_init();
}

static unsigned int lcm_compare_id(void)
{
    unsigned int id = 0xFF;
    unsigned char buffer[3];
    unsigned int data_array[16];
 
	SET_RESET_PIN(1);
	MDELAY(15);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(120);

    data_array[0] = 0x00043902;
    data_array[1] = 0x018198FF;
    dsi_set_cmdq(data_array, 2, 1);
    MDELAY(10);

    //set maximum return size
    data_array[0] = 0x00013700;
    dsi_set_cmdq(data_array, 1, 1);

    read_reg_v2(0x00, &buffer[0], 1);  //0x98

    id = buffer[0]; //we only need ID

#if defined(BUILD_LK)
    printf("%s,[LK] ili9881_hd720_dsi_vdo_gpo id = 0x%x\n", __func__, id);
#else
    printk("%s, ili9881_hd720_dsi_vdo_gpo id1 = 0x%x\n", __func__, id);
#endif

    return (0x98 == id)?1:0;
}

// ---------------------------------------------------------------------------
//  Get LCM Driver Hooks
// ---------------------------------------------------------------------------
LCM_DRIVER ili9881_hd720_dsi_vdo_lcm_drv = 
{
    .name          = "ili9881_hd720_dsi_vdo",
    .set_util_funcs 	= lcm_set_util_funcs,
    .get_params     	= lcm_get_params,
    .init           	= lcm_init,
    .suspend        	= lcm_suspend,
    .resume         	= lcm_resume,
    .compare_id     	= lcm_compare_id,
};
