#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
#include <mt-plat/mtk_boot.h>
#include <mt-plat/upmu_common.h>
#include <mt-plat/charger_type.h>
#include "mtk_charger_intf.h"
#include "bq24158.h"
//#include <mach/mt_gpio.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#define STATUS_OK   0
#define STATUS_UNSUPPORTED  -1
#define HIGH_BATTERY_VOLTAGE_SUPPORT
#define GETARRAYNUM(array) (sizeof(array)/sizeof(array[0]))
static unsigned int charging_error;
static unsigned int charging_get_error_state(void);
#define GPIO_LCM_RST_YT      (25+343) //   (GPIO83 | 0x80000000)
//static DEFINE_MUTEX(bq24158_i2c_access);
const unsigned int VBAT_CV_VTH[] = {
    3856000, 3888000, 3920000, 3952000, 
    3984000, 4016000, 4048000, 4080000, 
    4112000, 4144000, 4176000, 4208000, 
    4240000, 4272000, 4304000, 4336000, 
    4368000, 4400000, 4432000, 4464000,
    4496000, 4528000, 4560000, 4592000,
    4624000,
};

const unsigned int CS_VTH[] = {
    50000, 60000, 120000, 180000,
    240000, 300000, 360000, 420000,
    480000, 540000, 600000, 660000,
    720000, 780000, 840000, 900000,
    960000, 1020000, 1080000, 1140000,
    1200000, 1260000, 1320000, 1380000,
    1440000, 1500000, 1560000, 1620000,
    1680000, 1740000, 1800000, 1860000,
    1920000, 1980000, 2040000, 2100000
};

const unsigned int INPUT_CS_VTH[] = {
    80000, 100000, 200000, 300000,
    400000, 500000, 600000, 700000,
    800000, 900000, 1000000, 1100000,
    1200000, 1300000, 1400000, 1500000,
    1600000, 1700000, 1800000, 1900000,
    2000000, 2100000, 2200000, 2300000,
    2400000, 2500000, 2600000, 2700000,
    2800000, 2900000, 3000000, 3200000
};

const unsigned int VCDT_HV_VTH[] = {
    4200000, 4250000, 4300000, 4350000,
    4400000, 4450000, 4500000, 4550000,
    4600000, 6000000, 6500000, 7000000,
    7500000, 8500000, 9500000, 10500000
};

struct bq24158_info {
    struct device *dev;
    struct i2c_client *i2c;
    struct charger_device *chg_dev;
    const char *chg_dev_name;
};

/* ============================================================ // */
unsigned int charging_value_to_parameter(const unsigned int *parameter, const unsigned int array_size,
                       const unsigned int val)
{
    if (val < array_size) {
        return parameter[val];
    } else {
        pr_debug("Can't find the parameter \r\n");
        return parameter[0];
    }
}

unsigned int charging_parameter_to_value(const unsigned int *parameter, const unsigned int array_size,
                       const unsigned int val)
{
    unsigned int i;

    pr_debug("array_size = %d \r\n", array_size);

    for (i = 0; i < array_size; i++) {
        if (val == *(parameter + i))
            return i;

    }

    pr_debug("NO register value match. val=%d\r\n", val);
    /* TODO: ASSERT(0);      // not find the value */
    return 0;
}

static unsigned int bmt_find_closest_level(const unsigned int *pList, unsigned int number,
                     unsigned int level)
{
    unsigned int i;
    unsigned int max_value_in_last_element;

    if (pList[0] < pList[1])
        max_value_in_last_element = true;
    else
        max_value_in_last_element = false;

    if (max_value_in_last_element == true) {
        for (i = (number - 1); i != 0; i--) {/* max value in the last element */

            if (pList[i] <= level)
                return pList[i];

        }

        pr_debug("Can't find closest level, small value first \r\n");
        return pList[0];
        /* return CHARGE_CURRENT_0_00_MA; */
    } else {
        for (i = 0; i < number; i++) {/* max value in the first element */

            if (pList[i] <= level)
                return pList[i];

        }

        pr_debug("Can't find closest level, large value first \r\n");
        return pList[number - 1];
        /* return CHARGE_CURRENT_0_00_MA; */
    }
}

