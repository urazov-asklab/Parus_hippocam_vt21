        
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

#define CC_PAUSE    1000

//DevStat values
enum 
{
    DS_Off = 0,
    DS_Init = 1,
    DS_Ready = 2,
    DS_RecReady = 3,
    DS_RecWaitEvent = 4,
    DS_RecEnabling = 5,
    DS_Record = 6,
    DS_RecordNoVideo = 7,
    DS_RecEvent = 8,
    DS_NoVideo = 9,
    DS_NoCard = 0x0A,
    DS_CardError = 0x0B,
    DS_CardFull = 0x0C,
    DS_PartError = 0x0E,
    DS_FatalHwError = 0x0F
}DevStatus;

extern  AVAppSettings curr_settings;

u8 Dev_Stat;
void UpdateDevStat();

int apply_cmd(u8 cmd, u8 param0, u8 param1)
{
    if(cmd == CTRL_EMPTY)
        return SUCCESS;

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
        if(!is_rec_started)
        {
            cam_channel_num = param0;
            curr_settings.cam_channel = param0;
            is_config_changed_from_wlan = 1;
        }
        return SUCCESS;
    }
    return FAILURE;
}

int check_cmd_param(u8 cmd, u8 param0, u8 param1)
{
    if(cmd == CTRL_EMPTY)
        return SUCCESS;
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
    u8 param1 = 0xFF;

    usleep(CC_PAUSE);
    if(cc1310_get_status(&st))
        return FAILURE;
    debug("rf_stat: 0x%.2x\n", st);
    usleep(CC_PAUSE);

    if(st & RF_STAT_RESET)
    {
        debug("cc1310 reset bit found!\n");
        //TODO:set mode again(and reset?)
    }

    mode = (st & RS_MODE_MASK) >> RS_MODE_SHIFT;
    state = (st & RS_STAT_MASK) >> RS_STAT_SHIFT;

    if(mode != RS_MODE_ASKRF)
    {
        debug("CC1310 Mode is not AskRF3!\n");
        set_rf_mode();
        return SUCCESS;
    }

    if((state != RS_STAT_WCMD) && (state != RS_STAT_RX) && (state != RS_STAT_WOR))
    {
        debug("CC1310 state is %i, strange!\n", state);
    }

    if(cc1310_read_reg(RF_CMD_STAT, &st))
        return FAILURE;
    usleep(CC_PAUSE);
    debug("rf: cmd stat: 0x%.2x\n", st);
    
    if((st & RCS_CMD_RECEIVED) == 0)
    {
        return SUCCESS;//no cmd received
    }
    
    if(cc1310_read_reg(RF_CMD_CHANNEL, &cmd))
        return FAILURE;
    usleep(CC_PAUSE);
    if(cc1310_read_reg(RF_CMD_PARAM_0, &param0))
        return FAILURE;
    usleep(CC_PAUSE);
    /*if(cc1310_read_reg(RF_CMD_PARAM_1, &param1))
        return FAILURE;
    usleep(CC_PAUSE);*/

    debug("rf: cmd: 0x%.2x, param0: 0x%.2x\n", cmd, param0);

    if(check_cmd_param(cmd, param0, param1) == SUCCESS)
    {//apply cmd
        if(apply_cmd(cmd, param0, param1))
            return FAILURE;
    }

    if(cc1310_read_reg(RF_CMD_CONFIRM, &st))//confirm command
        return FAILURE;
    usleep(CC_PAUSE);
    if(cc1310_read_reg(RF_CMD_STAT, &st))
        return FAILURE;
    usleep(CC_PAUSE);
    if(st != 0)
    {
        debug("RF_CMD_STAT(0x%.2x) is not 0 after cmd confirm!\n", st);
    }

    return SUCCESS;
}

void *radioCommThrFxn(void *arg)
{
    //debug("RadioComm thread started\r\n");

    void           *status              = THREAD_SUCCESS;
    RadioCommEnv   *envp                = (RadioCommEnv *) arg;    
    struct timespec timeout;
    int             ret;

    // rftx_stop = 0;

    if(init_rf())
    {
        ERR("Can't init RF\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        // is_rftx_failed = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    cc1310_set_oe(1);
    UpdateDevStat();
    cc1310_set_oe(0);

    /* Signal that initialization is done */
    if(envp->hRendezvousInit)
    	Rendezvous_meet(envp->hRendezvousInit);

    init_step   	= 0;

    while(1)
    {
        if(gblGetCmd() == FINISH)
            goto cleanup;

        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;

        rf_sleep_condition = 1;
        ret = sem_timedwait(&rfSem, &timeout);//sem_wait(&rfSem);
        rf_sleep_condition = 0;
        
        cc1310_set_oe(1);
        if((ret == 0) && (!update_dev_stat_only))//then somebody waked up us, need to process_rx
        {
            process_rx();
        }//otherwise just update DevStat

        UpdateDevStat();
        if(update_dev_stat_only)
            update_dev_stat_only = 0;
        cc1310_set_oe(0);
    }

cleanup:

	if(envp->hRendezvousInit)
    	Rendezvous_force(envp->hRendezvousInit);

    release_rf();

    if(envp->hRendezvousFinishRC)
    	Rendezvous_meet(envp->hRendezvousFinishRC);

    rf_sleep_condition = 1;
    debug("Radiocomm thread finished\n");
    return status;
}

void UpdateDevStat()
{
    Dev_Stat = 0;
	
    if((is_rec_failed == 1) && (is_rec_request))
    {
        if(is_cam_failed)
        {
            Dev_Stat = DS_NoVideo;
        }
        else
        {
            Dev_Stat = DS_PartError;
        }
    }
    else if (is_memory_full == 1)					// Memory full - 1
	{
		Dev_Stat = DS_CardFull;
	}
	else if (sd_failed == 1)						// SD card failed - 1
	{
		Dev_Stat = DS_NoCard;
	}
	else if (init_step == 1)						// Initialization - 2
	{
		Dev_Stat = DS_Init;
	}
	else if ((is_rec_request) && (!is_rec_started))	// Start recording - 4 //is_rec_on_cmd
	{
		Dev_Stat = DS_RecEnabling;
	}
	else if ((!init_step) && (!is_rec_started)) 	// Device ready to be managed or to broadcast- 5
	{
		Dev_Stat = DS_Ready;
	}
	else if (is_rec_started)						// Record - 6
	{
		Dev_Stat = (sound_only == 0) ? DS_Record : DS_RecordNoVideo;
	}

    //debug("DevStat: %i\n", Dev_Stat);
    usleep(CC_PAUSE);
    cc1310_write_reg(ACK_DEV_STAT, Dev_Stat);
    usleep(CC_PAUSE);
}
