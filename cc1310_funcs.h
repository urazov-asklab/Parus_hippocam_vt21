//cc1310_funcs.h

#ifndef __CC1310_FUNCS_H
#define __CC1310_FUNCS_H

#include "common.h"

#define CC_WRITE   0x00
#define CC_READ    0x80
#define CC_BURST   0x40


int	cc1310_init(char *dev);
void cc1310_free();
void cc1310_setup_connection();

int cc1310_get_status(u8*sta);
int cc1310_read_reg(u8 reg, u8*val);
int cc1310_write_reg(u8 reg, u8 val);
//int cc1310_write_burst(u8 reg, u8 len, u8*buf);
//int cc1310_read_burst(u8 reg, u8 len, u8*buf);

#endif