/**********************************************************
  *
  *   [I2C Slave Setting]
  *
  *********************************************************/
#define BQ24158_SLAVE_ADDR_WRITE   0xD6
#define BQ24158_SLAVE_ADDR_READ    0xD7
#define bq24158_SLAVE_ADDR_WRITE   0xD4
#define bq24158_SLAVE_ADDR_Read    0xD5

static struct i2c_client *new_client;
static const struct i2c_device_id bq24158_i2c_id[] = { {"bq24158", 0}, {} };

kal_bool chargin_hw_init_done = false;
static int bq24158_driver_probe(struct i2c_client *client, const struct i2c_device_id *id);

#ifdef CONFIG_OF
static const struct of_device_id bq24158_of_match[] = {
	{.compatible = "mediatek,sw_charger",},
	{},
};

MODULE_DEVICE_TABLE(of, bq24158_of_match);
#endif

static struct i2c_driver bq24158_driver = {
	.driver = {
		   .name = "bq24158",
#ifdef CONFIG_OF
		   .of_match_table = bq24158_of_match,
#endif
		   },
	.probe = bq24158_driver_probe,
	.id_table = bq24158_i2c_id,
	//.shutdown = bq24158_shutdown,
};

/**********************************************************
  *
  *   [Global Variable]
  *
  *********************************************************/
unsigned char bq24158_reg[bq24158_REG_NUM] = { 0 };

static DEFINE_MUTEX(bq24158_i2c_access);
/**********************************************************
  *
  *   [I2C Function For Read/Write bq24158]
  *
  *********************************************************/
int bq24158_read_byte(unsigned char cmd, unsigned char *returnData)
{
    char     readData = 0;
    int      ret = 0;
    struct i2c_msg msg[2];
    struct i2c_adapter *adap = new_client->adapter;

    mutex_lock(&bq24158_i2c_access);
    msg[0].addr = new_client->addr;
    msg[0].flags = 0;
    msg[0].len = 1;
    msg[0].buf = &cmd;

    msg[1].addr = new_client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].len = 1;
    msg[1].buf = &readData;

    ret = i2c_transfer(adap, msg, 2);
    if (ret < 0) {
        mutex_unlock(&bq24158_i2c_access);
        return 0;
    }
    *returnData = readData;

    mutex_unlock(&bq24158_i2c_access);
    return 1;
}

int bq24158_write_byte(unsigned char cmd, unsigned char writeData)
{
    char write_data[2] = { 0 };
    int ret = 0;
    struct i2c_msg msg;
    struct i2c_adapter *adap = new_client->adapter;

    mutex_lock(&bq24158_i2c_access);
    write_data[0] = cmd;
    write_data[1] = writeData;
    msg.addr = new_client->addr;
    msg.flags = 0;
    msg.len = 2;
    msg.buf = (char *)write_data;

    ret = i2c_transfer(adap, &msg, 1);
    if (ret < 0) {
        mutex_unlock(&bq24158_i2c_access);
        return 0;
    }

    mutex_unlock(&bq24158_i2c_access);
    return 1;
}

/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
int bq24158_read_interface (unsigned char RegNum, unsigned char *val, unsigned char MASK, unsigned char SHIFT)
{
	unsigned char bq24158_reg = 0;
	int ret = 0;


	ret = bq24158_read_byte(RegNum, &bq24158_reg);
	//pr_debug("[bq24158_read_interface] Reg[%x]=0x%x\n", RegNum, bq24158_reg);

	bq24158_reg &= (MASK << SHIFT);
	*val = (bq24158_reg >> SHIFT);
	//pr_debug("[bq24158_read_interface] Val=0x%x\n", *val);

	return ret;
}

