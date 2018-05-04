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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
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

#define FRAME_WIDTH  (720)
#define FRAME_HEIGHT (1440)

#define REGFLAG_DELAY             							0xAB
#define REGFLAG_END_OF_TABLE      							0xAA   // END OF REGISTERS MARKER

#ifndef FALSE
  #define FALSE (0)
#endif

#ifndef TRUE
  #define TRUE  (1)
#endif

//static unsigned int lcm_esd_test = FALSE;      ///only for ESD test

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
#define read_reg(cmd)											lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size)   				lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)
#define GPIO_LCM_RST_1         (GPIO83 | 0x80000000)
#define GPIO_LCM_RST           (83+343)
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

static struct LCM_setting_table lcm_initialization_setting[] =
{
{0xFF,3,{0x98,0x81,0x03}},

//GIP_1

{0x01, 1, {0x00}},
{0x02, 1, {0x00}},
{0x03, 1, {0x56}},
{0x04, 1, {0x15}},
{0x05, 1, {0x00}},
{0x06, 1, {0x0C}},
{0x07, 1, {0x07}},
{0x08, 1, {0x00}},
{0x09, 1, {0x34}},
{0x0a, 1, {0x01}},
{0x0b, 1, {0x00}},
{0x0c, 1, {0x34}},
{0x0d, 1, {0x00}},
{0x0e, 1, {0x00}},
{0x0f, 1, {0x34}},
{0x10, 1, {0x34}},
{0x11, 1, {0x00}},
{0x12, 1, {0x00}},
{0x13, 1, {0x00}},
{0x14, 1, {0x00}},
{0x15, 1, {0x00}},
{0x16, 1, {0x00}},
{0x17, 1, {0x00}},
{0x18, 1, {0x00}},
{0x19, 1, {0x00}},
{0x1a, 1, {0x00}},
{0x1b, 1, {0x00}},
{0x1c, 1, {0x00}},
{0x1d, 1, {0x00}},
{0x1e, 1, {0x40}},
{0x1f, 1, {0x40}},
{0x20, 1, {0x0A}},
{0x21, 1, {0x0B}},
{0x22, 1, {0x00}},
{0x23, 1, {0x00}},
{0x24, 1, {0x80}},
{0x25, 1, {0x80}},
{0x26, 1, {0x00}},
{0x27, 1, {0x00}},
{0x28, 1, {0x5D}},
{0x29, 1, {0x05}},
{0x2a, 1, {0x00}},
{0x2b, 1, {0x00}},
{0x2c, 1, {0x00}},
{0x2d, 1, {0x00}},
{0x2e, 1, {0x00}},
{0x2f, 1, {0x00}},
{0x30, 1, {0x00}},
{0x31, 1, {0x00}},
{0x32, 1, {0x00}},
{0x33, 1, {0x00}},
{0x34, 1, {0x04}},
{0x35, 1, {0x00}},
{0x36, 1, {0x00}},
{0x37, 1, {0x00}},
{0x38, 1, {0x01}},
{0x39, 1, {0x35}},
{0x3a, 1, {0x00}},
{0x3b, 1, {0x40}},
{0x3c, 1, {0x00}},
{0x3d, 1, {0x00}},
{0x3e, 1, {0x00}},
{0x3f, 1, {0x00}},
{0x40, 1, {0x38}},
{0x41, 1, {0x88}},
{0x42, 1, {0x00}},
{0x43, 1, {0x00}},
{0x44, 1, {0x3f}},   //1F TO 3F_ RESET KEEP LOW ALL GATE ON  
{0x45, 1, {0x20}},     //LVD???ALL GATE ON ?VGH
{0x46, 1, {0x00}},
//GIP_2

