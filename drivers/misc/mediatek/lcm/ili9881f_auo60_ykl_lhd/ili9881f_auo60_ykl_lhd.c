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
#define LOG_TAG  "LCM"
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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_gpio.h>
#endif
#endif
#ifdef CONFIG_MTK_LEGACY
#include <cust_gpio_usage.h>
#endif

#ifdef BUILD_LK
#define printk(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define printk(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID	 0x3821

static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
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

#define set_gpio_lcd_enp(cmd) \
  lcm_util.set_gpio_lcd_enp_bias(cmd)

#define LCM_DSI_CMD_MODE									0
#define FRAME_WIDTH  										(720)
#define FRAME_HEIGHT 										(1440)

#ifndef CONFIG_FPGA_EARLY_PORTING
#endif
#define REGFLAG_DELAY             							0xAB
#define REGFLAG_END_OF_TABLE      							0xAA   // END OF REGISTERS MARKER

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE  (1)
#endif

static unsigned int lcm_compare_id(void);

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))
#define SET_GPIO_OUT(gpio_num,val)    						(lcm_util.set_gpio_out((gpio_num),(val)))

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))


// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	        lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)										lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)					lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)                                                                                   lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)                                   lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)  

#define GPIO_LCM_RST_1         (GPIO83 | 0x80000000)
#define GPIO_LCM_RST           (83+343)
#define GPIO_TP_RST_1          (GPIO104 | 0x80000000)
// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