int bq24158_config_interface (unsigned char RegNum, unsigned char val, unsigned char MASK, unsigned char SHIFT)
{
	unsigned char bq24158_reg = 0;
	int ret = 0;

	//pr_debug("--------------------------------------------------\n");

	ret = bq24158_read_byte(RegNum, &bq24158_reg);
	//pr_debug("[bq24158_config_interface] Reg[%x]=0x%x\n", RegNum, bq24158_reg);

	bq24158_reg &= ~(MASK << SHIFT);
	bq24158_reg |= (val << SHIFT);

	ret = bq24158_write_byte(RegNum, bq24158_reg);
	//pr_debug("[bq24158_config_interface] Write Reg[%x]=0x%x\n", RegNum, bq24158_reg);

	/* Check */
	 bq24158_read_byte(RegNum, &bq24158_reg); 
	 //pr_debug("[bq24158_config_interface] Check Reg[%x]=0x%x\n", RegNum, bq24158_reg); 

	return ret;
}

/* write one register directly */
int bq24158_config_interface_liao (unsigned char RegNum, unsigned char val)
{
	unsigned char ret = 0;

	ret = bq24158_write_byte(RegNum, val);

	return ret;
}

/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
/* CON0---------------------------------------------------- */


void bq24158_set_tmr_rst(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON0), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON0_TMR_RST_MASK),
                                    (unsigned char)(CON0_TMR_RST_SHIFT)
                                    );
}

 unsigned int bq24158_get_otg_status(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_OTG_MASK),
                                    (unsigned char)(CON0_OTG_SHIFT)
                                    );
    return val;
}

void bq24158_set_en_stat(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON0), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON0_EN_STAT_MASK),
                                    (unsigned char)(CON0_EN_STAT_SHIFT)
                                    );
}

unsigned int bq24158_get_chip_status(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_STAT_MASK),
                                    (unsigned char)(CON0_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24158_get_boost_status(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_BOOST_MASK),
                                    (unsigned char)(CON0_BOOST_SHIFT)
                                    );
    return val;
}

unsigned int bq24158_get_fault_status(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_FAULT_MASK),
                                    (unsigned char)(CON0_FAULT_SHIFT)
                                    );
    return val;
}

//CON1----------------------------------------------------

void bq24158_set_input_charging_current(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_LIN_LIMIT_MASK),
                                    (unsigned char)(CON1_LIN_LIMIT_SHIFT)
                                    );
}

void bq24158_set_v_low(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_LOW_V_MASK),
                                    (unsigned char)(CON1_LOW_V_SHIFT)
                                    );
}

void bq24158_set_te(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_TE_MASK),
                                    (unsigned char)(CON1_TE_SHIFT)
                                    );
}

void bq24158_set_ce(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_CE_MASK),
                                    (unsigned char)(CON1_CE_SHIFT)
                                    );
}

void bq24158_set_hz_mode(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_HZ_MODE_MASK),
                                    (unsigned char)(CON1_HZ_MODE_SHIFT)
                                    );
}

void bq24158_set_opa_mode(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_OPA_MODE_MASK),
                                    (unsigned char)(CON1_OPA_MODE_SHIFT)
                                    );
}

//CON2----------------------------------------------------

void bq24158_set_oreg(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OREG_MASK),
                                    (unsigned char)(CON2_OREG_SHIFT)
                                    );
}

void bq24158_set_otg_pl(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OTG_PL_MASK),
                                    (unsigned char)(CON2_OTG_PL_SHIFT)
                                    );
}

void bq24158_set_otg_en(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OTG_EN_MASK),
                                    (unsigned char)(CON2_OTG_EN_SHIFT)
                                    );
}

//CON3----------------------------------------------------

unsigned int bq24158_get_vender_code(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_VENDER_CODE_MASK),
                                    (unsigned char)(CON3_VENDER_CODE_SHIFT)
                                    );
    return val;
}

unsigned int bq24158_get_pn(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_PIN_MASK),
                                    (unsigned char)(CON3_PIN_SHIFT)
                                    );
    return val;
}

unsigned int bq24158_get_revision(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_REVISION_MASK),
                                    (unsigned char)(CON3_REVISION_SHIFT)
                                    );
    return val;
}

//CON4----------------------------------------------------

void bq24158_set_reset(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_RESET_MASK),
                                    (unsigned char)(CON4_RESET_SHIFT)
                                    );
}

