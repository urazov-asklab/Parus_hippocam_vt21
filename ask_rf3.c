//ask_rf3.c
#include <unistd.h> 

#include "ask_rf3.h"
#include "cc1310_funcs.h"


#define CCDEV   "/dev/cc1310"

#define CONN_TIMEOUT        50//5 sec
#define CONFIRM_TIMEOUT     10
#define WOR_TIMEOUT         5


int init_rf()
{
    cc1310_setup_connection();
    cc1310_set_oe(1);

    if(cc1310_init(CCDEV) != SUCCESS)
        return FAILURE;
    
    if(set_rf_mode() != SUCCESS)
        return FAILURE;

    return SUCCESS;
}

int set_rf_mode()
{
    u8 mode, r_mode;

    usleep(10000);

    mode = (ASK_RF_3 << ASK_RF_SHIFT) | (MODE_ASK_RF << MODE_SHIFT);
    cc1310_write_reg(RF_MODE, mode);
    usleep(10000);

    if(cc1310_read_reg(RF_MODE, &r_mode) != SUCCESS)
    {
        debug("rf: mode reg read failed\n");
        return FAILURE;
    }

    if(mode != r_mode)
        debug("rf: mode reg write failed(wr: 0x%.2x, rd: 0x%.2x)\n", mode, r_mode);

    return SUCCESS;
}

void release_rf()
{
    cc1310_set_oe(0);
    cc1310_free();
}