  //====================//   
{0x50, 1, {0x01}},
{0x51, 1, {0x23}},
{0x52, 1, {0x45}},
{0x53, 1, {0x67}},
{0x54, 1, {0x89}},
{0x55, 1, {0xab}},
{0x56, 1, {0x01}},
{0x57, 1, {0x23}},
{0x58, 1, {0x45}},
{0x59, 1, {0x67}},
{0x5a, 1, {0x89}},
{0x5b, 1, {0xab}},
{0x5c, 1, {0xcd}},
{0x5d, 1, {0xef}},

{0x5e, 1, {0x11}},
{0x5f, 1, {0x08}},
{0x60, 1, {0x00}},
{0x61, 1, {0x01}},
{0x62, 1, {0x0C}},
{0x63, 1, {0x0F}},
{0x64, 1, {0x0D}},
{0x65, 1, {0x10}},
{0x66, 1, {0x0E}},
{0x67, 1, {0x11}},
{0x68, 1, {0x02}},
{0x69, 1, {0x02}},
{0x6a, 1, {0x02}},
{0x6b, 1, {0x02}},
{0x6c, 1, {0x02}},
{0x6d, 1, {0x02}},
{0x6e, 1, {0x06}},
{0x6f, 1, {0x02}},
{0x70, 1, {0x02}},
{0x71, 1, {0x02}},
{0x72, 1, {0x02}},
{0x73, 1, {0x02}},
{0x74, 1, {0x02}},
{0x75, 1, {0x06}},
{0x76, 1, {0x00}},
{0x77, 1, {0x01}},
{0x78, 1, {0x0C}},
{0x79, 1, {0x0F}},
{0x7a, 1, {0x0D}},
{0x7b, 1, {0x10}},
{0x7c, 1, {0x0E}},
{0x7d, 1, {0x11}},
{0x7e, 1, {0x02}},
{0x7f, 1, {0x02}},
{0x80, 1, {0x02}},
{0x81, 1, {0x02}},
{0x82, 1, {0x02}},
{0x83, 1, {0x02}},
{0x84, 1, {0x08}},
{0x85, 1, {0x02}},
{0x86, 1, {0x02}},
{0x87, 1, {0x02}},
{0x88, 1, {0x02}},
{0x89, 1, {0x02}},
{0x8A, 1, {0x02}},
  //====================//    
{0xFF,3,{0x98,0x81,0x04}},   

{0x00, 1, {0x00}},

{0x68, 1, {0xDB}},     //nonoverlap 18ns (VGH and VGL)
{0x6D, 1, {0x08}},     //gvdd_isc[2:0]=0 (0.2uA) ???VREG1??
{0x70, 1, {0x00}},     //VGH_MOD and VGH_DC CLKDIV disable
{0x71, 1, {0x00}},     //VGL CLKDIV disable
{0x66, 1, {0xFE}},     //VGH 4X
{0x6F, 1, {0x05}},     //GIP EQ_EN 
{0x82, 1, {0x0F}},     //VREF_VGH_MOD_CLPSEL 15V
{0x84, 1, {0x0F}},     //VREF_VGH_CLPSEL 15V
{0x85, 1, {0x0F}},     //VREF_VGL_CLPSEL -10V
{0x32, 1, {0xAC}},     //???channel?power saving
{0x8C, 1, {0x80}},     //sleep out Vcom disable???Vcom source???enable??????
{0x3C, 1, {0xF5}},     //??Sample & Hold Function
{0x3A, 1, {0x24}},     //PS_EN OFF      
{0xB5, 1, {0x02}},     //GAMMA OP 
{0x31, 1, {0x25}},     //SOURCE OP 
{0x88, 1, {0x33}},     //VSP/VSN LVD Disable  
{0x89, 1, {0xBA}},     //VCI LVD ON   

  //====================// 
{0xFF,3,{0x98,0x81,0x01}},   
{0x22, 1, {0x0A}},      
{0x31, 1, {0x00}},          
{0x50, 1, {0x3C}},      //VREG1  4.1v    
{0x51, 1, {0x3B}},      //VREG2 -4.1v      
{0x53, 1, {0x3F}},                  
{0x55, 1, {0x3F}},                    
{0x60, 1, {0x28}},      //SDT=2.5us
{0x61, 1, {0x00}},         
{0x62, 1, {0x0D}},      
{0x63, 1, {0x00}}, 
{0x2E, 1, {0xF0}},      //1440 GATE NL SEL  
{0x2F, 1, {0x00}},      //480 SOURCE


  //====================//   GAMMA Positive
{0xA0, 1, {0x0F}},		//VP255	Gamma P
{0xA1, 1, {0x37}},               //VP251        
{0xA2, 1, {0x47}},               //VP247        
{0xA3, 1, {0x0F}},               //VP243        
{0xA4, 1, {0x12}},               //VP239        
{0xA5, 1, {0x25}},               //VP231        
{0xA6, 1, {0x19}},               //VP219        
{0xA7, 1, {0x1D}},               //VP203        
{0xA8, 1, {0xAA}},               //VP175        
{0xA9, 1, {0x1A}},               //VP144        
{0xAA, 1, {0x28}},               //VP111        
{0xAB, 1, {0x93}},               //VP80         
{0xAC, 1, {0x1D}},               //VP52         
{0xAD, 1, {0x1C}},              //VP36         
{0xAE, 1, {0x50}},               //VP24         
{0xAF, 1, {0x24}},               //VP16         
{0xB0, 1, {0x2A}},               //VP12         
{0xB1, 1, {0x57}},               //VP8          
{0xB2, 1, {0x65}},               //VP4          
{0xB3, 1, {0x39}},               //VP0          



  //====================//  GAMMA Negative
{0xC0, 1, {0x00}},		//VN255 GAMMA N
{0xC1, 1, {0x1D}},               //VN251        
{0xC2, 1, {0x29}},               //VN247        
{0xC3, 1, {0x16}},               //VN243        
{0xC4, 1, {0x19}},               //VN239        
{0xC5, 1, {0x2D}},               //VN231        
{0xC6, 1, {0x1E}},               //VN219        
{0xC7, 1, {0x1E}},               //VN203        
{0xC8, 1, {0x91}},              //VN175        
{0xC9, 1, {0x1C}},               //VN144        
{0xCA, 1, {0x29}},               //VN111        
{0xCB, 1, {0x85}},               //VN80         
{0xCC, 1, {0x1E}},               //VN52         
{0xCD, 1, {0x1D}},               //VN36         
{0xCE, 1, {0x4F}},               //VN24         
{0xCF, 1, {0x25}},               //VN16         
{0xD0, 1, {0x29}},               //VN12         
{0xD1, 1, {0x4E}},               //VN8          
{0xD2, 1, {0x5C}},               //VN4          
{0xD3, 1, {0x39}},               //VN0  