void bq24158_set_iocharge(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_I_CHR_MASK),
                                    (unsigned char)(CON4_I_CHR_SHIFT)
                                    );
}

void bq24158_set_iterm(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_I_TERM_MASK),
                                    (unsigned char)(CON4_I_TERM_SHIFT)
                                    );
}

//CON5----------------------------------------------------

void bq24158_set_dis_vreg(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_DIS_VREG_MASK),
                                    (unsigned char)(CON5_DIS_VREG_SHIFT)
                                    );
}

void bq24158_set_io_level(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_IO_LEVEL_MASK),
                                    (unsigned char)(CON5_IO_LEVEL_SHIFT)
                                    );
}

unsigned int bq24158_get_sp_status(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON5), 
                                    (&val),
                                    (unsigned char)(CON5_SP_STATUS_MASK),
                                    (unsigned char)(CON5_SP_STATUS_SHIFT)
                                    );
    return val;
}

unsigned int bq24158_get_en_level(void)
{
    int ret=0;
    unsigned char val=0;

    ret=bq24158_read_interface(     (unsigned char)(bq24158_CON5), 
                                    (&val),
                                    (unsigned char)(CON5_EN_LEVEL_MASK),
                                    (unsigned char)(CON5_EN_LEVEL_SHIFT)
                                    );
    return val;
}

void bq24158_set_vsp(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_VSP_MASK),
                                    (unsigned char)(CON5_VSP_SHIFT)
                                    );
}

//CON6----------------------------------------------------

void bq24158_set_i_safe(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_ISAFE_MASK),
                                    (unsigned char)(CON6_ISAFE_SHIFT)
                                    );
}

void bq24158_set_v_safe(unsigned int val)
{
    int ret=0;    

    ret=bq24158_config_interface(   (unsigned char)(bq24158_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_VSAFE_MASK),
                                    (unsigned char)(CON6_VSAFE_SHIFT)
                                    );
}
/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
int  bq24158_dump_register(struct charger_device *chg_dev)
{
	unsigned char i = 0;
	printk("[bq24158] ");
	for (i = 0; i < bq24158_REG_NUM; i++) {
		bq24158_read_byte(i, &bq24158_reg[i]);
		printk("[0x%x]=0x%x ", i, bq24158_reg[i]);
	}
	pr_debug("\n");

    return 0;
}
#if 0
int bq24158_write_byte(unsigned char cmd, unsigned char writeData)
{
    char    write_data[2] = {0};
    int     ret=0;

    if (new_client == NULL) {
        return -1;
    }

    mutex_lock(&bq24158_i2c_access);
    write_data[0] = cmd;
    write_data[1] = writeData;

    //new_client->ext_flag=((new_client->ext_flag ) & I2C_MASK_FLAG ) | I2C_DIRECTION_FLAG;

    ret = i2c_master_send(new_client, write_data, 2);
#if 0   
 if (ret < 0)
    {

        new_client->ext_flag=0;
        mutex_unlock(&bq24158_i2c_access);
        return 0;
    }

    new_client->ext_flag=0;
#endif
    mutex_unlock(&bq24158_i2c_access);
    return 1;
}
int bq24158_config_interface_liao (unsigned char RegNum, unsigned char val)
{
    int ret = 0;
ret = bq24158_write_byte(RegNum, val);
    //ret = bq24158_write_byte(RegNum, val);

    return ret;
}
#endif

