        
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "radiocomm.h"
#include "ask_rf3.h"
#include "logging.h"
#include "cc1310_funcs.h"
#include "avrec_service.h"

int apply_cmd(u8 cmd, u8 param0, u8 param1)
{
    if(cmd == CTRL_WIFI)
    {
        if(param0 == 0)
        {
            stop_netconnect = 1;
        }
        else
        {
            stop_netconnect = 0;
        }
        return SUCCESS;
    }
    
    if((cmd == CTRL_RECORD))
    {
        fix_record_state(param0);
        start_rec = param0;
        debug("initiated from rf\r\n");
        return SUCCESS;
    }

    if(cmd == CTRL_ACAM_NUM)
    {
        cam_channel_num = param0;
        //need to sync with avrec_service
        //need to restart record
        return SUCCESS;
    }
    return FAILURE;
}

int check_cmd_param(u8 cmd, u8 param0, u8 param1)
{
    if((cmd == CTRL_RECORD) || (cmd == CTRL_ACAM_NUM) || (cmd == CTRL_WIFI))
    {
        if((param0 == 0) || (param0 == 1))
            return SUCCESS;
        return FAILURE;
    }
    return FAILURE;
}

int process_rx()
{
    u8 st;
    u8 mode;
    u8 state;
    u8 cmd;
    u8 param0;
    u8 param1;

    if(cc1310_get_status(&st))
        return FAILURE;

    mode = (st & RS_MODE_MASK) >> RS_MODE_SHIFT;
    state = (st & RS_STAT_MASK) >> RS_STAT_SHIFT;

    if(mode != RS_MODE_ASKRF)
    {
        debug("CC1310 Mode is not AskRF3!\n");
    }

    if(state != RS_STAT_WCMD)
    {
        if(state != RS_STAT_WOR)
            debug("CC1310 state is %i, strange!\n", state);
        //return SUCCESS;
    }

    if(cc1310_read_reg(RF_CMD_STAT, &st))
        return FAILURE;
    
    if((st & RCS_CMD_RECEIVED) == 0)
        return SUCCESS;//no cmd received

    if(cc1310_read_reg(RF_CMD_CHANNEL, &cmd))
        return FAILURE;
    if(cc1310_read_reg(RF_CMD_PARAM_0, &param0))
        return FAILURE;
    if(cc1310_read_reg(RF_CMD_PARAM_1, &param1))
        return FAILURE;

    if(check_cmd_param(cmd, param0, param1) == SUCCESS)
    {//apply cmd
        if(apply_cmd(cmd, param0, param1))
            return FAILURE;
    }

    if(cc1310_read_reg(RF_CMD_CONFIRM, &st))//confirm command
        return FAILURE;
    if(cc1310_read_reg(RF_CMD_STAT, &st))
        return FAILURE;
    if(st != 0)
    {
        debug("RF_CMD_STAT is not 0 after cmd confirm!\n");
    }

    return SUCCESS;
}

void *radioCommThrFxn(void *arg)
{
    //debug("RadioComm thread started\r\n");

    void           *status              = THREAD_SUCCESS;
    RadioCommEnv   *envp                = (RadioCommEnv *) arg;    
    Command 		currentCommand;

    // rftx_stop = 0;

    if(init_rf())
    {
        ERR("Can't init RF\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        // is_rftx_failed = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    /* Signal that initialization is done */
    if(envp->hRendezvousInit != NULL)
    {
    	Rendezvous_meet(envp->hRendezvousInit);
    }

    init_step   	= 0;

    while(1)
    {
        currentCommand = gblGetCmd();
        if(currentCommand == FINISH)
        {
            //debug("Radiothread finishing...\r\n");
            goto cleanup;
        }

        radiocomm_sleeping = 1;
        sem_wait(&rfSem);
        radiocomm_sleeping = 0;//wakeup
        rf_sleep_condition = 0;

        process_rx();
        radiocomm_last_cmd_time = uptime();

        //UpdateInfo();
    }

cleanup:

    //debug("Finishing radiocommunication ... \r\n");

	if(envp->hRendezvousInit != NULL)
	{
    	Rendezvous_force(envp->hRendezvousInit);
    }

    release_rf();

    if(envp->hRendezvousFinishRC != NULL)
    {
    	Rendezvous_meet(envp->hRendezvousFinishRC);
    }
    debug("Radiocomm thread finished\r\n");
    return status;
}