  //====================//      
{0xFF,03,{0x98,0x81,0x00}},    
{0x35,01,{0x00}}, 
{0x11,1,{0x00}},        // Sleep-Out
{REGFLAG_DELAY, 120, {}},
{0x29,1, {0x00}},       // Display On
{REGFLAG_DELAY, 20, {}},
{REGFLAG_END_OF_TABLE, 0x00, {}}

};
static struct LCM_setting_table lcm_sleep_mode_in_setting[] =
{
  // Display off sequence
  {0x28, 0, {0x00}},
  {REGFLAG_DELAY, 120, {}},

  // Sleep Mode On
  {0x10, 0, {0x00}},
  {REGFLAG_DELAY, 200, {}},
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

    //params->dsi.mode   = SYNC_PULSE_VDO_MODE;
    params->dsi.mode   = BURST_VDO_MODE;	

  // DSI
  /* Command mode setting */
  params->dsi.LANE_NUM = LCM_THREE_LANE;
  //The following defined the fomat for data coming from LCD engine.
  params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
  params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
  params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
  params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;


  params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

  params->dsi.vertical_sync_active				= 8;
  params->dsi.vertical_backporch					= 14;
  params->dsi.vertical_frontporch					= 20;
  params->dsi.vertical_active_line				= FRAME_HEIGHT; 

  params->dsi.horizontal_sync_active				= 20;
  params->dsi.horizontal_backporch				= 60;
  params->dsi.horizontal_frontporch				= 60;
  params->dsi.horizontal_active_pixel				= FRAME_WIDTH;


  params->dsi.HS_TRAIL=20; 
  params->dsi.PLL_CLOCK = 360;
}
/*
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);
static int adc_read_vol(void)
{
  int adc[1];
  int data[4] ={0,0,0,0};
  int sum = 0;
  int adc_vol=0;
  int num = 0;

  for(num=0;num<10;num++)
  {
    IMM_GetOneChannelValue(12, data, adc);
    sum+=(data[0]*100+data[1]);
  }
  adc_vol = sum/10;
#if defined(BUILD_LK)
  printf("ili9881c adc_vol is %d\n",adc_vol);
#else
  printk("ili9881c adc_vol is %d\n",adc_vol);
#endif
 return (adc_vol >60) ? 0: 1;
}
*/
static void lcm_init(void)
{
 #ifdef BUILD_LK
  lcm_set_rst_lk(1);
  MDELAY(15);
  lcm_set_rst_lk(0);
  MDELAY(10);
  lcm_set_rst_lk(1);
  MDELAY(120);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(15);
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
  MDELAY(10);
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(120);
#endif

  push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
 #ifdef BUILD_LK

  lcm_set_rst_lk(0);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
#endif
  push_table(lcm_sleep_mode_in_setting, sizeof(lcm_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_resume(void)
{
  lcm_init();
}

static unsigned int lcm_compare_id(void)
{

  int array[4];
  char buffer[3];
//  char id_high=0;
//  char id_low=0;
  int id=0;

  #ifdef BUILD_LK
  lcm_set_rst_lk(1);
  MDELAY(15);
  lcm_set_rst_lk(0);
  MDELAY(10);
  lcm_set_rst_lk(1);
  MDELAY(50);
#else
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(15);
  gpio_set_value_cansleep(GPIO_LCM_RST, 0);
  MDELAY(10);
  gpio_set_value_cansleep(GPIO_LCM_RST, 1);
  MDELAY(50);
#endif

  //{0x39, 0xFF, 5, { 0xFF,0x98,0x06,0x04,0x01}}, // Change to Page 1 CMD
  array[0] = 0x00043902;
  array[1] = 0x068198FF;
  dsi_set_cmdq(array, 2, 1);

  array[0] = 0x00013700;
  dsi_set_cmdq(array, 1, 1);
  read_reg_v2(0xF2, &buffer[0], 1);  //0xOd

  id = buffer[0];
#if defined(BUILD_LK)
  printf("%s, [ili9881d_ivo50_txd_hd]  buffer[0] = [0x%x]  ID = [0x%x]\n",__func__,buffer[0], id);
#else
  printk("%s, [ili9881d_ivo50_txd_hd]  buffer[0] = [0x%x]  ID = [0x%x]\n",__func__,buffer[0], id);
#endif


  return (0x0d == ( id))?1:0;

}

LCM_DRIVER ili9881d_cpt55_haifei_lhd_lcm_drv =
{
  .name			= "ili9881d_cpt55_haifei_lhd",
  .set_util_funcs = lcm_set_util_funcs,
  .get_params     = lcm_get_params,
  .init           = lcm_init,
  .suspend        = lcm_suspend,
  .resume         = lcm_resume,
  .compare_id     = lcm_compare_id,
};