static unsigned int bq24158_charging_hw_init(struct charger_device *chg_dev)
{
	unsigned int status = STATUS_OK;
	static bool charging_init_flag = KAL_FALSE;
#if 0	
	mt_set_gpio_mode(gpio_number,gpio_on_mode);  
	mt_set_gpio_dir(gpio_number,gpio_on_dir);
	mt_set_gpio_out(gpio_number,gpio_on_out);
#endif
#if defined(MTK_WIRELESS_CHARGER_SUPPORT)
	mt_set_gpio_mode(wireless_charger_gpio_number,0); // 0:GPIO mode
	mt_set_gpio_dir(wireless_charger_gpio_number,0); // 0: input, 1: output
#endif
	//battery_xlog_printk(BAT_LOG_FULL, "gpio_number=0x%x,gpio_on_mode=%d,gpio_off_mode=%d\n", gpio_number, gpio_on_mode, gpio_off_mode);
	
    pmic_set_register_value(PMIC_RG_USBDL_RST,1);//force leave USBDL mode
#if defined(HIGH_BATTERY_VOLTAGE_SUPPORT)
	bq24158_config_interface_liao(0x06,0x77); // ISAFE = 1250mA, VSAFE = 4.34V
#else
	bq24158_config_interface_liao(0x06,0x70);
#endif
	    
	bq24158_config_interface_liao(0x00,0xC0);	//kick chip watch dog
	bq24158_config_interface_liao(0x01,0xd8);//0xb8);	//TE=1, CE=0, HZ_MODE=0, OPA_MODE=0
	bq24158_config_interface_liao(0x05,0x03);
	bq24158_config_interface_liao(0x02,0xaa);
    
	bq24158_config_interface_liao(0x04,0x1A); //146mA
	if ( !charging_init_flag ) {   
		bq24158_config_interface_liao(0x04,0x1A); //146mA
		charging_init_flag = KAL_TRUE;
	}        
 	return status;
}

static int bq24158_charging_enable(struct charger_device *chg_dev, bool en)
{
 	unsigned int status = 0;


	if(en) {
		bq24158_set_ce(0);
		bq24158_set_hz_mode(0);
		bq24158_set_opa_mode(0);
	} else {

#if defined(CONFIG_USB_MTK_HDRC_HCD)
   		if(mt_usb_is_device())
#endif 			
		{
#if 0
			mt_set_gpio_mode(gpio_number,gpio_off_mode);  
			mt_set_gpio_dir(gpio_number,gpio_off_dir);
			mt_set_gpio_out(gpio_number,gpio_off_out);
#endif

			// bq24158_set_ce(1);
			if (charging_get_error_state()) {
				bq24158_set_ce(0x1);
				printk("[charging_enable] bq24158_set_hz_mode(0x1)\n");
				bq24158_set_hz_mode(0x1);	// disable power path
			}
		}
	}
	return status;
}

static int bq24158_charging_get_current(struct charger_device *chg_dev, u32 *ichg)
{
    unsigned int status = 0;
    /* unsigned int array_size; */
    /* unsigned char reg_value; */
    unsigned char ret_val = 0;

    /* Get current level */
    bq24158_read_interface(0x1, &ret_val, 0x3, 0x6);  //IINLIM

    /* Parsing */
    ret_val = (ret_val * 60) ;

        /* Get current level */
        /* array_size = GETARRAYNUM(CS_VTH); */
        /* *(unsigned int *)data = charging_value_to_parameter(CS_VTH,array_size,reg_value); */
    *ichg = ret_val;

  return status;

}

static int bq24158_charging_set_current(struct charger_device *chg_dev, u32 current_value)
{
     unsigned int status = 0;
    unsigned int set_chr_current;
    unsigned int array_size;
    unsigned int register_value;

	if(current_value <= 35000)//CHARGE_CURRENT_350_00_MA)
	{
		bq24158_set_io_level(1);
	}
	else
	{
		bq24158_set_io_level(0);
    array_size = GETARRAYNUM(CS_VTH);
    set_chr_current = bmt_find_closest_level(CS_VTH, array_size, current_value);
    register_value = charging_parameter_to_value(CS_VTH, array_size, set_chr_current);

//	bq24158_config_interface_liao(0x04,0x1A); //146mA
		bq24158_set_iocharge(register_value);
	}
        return status;
}

