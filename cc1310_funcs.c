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


int cc1310_write_reg(u8 reg, u8 val)
{
	int 	status;
	struct 	cc1310_ioctl_p iop;

	ccbuf[0] = val;
	iop.reg  = reg;
	iop.buf  = ccbuf;
	status 	 = ioctl(cc1310_dev, CC1310_IOCTL_WRITE, (void*)&iop);

	return status;
}

int cc1310_read_reg(u8 reg, u8 *val)
{
	int 	status;
	struct 	cc1310_ioctl_p iop;

	iop.reg = reg;
	iop.buf = ccbuf;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_READ, (void*)&iop);
	*val 	= ccbuf[0];

	return status;
}

int cc1310_write_burst(u8 reg, u8 len, u8*buf)
{
    int 	status;
	struct 	cc1310_ioctl_p iop;

	iop.reg = reg;
	iop.buf = buf;
	iop.len = len;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_WRITE_BURST, (void*)&iop);

	return status;
}

int cc1310_read_burst(u8 reg, u8 len, u8*buf)
{
    int 	status;
	struct 	cc1310_ioctl_p iop;

	iop.reg = reg;
	iop.buf = buf;	
	iop.len = len;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_READ_BURST, (void*)&iop);

	return status;
}

int cc1310_get_status(u8*sta)
{
    int 	status;
	struct 	cc1310_ioctl_p iop;

	iop.buf = ccbuf;
	status 	= ioctl(cc1310_dev, CC1310_IOCTL_GET_STATUS, (void*)&iop);

	if(!status)
	{
		*sta = ccbuf[0];
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
	

	while(1)
	{
		ret = cc1310_get_status(&sta);
		i++;

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
    rf_rst_pin 			= RF_RST_VAL;
    rf_oe_pin 			= RF_OE_VAL;
 
    gpio_export(rf_rst_pin);
    gpio_set_dir(rf_rst_pin, 1);
    gpio_set_value(rf_rst_pin, 0);
    usleep(10000);
    gpio_set_value(rf_rst_pin, 1);

    gpio_export(rf_oe_pin);
    gpio_set_dir(rf_oe_pin, 1);
    gpio_set_value(rf_oe_pin, 1);
}
