
#ifndef _GPIO_FUNC_H
#define _GPIO_FUNC_H

int gpio_export(u32 gpio);
int gpio_unexport(u32 gpio);
int gpio_set_dir(u32 gpio, u32 out_flag);
int gpio_set_value(u32 gpio, u32 value);
int gpio_get_value(u32 gpio, u32 *value);
int gpio_get_dir(u32 gpio, u32 *value);
int gpio_set_edge(u32 gpio, char *edge);
int gpio_fd_open(u32 gpio);
int gpio_fd_close(int fd);


//Port Expander input pins
#define PE_PORT_IN1     0x01
#define PE_PORT_IN2     0x02
#define PE_PORT_IN3     0x04
#define PE_BUT_PWR_ON   0x08
#define PE_CHRG_INT     0x10
#define PE_USB_VBUS_OK  0x20
#define PE_RF_PWR_ON    0x40
#define PE_SW_PWR_ON    0x80

int get_pe_change_mask();
int set_pe_interrupt_mask(u8 mask);

//Port Expander output pins//in omap gpio order
#define PE_PORT_OUT1        200
#define PE_PORT_OUT2        201
#define PE_PORT_OUT3        202
#define PE_PWR_HOLDn        203
#define PE_EXT_PWR_OUT_EN   204
#define PE_EXT_PWR_IN_EN    205
#define PE_PWR_RF_EN        206

#endif /* _GPIO_FUNC_H */