static int bq24158_charging_set_input_current(struct charger_device *chg_dev, u32 current_value)
{
	unsigned int status = 0;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned int register_value;
    if(current_value > 50000)//CHARGE_CURRENT_500_00_MA)
    {
        register_value = 0x3;
    }
    else
    {
	array_size = GETARRAYNUM(INPUT_CS_VTH);
	set_chr_current = bmt_find_closest_level(INPUT_CS_VTH, array_size, current_value);
	register_value = charging_parameter_to_value(INPUT_CS_VTH, array_size, set_chr_current);
    }
	 //bq24158_set_input_charging_current(register_value);
	// bq24158_set_input_charging_current(0xd8);
//	bq24158_config_interface_liao(0x01,0xd8);//0xb8);	//TE=1, CE=0, HZ_MODE=0, OPA_MODE=0
    bq24158_set_input_charging_current(register_value);
	pr_debug("yuting bq24158_charging_set_cc_input_current set_cc_current=%d\n", register_value);

        return status;

}

static int bq24158_charging_set_cv_voltage(struct charger_device *chg_dev, u32 cv)
{
    unsigned int status = 0;
    unsigned short register_value;
    //unsigned int cv_value = *(unsigned int *)(data);
   // unsigned int set_cv_voltage;
    unsigned int cv_value = cv;
    unsigned int array_size;

    array_size = GETARRAYNUM(VBAT_CV_VTH);
  //  set_cv_voltage = bmt_find_closest_level(VBAT_CV_VTH, array_size, cv_value);
    register_value = charging_parameter_to_value(VBAT_CV_VTH, array_size, cv_value);//set_cv_voltage);

  //  bq24158_set_oreg(register_value);
   // bq24158_config_interface_liao(0x02,0xaa);
	return status;
}

static int bq24158_charging_reset_watch_dog_timer(struct charger_device *chg_dev)
{
	unsigned int status = 0;

	pr_debug("bq24158_charging_reset_watch_dog_timer\r\n");

     bq24158_set_tmr_rst(1);

	return status;
}

static int bq24158_charging_get_charging_status(struct charger_device *chg_dev, bool *is_done)
{
	unsigned int status = 0;
    unsigned int ret_val;

    ret_val = bq24158_get_chip_status();

    if(ret_val == 0x2){
        *is_done = true;
    }else{
        *is_done = false;
    }
	return status;
}

static int bq24158_enable_otg(struct charger_device *chg_dev, bool en)
{
	unsigned int status = 0;

	pr_debug("bq24158_enable_otg\r\n");

//	bq24158_set_otg_config(en);
    if(en){
        bq24158_set_opa_mode(1);
        bq24158_set_otg_pl(1);
        bq24158_set_otg_en(1);

    }else{
        bq24158_config_interface_liao(0x01,0x30);
        bq24158_config_interface_liao(0x02,0x8e);

    }
	return status;
}

#if 0
static unsigned int bq24158_set_boost_current_limit(struct charger_device *chg_dev, u32 current_limit)
{
	unsigned int status = 0;

	pr_debug("bq24158_set_boost_current_limit\r\n");

	bq24158_set_boost_lim(current_limit);

	return status;
}
#endif

static int bq24158_set_pep_current_pattern(struct charger_device *chg_dev, bool is_increase)
{
# if 0
	bool charging_status = false;

#if defined(HIGH_BATTERY_VOLTAGE_SUPPORT)
    unsigned int cv_voltage = 4400000;
#else
	unsigned int cv_voltage = 4200000;
#endif

	bq24158_charging_get_charging_status(chg_dev, &charging_status);
	if (false == charging_status) {
		bq24158_charging_set_cv_voltage(chg_dev, cv_voltage);	/* Set CV */
		bq24158_set_ichg(0x0);	/* Set charging current 500ma */
		bq24158_set_chg_config(0x1);	/* Enable Charging */
	}

	if (is_increase) {
		bq24158_set_iinlim(0x0);	/* 100mA */
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 1");
		msleep(85);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 1");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 2");
		msleep(85);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 2");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 3");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 3");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 4");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 4");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 5");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 5");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_increase() on 6");
		msleep(485);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_increase() off 6");
		msleep(50);

		pr_debug("mtk_ta_increase() end\n");

		bq24158_set_iinlim(0x2);	/* 500mA */
		msleep(200);
	} else {
		bq24158_set_iinlim(0x0);	/* 100mA */
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 1");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 1");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 2");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 2");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 3");
		msleep(281);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 3");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 4");
		msleep(85);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 4");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 5");
		msleep(85);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 5");
		msleep(85);

		bq24158_set_iinlim(0x2);	/* 500mA */
		pr_debug("mtk_ta_decrease() on 6");
		msleep(485);

		bq24158_set_iinlim(0x0);	/* 100mA */
		pr_debug("mtk_ta_decrease() off 6");
		msleep(50);

		pr_debug("mtk_ta_decrease() end\n");

		bq24158_set_iinlim(0x2);	/* 500mA */
	}
