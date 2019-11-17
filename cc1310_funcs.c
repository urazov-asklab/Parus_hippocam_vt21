//cc1310_funcs.c

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "cc1310_funcs.h"
#include "cc1310_ioctl.h"
#include "gpio_func.h"
#include "ask_rf3.h"

int cc1310_dev = -1;
u8	ccbuf[256];

int rf_rst_pin;
int rf_oe_pin;
int rf_srdy_pin;
int rf_mrdy_pin;


int cc1310_write_reg(u8 reg, u8 val)
{
	int 	status;
	struct 	cc1310_ioctl_p iop;

	ccbuf[0] = CC_WRITE | (reg & 0x3F);
	ccbuf[1] = val;
	iop.buf  = ccbuf;
	iop.len  = 2;
	status 	 = ioctl(cc1310_dev, CC1310_IOCTL_WRITE, (void*)&iop);

	return status;
}

int cc1310_read_reg(u8 reg, u8 *val)
{
	int 	status, i;
	u32 	srdy = 1;
	struct 	cc1310_ioctl_p iop;

	ccbuf[0] = CC_READ | (reg & 0x3F);
	iop.buf  = ccbuf;
	iop.len  = 1;
	status 	 = ioctl(cc1310_dev, CC1310_IOCTL_WRITE, (void*)&iop);

	for(i=0; i<=10; i++)
	{
		gpio_get_value(rf_srdy_pin, &srdy);
		if(srdy == 0)
			break;
		usleep(100);
	}
	if(i == 10)
		return FAILURE;

	iop.buf = ccbuf;
	iop.len = 2;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_READ, (void*)&iop);
	*val 	= ccbuf[1];//ccbuf[0] is dummy byte(see cc1310 errata - spi);

	return status;
}

// int cc1310_write_burst(u8 reg, u8 len, u8*buf)
// {
//     int 	status;
// 	struct 	cc1310_ioctl_p iop;

// 	iop.reg = reg;
// 	iop.buf = buf;
// 	iop.len = len;
// 	status 	= ioctl(cc1310_dev, CC1310_IOCTL_WRITE_BURST, (void*)&iop);

// 	return status;
// }

// int cc1310_read_burst(u8 reg, u8 len, u8*buf)
// {
//     int 	status;
// 	struct 	cc1310_ioctl_p iop;

// 	iop.reg = reg;
// 	iop.buf = buf;	
// 	iop.len = len;
// 	status 	= ioctl(cc1310_dev, CC1310_IOCTL_READ_BURST, (void*)&iop);

// 	return status;
// }

int cc1310_get_status(u8*sta)
{
    int 	status;
	struct 	cc1310_ioctl_p iop;

	iop.buf = ccbuf;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_GET_STATUS, (void*)&iop);

	if(!status)
	{
		*sta = ccbuf[1];//ccbuf[0] is dummy byte(see cc1310 errata - spi);
	}
	return status;
}

int	cc1310_init(char *dev)
{
	u8 	sta 	= 0;
	int ret 	= 0;
	int i 		= 0;

	memset(&ccbuf[0], 0, 256);

	cc1310_dev 	= open(dev, O_RDWR);
	if(cc1310_dev < 0)
	{
		ERR("Failed to open device: %s\r\n", dev);
		return FAILURE;
	}
	debug("cc1310 dev opened\n");
	
	while(1)
	{
		ret = cc1310_get_status(&sta);
		i++;
		debug("cc1310 stat: 0x%.2x\n", sta);

		if(ret || (sta & RF_STAT_READY))
		{
			if(i > 10)
			{
				ERR("Failed to get status of device:%i\r\n", sta);
				return FAILURE;
			}
		}
		else
		{
			break;
		}
		usleep(10000);
	}

	return SUCCESS;
}

void cc1310_free()
{
	close(cc1310_dev);
}

void cc1310_setup_connection()
{
    rf_rst_pin  = RF_RST_VAL;
    rf_oe_pin   = RF_OE_VAL;
	rf_srdy_pin	= RF_SRDY_VAL;
	rf_mrdy_pin = RF_MRDY_VAL;
 
	gpio_export(rf_mrdy_pin);
	gpio_set_dir(rf_mrdy_pin, 1);
	gpio_set_value(rf_mrdy_pin, 1);

    gpio_export(rf_rst_pin);
    gpio_set_dir(rf_rst_pin, 1);
    gpio_set_value(rf_rst_pin, 1);
    
    gpio_export(rf_oe_pin);
    gpio_set_dir(rf_oe_pin, 1);
    gpio_set_value(rf_oe_pin, 1);

	gpio_export(rf_srdy_pin);
	gpio_set_dir(rf_srdy_pin, 0);
	
	gpio_set_value(rf_rst_pin, 0);
	usleep(10000);
    gpio_set_value(rf_rst_pin, 1);
	usleep(10000);
}

void cc1310_set_oe(u8 output_enable)
{
	gpio_set_value(rf_oe_pin, output_enable);
}