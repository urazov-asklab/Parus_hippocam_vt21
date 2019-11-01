//functions to access gpio from userspace

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <common.h>

#define SYSFS_GPIO_DIR "/sys/class/gpio"
#define POLL_TIMEOUT 	(3 * 1000) 			/* 3 seconds */

// see info about GPIO here: http://processors.wiki.ti.com/index.php/Linux_PSP_GPIO_Driver_Guide#


int gpio_export(u32 gpio)
{
	int     ret_value           = 0;
	int 	fd;
	int 	len;
	char 	buf[MAX_BUF];
 
	fd = open(SYSFS_GPIO_DIR "/export", O_WRONLY);
	if (fd < 0) 
	{
		perror("gpio/export");
		return fd;
	}
 
	len = snprintf(buf, sizeof(buf), "%lu", gpio);
	ret_value = write(fd, buf, len);
	if(ret_value < 0) 
    {
    	close(fd);
        ERR("Failed to write to %s%s file: %d (gpio #%lu)\n\r", SYSFS_GPIO_DIR, "/export", errno, gpio);
        return FAILURE;
    }
	close(fd);
 
	return SUCCESS;
}


int gpio_unexport(u32 gpio)
{
	int     ret_value           = 0;
	int 	fd;
	int 	len;
	char 	buf[MAX_BUF];
 
	fd = open(SYSFS_GPIO_DIR "/unexport", O_WRONLY);
	if (fd < 0) 
	{
		perror("gpio/export");
		return fd;
	}
 
	len = snprintf(buf, sizeof(buf), "%lu", gpio);
	ret_value = write(fd, buf, len);
	if(ret_value < 0) 
    {
    	close(fd);
        ERR("Failed to write to %s%s file: %d\n\r", SYSFS_GPIO_DIR, "/unexport", errno);
        return FAILURE;
    }
	close(fd);
	return SUCCESS;
}


int gpio_set_dir(u32 gpio, u32 out_flag)
{
	int     ret_value           = 0;
	int 	fd;
	int 	len;
	char 	buf[MAX_BUF];
 
	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR  "/gpio%lu/direction", gpio);
 
 	if(len <= 0)
 	{
		perror("gpio/direction");
		return FAILURE;
	}
	else
	{
		fd = open(buf, O_WRONLY);
		if (fd < 0) 
		{
			perror("gpio/direction");
			return fd;
		}
	 
		if(out_flag)
		{
			ret_value = write(fd, "out", 4);
		}
		else
		{
			ret_value = write(fd, "in", 3);
		}

		if(ret_value < 0) 
	    {
	    	close(fd);
	        ERR("Failed to write to %s file: %d\n\r", buf, errno);
	        return FAILURE;
	    }
	 
		close(fd);
	}
	return SUCCESS;
}

int gpio_set_value(u32 gpio, u32 value)
{
	int     ret_value           = 0;
	int 	fd;
	int 	len;
	char 	buf[MAX_BUF];
 
	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%lu/value", gpio);
 
 	if(len <= 0)
 	{
		perror("gpio/set-value");
		return FAILURE;
	}
	else
 	{
		fd = open(buf, O_WRONLY);
		if (fd < 0) 
		{
			perror("gpio/set-value");
			return fd;
		}
	 
		if (value)
		{
			ret_value = write(fd, "1", 2);
		}
		else
		{
			ret_value = write(fd, "0", 2);
		}

		if(ret_value < 0) 
	    {
	    	close(fd);
	        ERR("Failed to write to %s file: %d\n\r", buf, errno);
	        return FAILURE;
	    }
	 
		close(fd);
	}
	return SUCCESS;
}


int gpio_get_value(u32 gpio, u32 *value)
{
	int 	fd; 
	int 	len;
	char 	buf[MAX_BUF];
	char 	ch;

	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%lu/value", gpio);

	if(len <= 0)
	{
		perror("gpio/get-value");
		return FAILURE;
	}
	else
	{
		fd = open(buf, O_RDONLY);
		if (fd < 0) 
		{
			perror("gpio/get-value");
			return fd;
		}
	 
		read(fd, &ch, 1);

		*value = (ch != '0') ? 1 : 0;
	 
		close(fd);
	}
	return SUCCESS;
}


int gpio_get_dir(u32 gpio, u32 *value)
{
	int 	fd; 
	int 	len;
	char 	buf[MAX_BUF];
	char 	ch[5];

	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%lu/direction", gpio);
 
 	if(len <= 0)
 	{
		perror("gpio/direction");
		return FAILURE;
	}
	else
 	{
		fd = open(buf, O_RDONLY);
		if (fd < 0) 
		{
			perror("gpio/direction");
			return fd;
		}
	 
		read(fd, &(ch[0]), 3);

		if ((ch[0] == 'o') && (ch[1] == 'u') && (ch[2] == 't')) 
		{
			*value = 1;
		} 
		else if ((ch[0] == 'i') && (ch[1] == 'n'))
		{
			*value = 0;
		} 
		else 
		{
			*value = 2;
		}
	 
		close(fd);
	}
	return SUCCESS;
}


int gpio_set_edge(u32 gpio, char *edge)
{
	int     ret_value           = 0;
	int 	fd; 
	int 	len;
	char 	buf[MAX_BUF];

	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%lu/edge", gpio);
 
 	if(len <= 0)
 	{
		perror("gpio/set-edge");
		return FAILURE;
	}
	else
 	{
		fd = open(buf, O_WRONLY);
		if (fd < 0) 
		{
			perror("gpio/set-edge");
			return fd;
		}
	 
		ret_value = write(fd, edge, strlen(edge) + 1); 
		if(ret_value < 0) 
	    {
	    	close(fd);
	        ERR("Failed to write to %s file: %d\n\r", buf, errno);
	        return FAILURE;
	    }
		close(fd);
	}
	return SUCCESS;
}


int gpio_fd_open(u32 gpio)
{
	int 	fd;
	int 	len;
	char 	buf[MAX_BUF];

	len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%lu/value", gpio); 
	if(len <= 0)
	{
		perror("gpio/fd_open");
		return FAILURE;
	}
	else
	{
		fd 	= open(buf, O_RDONLY | O_NONBLOCK );
		if (fd < 0) 
		{
			perror("gpio/fd_open");
		}
	}
	return fd;
}


int gpio_fd_close(int fd)
{
	if(fd > 0)
	{
		return close(fd);
	}
	else
	{
		return FAILURE;
	}
}

#define CHANGE_MASK_PATH	"/sys/devices/platform/omap/omap_i2c.2/i2c-2/2-0068/chng_mask"

int get_pe_change_mask()
{
    int     fd;
    int 	value;
    char    buf[MAX_BUF];
    char    ch[2];

    snprintf(buf, sizeof(buf), CHANGE_MASK_PATH);
 	
	fd  = open(buf, O_RDONLY);
	if (fd < 0)
	{
		ERR("Can't open %s\r\n", CHANGE_MASK_PATH);
		return FAILURE;
	}
	
	read(fd, &ch, 2);
	value = atoi(ch); 
	close(fd);
	
    return value;
}

#define INT_MASK_PATH	"/sys/devices/platform/omap/omap_i2c.2/i2c-2/2-0068/int_mask"

int set_pe_interrupt_mask(u8 mask)
{
	char    buf[MAX_BUF];

	snprintf(buf, sizeof(buf), "echo %u > %s", mask, INT_MASK_PATH);
	
	return system(buf);
}