#endif
	return 0;
}

static int bq24158_do_event(struct charger_device *chg_dev, unsigned int event, unsigned int args)
{
    if (chg_dev == NULL)
        return -EINVAL;

    pr_info("%s: event = %d\n", __func__, event);

    switch (event) {
    case EVENT_EOC:
        charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
        break;
    case EVENT_RECHARGE:
        charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
        break;
    default:
        break;
    }

    return 0;
}

static struct charger_ops bq24158_chg_ops = {
    /* Normal charging */
    .dump_registers = bq24158_dump_register,
    .enable = bq24158_charging_enable,
    .get_charging_current = bq24158_charging_get_current,
    .set_charging_current = bq24158_charging_set_current,
    .set_input_current = bq24158_charging_set_input_current,
    //.get_constant_voltage = bq24158_get_cv,
    .set_constant_voltage = bq24158_charging_set_cv_voltage,
    .kick_wdt = bq24158_charging_reset_watch_dog_timer,
    //.set_mivr = bq24158_set_mivr,
    .is_charging_done = bq24158_charging_get_charging_status,
    //.get_min_charging_current = bq24158_get_min_ichg,
    //.set_eoc_current = bq24158_set_ieoc,
    //.enable_termination = bq24158_enable_te,
    //.run_aicl = bq24158_run_aicl,
    //.reset_eoc_state = bq24158_reset_eoc_state,

    /* Safety timer */
    //.enable_safety_timer = bq24158_enable_safety_timer,
    //.is_safety_timer_enabled = bq24158_is_safety_timer_enable,

    /* Power path */
    //.enable_powerpath = bq24158_enable_power_path,
    //.is_powerpath_enabled = bq24158_is_power_path_enable,

    /* OTG */
    .enable_otg = bq24158_enable_otg,
    //.set_boost_current_limit = bq24158_set_boost_current_limit,
    //.enable_discharge = bq24158_enable_discharge,

    /* PE+/PE+20 */
    .send_ta_current_pattern = bq24158_set_pep_current_pattern,
    //.set_pe20_efficiency_table = bq24158_set_pep20_efficiency_table,
    //.send_ta20_current_pattern = bq24158_set_pep20_current_pattern,
    //.set_ta20_reset = bq24158_set_pep20_reset,
    //.enable_cable_drop_comp = bq24158_enable_cable_drop_comp,

    /* ADC */
    //.get_tchg_adc = bq24158_get_tchg,

    /* Event */
    .event = bq24158_do_event,
};

static const struct charger_properties bq24158_chg_props = {
    .alias_name = "bq24158",
};

static int bq24158_parse_dt(struct bq24158_info *info, struct device *dev)
{
    struct device_node *np = dev->of_node;

    pr_info("%s\n", __func__);

    if (!np) {
        pr_err("%s: no of node\n", __func__);
        return -ENODEV;
    }

    if (of_property_read_string(np, "charger_name", &info->chg_dev_name) < 0) {
        info->chg_dev_name = "primary_chg";
        pr_warn("%s: no charger name\n", __func__);
    }

    return 0;
}


