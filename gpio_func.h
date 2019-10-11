
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
//int gpio_get_int_status();

#endif /* _GPIO_FUNC_H */