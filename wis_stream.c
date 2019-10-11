//wis_stream.c

#include "common.h"
#include "transport_buffers.h"
#include "eth_iface.h"

#include "wis_stream.h"

extern int wis_start_streaming(int aFreq, int channels, int aBtrt, int vBtrt);

void *WISStreamThrFxn(void *arg)
{
    WISStreamEnv *envp      = (WISStreamEnv *) arg;
    void         *status   	= THREAD_SUCCESS;
    int 		  ret 	 	= -1;

    audio_channels_prev = audio_channels;
    video_bitrate_prev 	= video_bitrate;

    if(curEthIf.ip_addr == 0)
    {
        ERR("Getting ip address failed\r\n");
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    while(ret == -1)
    {
    	ret = wis_start_streaming(SAMPLING_FREQUENCY, audio_channels, AUDIO_BITRATE, video_bitrate);
    }

cleanup:
    if(envp->hRendezvousFinish != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinish);
    }
    debug("WISStreamThrFxn finished\r\n");
    return status;
}