static int bq24158_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
    struct bq24158_info *info = NULL;

	//pr_debug("[bq24158_driver_probe]\n");
    info = devm_kzalloc(&client->dev, sizeof(struct bq24158_info), GFP_KERNEL);
    if (!info)
        return -ENOMEM;
    new_client = client;
    info->dev = &client->dev;
    ret = bq24158_parse_dt(info, &client->dev);
    if (ret < 0)
        return ret;

    /* Register charger device */
    info->chg_dev = charger_device_register(info->chg_dev_name,
        &client->dev, info, &bq24158_chg_ops, &bq24158_chg_props);

    if (IS_ERR_OR_NULL(info->chg_dev)) {
        pr_err("%s: register charger device failed\n", __func__);
        ret = PTR_ERR(info->chg_dev);
        return ret;
    }

    bq24158_charging_hw_init(info->chg_dev);
    //---------------------
  //  bq24158_hw_init();
#if defined(HIGH_BATTERY_VOLTAGE_SUPPORT)
    bq24158_config_interface_liao(0x06,0x77);
#else
    bq24158_config_interface_liao(0x06,0x70);
#endif

	bq24158_dump_register(info->chg_dev);
	chargin_hw_init_done = true;

	return 0;
}
static unsigned int charging_get_error_state(void)
{
	return charging_error;
}
/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
unsigned char g_reg_value_bq24158 = 0;
static ssize_t show_bq24158_access(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_debug("[show_bq24158_access] 0x%x\n", g_reg_value_bq24158);
	return sprintf(buf, "%u\n", g_reg_value_bq24158);
}

static ssize_t store_bq24158_access(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret = 0;
	char *pvalue = NULL;
	unsigned int reg_value = 0;
	unsigned int reg_address = 0;

	pr_debug("[store_bq24158_access]\n");

	if (buf != NULL && size != 0) {
		pr_debug("[store_bq24158_access] buf is %s and size is %zu\n", buf, size);
		reg_address = simple_strtoul(buf, &pvalue, 16);

		if (size > 3) {
			reg_value = simple_strtoul((pvalue + 1), NULL, 16);
			pr_debug
			    ("[store_bq24158_access] write bq24158 reg 0x%x with value 0x%x !\n",
			     reg_address, reg_value);
			ret = bq24158_config_interface(reg_address, reg_value, 0xFF, 0x0);
		} else {
			ret = bq24158_read_interface(reg_address, &g_reg_value_bq24158, 0xFF, 0x0);
			pr_debug("[store_bq24158_access] read bq24158 reg 0x%x with value 0x%x !\n",
				 reg_address, g_reg_value_bq24158);
			pr_debug
			    ("[store_bq24158_access] Please use \"cat bq24158_access\" to get value\r\n");
		}
	}
	return size;
}

static DEVICE_ATTR(bq24158_access, 0664, show_bq24158_access, store_bq24158_access);	/* 664 */

static int bq24158_user_space_probe(struct platform_device *dev)
{
	int ret_device_file = 0;

	pr_debug("******** bq24158_user_space_probe!! ********\n");

	ret_device_file = device_create_file(&(dev->dev), &dev_attr_bq24158_access);

	return 0;
}

struct platform_device bq24158_user_space_device = {
	.name = "bq24158-user",
	.id = -1,
};

static struct platform_driver bq24158_user_space_driver = {
	.probe = bq24158_user_space_probe,
	.driver = {
		   .name = "bq24158-user",
		   },
};


static int __init bq24158_subsys_init(void)
{
	int ret = 0;



	if (i2c_add_driver(&bq24158_driver) != 0)
		pr_debug("[bq24158_init] failed to register bq24158 i2c driver.\n");
	else
		pr_debug("[bq24158_init] Success to register bq24158 i2c driver.\n");


	/* bq24158 user space access interface */
	ret = platform_device_register(&bq24158_user_space_device);
	if (ret) {
		pr_debug("****[bq24158_init] Unable to device register(%d)\n", ret);
		return ret;
	}
	ret = platform_driver_register(&bq24158_user_space_driver);
	if (ret) {
		pr_debug("****[bq24158_init] Unable to register driver (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void __exit bq24158_exit(void)
{
	i2c_del_driver(&bq24158_driver);
}

/* module_init(bq24158_init); */
/* module_exit(bq24158_exit); */
subsys_initcall(bq24158_subsys_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C bq24158 Driver");
MODULE_AUTHOR("YT Lee<yt.lee@mediatek.com>");
