/**************************************************************************
*  AW2013_LED.c
* 
*  Create Date : 
* 
*  Modify Date : 
*
*  Create by   : AWINIC Technology CO., LTD
*
*  Version     : 1.0.0 , 2016/02/15
**************************************************************************/

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AW2013 I2C
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define AW2013_I2C_NAME		"AW2013_LED" 
#define AW2013_I2C_BUS		0
#define AW2013_I2C_ADDR		0x45

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
static ssize_t AW2013_get_reg(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW2013_set_reg(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);

static DEVICE_ATTR(reg, 0660, AW2013_get_reg,  AW2013_set_reg);

struct i2c_client *AW2013_i2c_client;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AW2013 Config
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define Imax          0x02   //LED Imax,0x00=omA,0x01=5mA,0x02=10mA,0x03=15mA,
#define Rise_time   0x02   //LED rise time,0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Hold_time   0x01   //LED max light time light 0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s
#define Fall_time     0x02   //LED fall time,0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Off_time      0x01   //LED off time ,0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Delay_time   0x00   //LED Delay time ,0x00=0s,0x01=0.13s,0x02=0.26s,0x03=0.52s,0x04=1.04s,0x05=2.08s,0x06=4.16s,0x07=8.32s,0x08=16.64s
#define Period_Num  0x00   //LED breath period number,0x00=forever,0x01=1,0x02=2.....0x0f=15

void aw2013_breath_all(int led0,int led1,int led2) ; //led on=0x01   ledoff=0x00
void aw2013_led_on(int led0, int led1, int led2);
void aw2013_led_off(void);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// i2c write and read
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned char I2C_write_reg(unsigned char addr, unsigned char reg_data)
{
	char ret;
	u8 wdbuf[512] = {0};

	struct i2c_msg msgs[] = {
		{
			.addr	= AW2013_i2c_client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= wdbuf,
		},
	};

	wdbuf[0] = addr;
	wdbuf[1] = reg_data;

	ret = i2c_transfer(AW2013_i2c_client->adapter, msgs, 1);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

    return ret;
}

unsigned char I2C_read_reg(unsigned char addr)
{
	unsigned char ret;
	u8 rdbuf[512] = {0};

	struct i2c_msg msgs[] = {
		{
			.addr	= AW2013_i2c_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rdbuf,
		},
		{
			.addr	= AW2013_i2c_client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= rdbuf,
		},
	};

	rdbuf[0] = addr;
	
	ret = i2c_transfer(AW2013_i2c_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

    return rdbuf[0];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AW2013 LED
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void aw2013_breath_all(int led0,int led1,int led2)  //led on=0x01   ledoff=0x00
{  
	I2C_write_reg(0x01, 0x01);				// enable LED 		

	I2C_write_reg(0x31, Imax|0x70);				//config mode, IMAX = 5mA	
	I2C_write_reg(0x32, Imax|0x70);				//config mode, IMAX = 5mA	
	I2C_write_reg(0x33, Imax|0x70);				//config mode, IMAX = 5mA	

	I2C_write_reg(0x34, 0xff);				// LED0 level,
	I2C_write_reg(0x35, 0xff);				// LED1 level,
	I2C_write_reg(0x36, 0xff);				// LED2 level,
											
	I2C_write_reg(0x37, Rise_time<<4 | Hold_time);		//led0  				
	I2C_write_reg(0x38, Fall_time<<4 | Off_time);		//led0 
	I2C_write_reg(0x39, Delay_time<<4| Period_Num);		//led0 

	I2C_write_reg(0x3a, Rise_time<<4 | Hold_time);		//led1						
	I2C_write_reg(0x3b, Fall_time<<4 | Off_time);		//led1 
	I2C_write_reg(0x3c, Delay_time<<4| Period_Num);		//led1  

	I2C_write_reg(0x3d, Rise_time<<4 | Hold_time);		//led2 			
	I2C_write_reg(0x3e, Fall_time<<4 | Off_time);		//led2 
	I2C_write_reg(0x3f, Delay_time<<4| Period_Num);		//led2

	I2C_write_reg(0x30, led2<<2|led1<<1|led0);		//led on=0x01 ledoff=0x00	
}

void aw2013_led_on(int led0, int led1, int led2)
{
	I2C_write_reg(0x01, 0x01);				// enable LED 		

	I2C_write_reg(0x31, Imax);				//config mode, IMAX = 5mA	
	I2C_write_reg(0x32, Imax);				//config mode, IMAX = 5mA	
	I2C_write_reg(0x33, Imax);				//config mode, IMAX = 5mA	

	I2C_write_reg(0x34, 0xff);				// LED0 level,
	I2C_write_reg(0x35, 0xff);				// LED1 level,
	I2C_write_reg(0x36, 0xff);				// LED2 level,

	I2C_write_reg(0x30, led2<<2|led1<<1|led0);		//led on=0x01 ledoff=0x00	
}

void aw2013_led_off(void)
{
	I2C_write_reg(0x01, 0x00);				// disable LED 		
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Debug
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static ssize_t AW2013_get_reg(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned char reg_val;
	ssize_t len = 0;
	u8 i;
	for(i=0;i<0x3A;i++)
	{
		reg_val = I2C_read_reg(i);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg%2X = 0x%2X, ", i,reg_val);
	}

	return len;
}

static ssize_t AW2013_set_reg(struct device* cd, struct device_attribute *attr, const char* buf, size_t len)
{
	unsigned int databuf[2];
	if(2 == sscanf(buf,"%x %x",&databuf[0], &databuf[1]))
	{
		I2C_write_reg(databuf[0],databuf[1]);
	}
	return len;
}



static int AW2013_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	err = device_create_file(dev, &dev_attr_reg);

	return err;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int AW2013_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	unsigned char reg_value;
	unsigned char cnt = 5;
	int err = 0;
	printk("%s start\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	AW2013_i2c_client = client;

	while(cnt>0)
	{
		reg_value = I2C_read_reg(0x00);
		printk("AW2013 CHIPID=0x%2x\n", reg_value);
		if(reg_value == 0x33)
		{
			break;
		}
		cnt --;
		msleep(10);
	}
	if(!cnt)
	{
		err = -ENODEV;
		goto exit_create_singlethread;
	}

	AW2013_create_sysfs(client);	
	
	return 0;

exit_create_singlethread:
	AW2013_i2c_client = NULL;
exit_check_functionality_failed:
	return err;	
}

static int AW2013_i2c_remove(struct i2c_client *client)
{
	AW2013_i2c_client = NULL;
	return 0;
}

static const struct i2c_device_id AW2013_i2c_id[] = {
	{ AW2013_I2C_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id extled_of_match[] = {
	{.compatible = "mediatek,breathlight"},
	{},
};
#endif

static struct i2c_driver AW2013_i2c_driver = {
        .driver = {
                .name   = AW2013_I2C_NAME,
                .owner   = THIS_MODULE,
#ifdef CONFIG_OF
				.of_match_table = extled_of_match,
#endif
},

        .probe          = AW2013_i2c_probe,
        .remove         = AW2013_i2c_remove,
        .id_table       = AW2013_i2c_id,
};

static int __init AW2013_led_init(void) {
	int ret;
	printk("%s start\n", __func__);
	
	ret = i2c_add_driver(&AW2013_i2c_driver);
	if (ret != 0) {
		printk("[%s] failed to register AW2013 i2c driver.\n", __func__);
		return ret;
	} else {
		printk("[%s] Success to register AW2013 i2c driver.\n", __func__);
	}
	return 0;
}

static void __exit AW2013_led_exit(void) {
	printk("%s exit\n", __func__);
	i2c_del_driver(&AW2013_i2c_driver);
}

module_init(AW2013_led_init);
module_exit(AW2013_led_exit);

MODULE_AUTHOR("<liweilei@awinic.com.cn>");
MODULE_DESCRIPTION("AWINIC AW2013 LED Driver");
MODULE_LICENSE("GPL");

