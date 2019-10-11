//wis_stream.h

#ifndef __WISSTREAM_H
#define __WISSTREAM_H

#include <pthread.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"

// переменные, объявленные в библиотеке live для стриминга по сети
extern int wis_video_hw_started;
extern int wis_audio_hw_started;
extern int wis_audio_enable;
extern int wis_video_enable;
extern int wis_hw_started;

u32        video_bitrate_prev;
u32        audio_channels_prev;

extern int wis_stop_streaming();

/* Environment passed when creating the thread */
typedef struct WISStreamEnv 
{
	Rendezvous_Handle               hRendezvousFinish;
} WISStreamEnv;

extern void *WISStreamThrFxn(void *arg);


#endif//__WISSTREAM_H