#ifdef BUILD_LK
static void lcm_set_rst_lk(int output)
{
  mt_set_gpio_mode(GPIO_LCM_RST_1, GPIO_MODE_00);
  mt_set_gpio_dir(GPIO_LCM_RST_1, GPIO_DIR_OUT);
  mt_set_gpio_out(GPIO_LCM_RST_1, (output > 0) ? GPIO_OUT_ONE : GPIO_OUT_ZERO);
}
#endif
struct LCM_setting_table
{
  unsigned char cmd;
  unsigned char count;
  unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] ={
 //initial
{0xFF,3,{0x98,0x81,0x02}}, 	//page2
{0x4D,1,{0x0E}}, 		//turn off source precharge
{0x4F,1,{0x00}},		 //Latch CLK delay
//{0x63,1,{0xD1}},
{0x66,1,{0xD8}}, 	

{0xFF,3,{0x98,0x81,0x06}},		//Page6
{0x45,1,{0x00}},		//Sleep Out OTP Reload Disable (02h=Enable)
//{0x43,1,{0x01}},		//VCI&VCL Pump Mode
{0x2E,1,{0x01}},
{0xC0,1,{0xCF}},
{0xC1,1,{0x02}},
{0x19,1,{0xAC}},
{0x1B,1,{0x03}},

// GIP
{0xFF,3,{0x98,0x81,0x01}},
{0x00,1,{0x46}},
{0x01,1,{0x0F}},
{0x02,1,{0x00}}, //00
{0x03,1,{0x18}}, //18
{0x04,1,{0x42}},
{0x05,1,{0x0F}},
{0x06,1,{0x00}}, //00
{0x07,1,{0x18}}, //18
{0x08,1,{0x86}},
{0x09,1,{0x00}},
{0x0A,1,{0x73}},
{0x0B,1,{0x10}},
{0x0C,1,{0x00}}, //00
{0x0D,1,{0x00}}, //00
{0x0E,1,{0x18}}, //18
{0x0F,1,{0x18}}, //18
         
{0x14,1,{0x87}},
{0x15,1,{0x87}},
{0x16,1,{0x86}},
{0x17,1,{0x00}},
{0x18,1,{0x74}},
{0x19,1,{0x11}}, //11
         
{0x1A,1,{0x00}}, //00
{0x1C,1,{0x0B}}, //00
          
//AUO P1-12
//{0x1A,1,{0x18}},
//{0x1C,1,{0x18}},
          
{0x22,1,{0x87}},
{0x23,1,{0x87}},
          
{0x31,1,{0x30}},
{0x32,1,{0x30}},
{0x33,1,{0x0C}},
{0x34,1,{0x0E}},
{0x35,1,{0x1C}},
{0x36,1,{0x20}},
{0x37,1,{0x1E}},
{0x38,1,{0x22}},
{0x39,1,{0x14}},
{0x3A,1,{0x14}},
{0x3B,1,{0x10}},
{0x3C,1,{0x10}},
{0x3D,1,{0x16}},
{0x3E,1,{0x16}},
{0x3F,1,{0x12}},
          
{0x40,1,{0x12}},
{0x41,1,{0x08}},
{0x42,1,{0x0A}},
{0x43,1,{0x30}},
{0x44,1,{0x30}},
{0x45,1,{0x30}},
{0x46,1,{0x30}},
{0x47,1,{0x30}},
{0x48,1,{0x30}},
{0x49,1,{0x0D}},
{0x4A,1,{0x0F}},
{0x4B,1,{0x1D}},
{0x4C,1,{0x21}},
{0x4D,1,{0x1F}},
{0x4E,1,{0x23}},
{0x4F,1,{0x15}},
          
{0x50,1,{0x15}},
{0x51,1,{0x11}},
{0x52,1,{0x11}},
{0x53,1,{0x17}},
{0x54,1,{0x17}},
{0x55,1,{0x13}},
{0x56,1,{0x13}},
{0x57,1,{0x09}},
{0x58,1,{0x0B}},
{0x59,1,{0x30}},
{0x5A,1,{0x30}},
{0x5B,1,{0x30}},
{0x5C,1,{0x30}},
          
{0x61,1,{0x30}},
{0x62,1,{0x30}},
{0x63,1,{0x0B}},
{0x64,1,{0x09}},
{0x65,1,{0x1F}},
{0x66,1,{0x23}},
{0x67,1,{0x1D}},
{0x68,1,{0x21}},
{0x69,1,{0x13}},
{0x6A,1,{0x13}},
{0x6B,1,{0x17}},
{0x6C,1,{0x17}},
{0x6D,1,{0x11}},
{0x6E,1,{0x11}},
{0x6F,1,{0x15}},
          
{0x70,1,{0x15}},
{0x71,1,{0x0F}},
{0x72,1,{0x0D}},
{0x73,1,{0x30}},
{0x74,1,{0x30}},
{0x75,1,{0x30}},
{0x76,1,{0x30}},
{0x77,1,{0x30}},
{0x78,1,{0x30}},
{0x79,1,{0x0A}},
{0x7A,1,{0x08}},
{0x7B,1,{0x1E}},
{0x7C,1,{0x22}},
{0x7D,1,{0x1C}},
{0x7E,1,{0x20}},
{0x7F,1,{0x12}},
          
{0x80,1,{0x12}},
{0x81,1,{0x16}},
{0x82,1,{0x16}},
{0x83,1,{0x10}},
{0x84,1,{0x10}},
{0x85,1,{0x14}},
{0x86,1,{0x14}},
{0x87,1,{0x0E}},
{0x88,1,{0x0C}},
{0x89,1,{0x30}},
{0x8A,1,{0x30}},
{0x8B,1,{0x30}},
{0x8C,1,{0x30}},
          
//{0xB0,1,{0x44}}, //33
//{0xB1,1,{0x44}}, //33
{0xB0,1,{0x33}},  
{0xB1,1,{0x33}},  
//        
//{0xB2,1,{0x04}},
          
          
{0xCA,1,{0x44}},
{0xD0,1,{0x01}},
{0xD1,1,{0x20}},
{0xD5,1,{0x45}},
{0xDF,1,{0xB7}},
{0xE0,1,{0x6E}},
{0xE2,1,{0x52}},
{0xE6,1,{0x42}},
{0xE7,1,{0x50}},

{0xFF,3,{0x98,0x81,0x0E}},

{0x00,1,{0xA0}},               //A1 LONG H A3 LONG V RA
{0x02,1,{0x09}},

{0xFF,3,{0x98,0x81,0x0E}}, //pageE

{0x07,1,{0x31}}, //bit6 TSXD gating,0x bit 0 tp term modulation enable

{0x2D,1,{0x97}}, //RTN2 setting
{0x25,1,{0x06}}, //Unit0 term2 setting
{0x26,1,{0x4C}}, //Unit0 term2 setting
{0x29,1,{0x41}}, //Unit line number
//------term1 tuning parameter--------------------------
{0xc0,1,{0x01}}, //how many term1
{0xc1,1,{0x10}}, //term1 period01
{0xc2,1,{0x01}}, //T63 period

//SRC output,0x 0 gnd; 1 hi-z; 2 G0; 3 G255; 4 Last Data
{0x2E,1,{0x00}}, //term1_1 PRZ1 & PRZ2

//-----term2 tuning parameter---------------------------

{0xc3,1,{0x00}}, //term2 SRC keep 00 gnd; 01 hi-z; 02 G0; 03 G255; 04 Last Data
//{0xc8,1,{0x41}}, //SRC cut TP connect

//------term3 tuning parameter---------------------------
{0xD0,1,{0x13}}, //03 // 0x1x re-scan 2 line,0x 0x2x re-scan 1 line
//{0xD1,1,{0x80}},
//{0xD2,1,{0x80}},
//{0xD3,1,{0x95}},
//{0xD4,1{0x91}},  //9C
{0xD5,1,{0x10}},  //10
{0xD6,1,{0x80}},  //80
{0xD7,1,{0x80}},  //80
{0xD8,1,{0x00}},  //00	 
//{0xD9,1,{0x88}},
//{0xDA,1,{0x08}},
//{0xDB,1,{0x08}},

//add
{0xDC,1,{0x08}},
{0xDD,1,{0x08}},
{0xDE,1,{0x88}},
{0xDF,1,{0x88}},
      
{0x2B,1,{0x04}}, // NUM-1
      
{0x60,1,{0x11}},
{0x62,1,{0x12}},
{0x64,1,{0x13}},
{0x66,1,{0x14}},
{0x68,1,{0x15}},
      
      
{0x61,1,{0x2B}},
{0x63,1,{0x31}},
{0x65,1,{0x38}},
{0x67,1,{0x3E}},
{0x69,1,{0x41}},


//0816
{0xFF,3,{0x98,0x81,0x05}},		//Page5
{0x04,1,{0x40}},		//VCOM	

//0816 TEST
//{0xFF,3,{0x98,0x81,0x05}},		//Page5
//{0x23,1,{0x93}},		//VGHO1	= 15V
//{0x24,1,{0x93}},		//VGHO2	
//{0x25,1,{0x57}},		//VGLO1	
//{0x26,1,{0x57}},		//VGLO2	= -10V
//{0x27,1,{0x95}},		//VGH = 18.7V
//{0x28,1,{0x59}},		//VGL = -12.5V

//0818 ADD
//{0x2E,1,{0xEB}},		//VGH 3.5X

//0620
{0x29,1,{0x65}},  		//GVDDP = 5.2V
{0x2A,1,{0x65}},   		//GVDDN = -5.2V

//0620
{0xFF,3,{0x98,0x81,0x08}},         //Page8
{0x00,1,{0x00}},	
{0x01,1,{0x27}},	
{0x02,1,{0x00}},	
{0x03,1,{0x69}},	
{0x04,1,{0x00}},	
{0x05,1,{0x97}},	
{0x06,1,{0x00}},	
{0x07,1,{0xD3}},	
{0x08,1,{0x01}},	
{0x09,1,{0x04}},	
{0x0A,1,{0x01}},	
{0x0B,1,{0x2A}},	
{0x0C,1,{0x01}},	
{0x0D,1,{0x57}},	
{0x0E,1,{0x01}},	
{0x0F,1,{0x7B}},	
{0x10,1,{0x01}},	
{0x11,1,{0xB5}},	
{0x12,1,{0x01}},	
{0x13,1,{0xE3}},	
{0x14,1,{0x02}},	
{0x15,1,{0x0C}},	
{0x16,1,{0x02}},	
{0x17,1,{0x34}},	
{0x18,1,{0x02}},	
{0x19,1,{0x60}},	
{0x1A,1,{0x02}},	
{0x1B,1,{0x96}},	
{0x1C,1,{0x02}},	
{0x1D,1,{0xBA}},	
{0x1E,1,{0x02}},	
{0x1F,1,{0xE9}},	
{0x20,1,{0x03}},	
{0x21,1,{0x11}},	
{0x22,1,{0x03}},	
{0x23,1,{0x46}},	
{0x24,1,{0x03}},	
{0x25,1,{0x84}},	
{0x26,1,{0x03}},	
{0x27,1,{0xAF}},	
{0x28,1,{0x03}},	
{0x29,1,{0xC5}},	


//Neg Register
{0x80,1,{0x00}},	
{0x81,1,{0x27}},	
{0x82,1,{0x00}},	
{0x83,1,{0x69}},	
{0x84,1,{0x00}},	
{0x85,1,{0x97}},	
{0x86,1,{0x00}},	
{0x87,1,{0xD3}},	
{0x88,1,{0x01}},	
{0x89,1,{0x04}},	
{0x8A,1,{0x01}},	
{0x8B,1,{0x2A}},	
{0x8C,1,{0x01}},	
{0x8D,1,{0x57}},	
{0x8E,1,{0x01}},	
{0x8F,1,{0x7B}},	
{0x90,1,{0x01}},	
{0x91,1,{0xB5}},	
{0x92,1,{0x01}},	
{0x93,1,{0xE3}},	
{0x94,1,{0x02}},	
{0x95,1,{0x0C}},	
{0x96,1,{0x02}},	
{0x97,1,{0x34}},	
{0x98,1,{0x02}},	
{0x99,1,{0x60}},	
{0x9A,1,{0x02}},	
{0x9B,1,{0x96}},	
{0x9C,1,{0x02}},	
{0x9D,1,{0xBA}},	
{0x9E,1,{0x02}},	
{0x9F,1,{0xE9}},	
{0xA0,1,{0x03}},	
{0xA1,1,{0x11}},	
{0xA2,1,{0x03}},	
{0xA3,1,{0x46}},	
{0xA4,1,{0x03}},	
{0xA5,1,{0x84}},	
{0xA6,1,{0x03}},	
{0xA7,1,{0xAF}},	
{0xA8,1,{0x03}},	
{0xA9,1,{0xC5}},	



{0xFF,3,{0x98,0x81,0x00}},		//Page0
{0x35,1,{0x00}},

{0x11,1,{0x00}},
 {REGFLAG_DELAY,120,{}},
{0x29,1,{0x00}},//Display ON 
 {REGFLAG_DELAY,20,{}},

  {REGFLAG_END_OF_TABLE,0x00,{}}	
};
/*
   static struct LCM_setting_table lcm_sleep_out_setting[] = {
// Sleep Out
{0x11, 0, {0x00}},
{REGFLAG_DELAY, 120, {}},

// Display ON
{0x29, 0, {0x00}},
{REGFLAG_DELAY, 10, {}},

{REGFLAG_END_OF_TABLE, 0x00, {}}
};
 */

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
  // Display off sequence
  {0x28, 1, {0x00}},
  {REGFLAG_DELAY, 120, {}},
  // Sleep Mode On
  {0x10, 1, {0x00}},
  {REGFLAG_DELAY, 120, {}},
  {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
  unsigned int i;

  for(i = 0; i < count; i++)
  {

    unsigned cmd;
    cmd = table[i].cmd;

    switch (cmd)
    {

      case REGFLAG_DELAY :
        MDELAY(table[i].count);
        break;

      case REGFLAG_END_OF_TABLE :
        break;

      default:
        dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
        //MDELAY(2);
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

  params->type = LCM_TYPE_DSI;

  params->width = FRAME_WIDTH;
  params->height = FRAME_HEIGHT;

  // enable tearing-free
  params->dbi.te_mode = LCM_DBI_TE_MODE_VSYNC_ONLY;
  params->dbi.te_edge_polarity = LCM_POLARITY_RISING;

  params->dsi.mode   = SYNC_PULSE_VDO_MODE; //SYNC_PULSE_VDO_MODE;//BURST_VDO_MODE;

  // DSI
  /* Command mode setting */
  params->dsi.LANE_NUM = LCM_FOUR_LANE;
  //The following defined the fomat for data coming from LCD engine.
  params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
  params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
  params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
  params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

  params->dsi.intermediat_buffer_num = 0;	//because DSI/DPI HW design change, this parameters should be 0 when video mode in MT658X; or memory leakage

  params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;
  params->dsi.word_count = 720 * 3;
  params->dsi.ssc_disable = 1;  // ssc disable control (1: disable, 0: enable, default: 0)

  params->dsi.vertical_sync_active				= 2;
  params->dsi.vertical_backporch					= 16;
  params->dsi.vertical_frontporch					= 255;
  params->dsi.vertical_active_line				= FRAME_HEIGHT;

  params->dsi.horizontal_sync_active				= 5;
  params->dsi.horizontal_backporch				= 60;//60
  params->dsi.horizontal_frontporch				= 60;//40
  params->dsi.horizontal_active_pixel				= FRAME_WIDTH;

  params->dsi.PLL_CLOCK = 250;//250;  //321;
}

static void lcm_init(void)
{

#ifdef BUILD_LK
  mt_set_gpio_mode(GPIO_TP_RST_1, GPIO_MODE_00);
  mt_set_gpio_pull_enable(GPIO_TP_RST_1, GPIO_PULL_ENABLE);
  mt_set_gpio_dir(GPIO_TP_RST_1, GPIO_DIR_OUT);
  mt_set_gpio_out(GPIO_TP_RST_1, GPIO_OUT_ONE);

  lcm_set_rst_lk(1);
  MDELAY(50);
  lcm_set_rst_lk(0);
  MDELAY(10);
  lcm_set_rst_lk(1);
  MDELAY(120);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(50);
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
  MDELAY(10);
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(120);
#endif

  push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
  push_table(lcm_deep_sleep_mode_in_setting,sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
#ifdef BUILD_LK

  lcm_set_rst_lk(0);
  MDELAY(10);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
  MDELAY(10);
#endif
}

static void lcm_resume(void)
{
  lcm_init();
}

static unsigned int lcm_compare_id(void)
{

  unsigned int array[4];
  unsigned short device_id;
  unsigned char buffer[4];

#ifdef BUILD_LK
  lcm_set_rst_lk(1);
  MDELAY(10);
  lcm_set_rst_lk(0);
  MDELAY(20);
  lcm_set_rst_lk(1);
  MDELAY(20);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(10);
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
  MDELAY(20);
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(20);
#endif
  array[0] = 0x00043902;
  array[1] = 0x068198FF;
  dsi_set_cmdq(array, 2, 1);

  array[0] = 0x00013700;
  dsi_set_cmdq(array, 1, 1);
  read_reg_v2(0xF2, &buffer[0], 1);  //0xOd

  device_id = buffer[0];

#if defined(BUILD_LK)
  printf("%s, [ili9881f]  buffer[0] = [0x%x]  ID = [0x%x]\n",__func__,buffer[0], device_id);
#else
  printk("%s, [ili9881f]  buffer[0] = [0x%x]  ID = [0x%x]\n",__func__,buffer[0], device_id);
#endif
  return (0x1f == device_id)?1:0;


}

LCM_DRIVER ili9881f_auo60_ykl_lhd_lcm_drv =
{
  .name			  = "ili9881f_auo60_ykl_lhd",
  .set_util_funcs = lcm_set_util_funcs,
  .get_params     = lcm_get_params,
  .init           = lcm_init,
  .suspend        = lcm_suspend,
  .resume         = lcm_resume,
  .compare_id     = lcm_compare_id,
};
