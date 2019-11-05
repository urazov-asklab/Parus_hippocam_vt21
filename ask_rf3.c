//ask_rf3.c

#include "ask_rf3.h"
#include "cc1310_funcs.h"


#define CCDEV   "/dev/cc1310"

#define CONN_TIMEOUT        50//5 sec
#define CONFIRM_TIMEOUT     10
#define WOR_TIMEOUT         5


int init_rf()
{
    u8 mode;

    cc1310_setup_connection();

    if(cc1310_init(CCDEV) != SUCCESS)
        return FAILURE;
    
    cc1310_write_reg(RF_CONNECT_TIMEOUT, CONN_TIMEOUT);
    cc1310_write_reg(RF_CONFIRM_TIMEOUT, CONFIRM_TIMEOUT);
    cc1310_write_reg(RF_WOR_CONFIG, WOR_TIMEOUT);

    //UpdateInfo();

    mode = (ASK_RF_3 << ASK_RF_SHIFT) | (MODE_ASK_RF << MODE_SHIFT);
    cc1310_write_reg(RF_MODE, mode);

    return SUCCESS;
}

void release_rf()
{
    cc1310_free();
}
