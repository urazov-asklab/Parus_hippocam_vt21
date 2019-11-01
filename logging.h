/*
 * logging.h
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/

#ifndef _LOGGING_H
#define _LOGGING_H

#include "common.h"

#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h> 
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <unistd.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

// перечень событий, которые логгируются; комментарием отмечены те, что используются в этом проекте
typedef enum
{
    log_NO_EVENT 					= 0,
    log_LOG_OFFSET_CORRECTED 		= 1,
    log_WATCHES_OSC_WAS_STOPED,
    log_SYSTEM_STARTUP_POWERUP,
    log_SYSTEM_STARTUP_TIMER,
    log_SYSTEM_STARTUP_EXTERNAL,            /* Device is started due to external power on */
    log_SYSTEM_STARTUP_RESET,
    log_SYSTEM_STARTUP_UNKNOWN,
        
    log_SYSTEM_OVERHEAT,                    /* System overheat */
    log_SYSTEM_UNDERHEAT,                   /* System returned to the correct temperature mode */
    
    log_SYSTEM_POWEROFF_OVERHEAT,
    log_SYSTEM_NORMAL_POWEROFF,				/* Device regular power off */
        
    log_REC_APL_INIT_FAILED,				/* Initialization error of record application */
    log_REC_APL_REC_TIMER_ACTIVATED,
    log_REC_APL_REC_TIMER_ENDED,
    log_REC_APL_REC_STARTED,    			/* Record started */
    log_REC_APL_REC_STOPPED,				/* Record stopped */
    log_REC_APL_REC_START_FAILED,			/* Record starting error (periphery failed) */
    log_REC_APL_REC_RUNTIME_ERROR,    		/* Error during recording */
    log_REC_APL_SD_INIT_ERROR,              /* Error of SD card initialization */
    log_REC_APL_SD_FAT_ERROR,               /* Error of SD card file system */
    log_REC_APL_SD_FILE_ERROR,
    log_REC_APL_SD_RUNTIME_ERROR,
    log_REC_APL_SD_MASK_CHANGED,
    
    log_SV_APL_INIT_FAILED,
    
    log_REC_APL_SD_FILE_ERASED,
    log_REC_APL_SD_CARD_FORMATTED,
    log_SV_BOX_WAS_OPENED,
    log_SV_BOX_WAS_RESETED,
    log_REC_APL_SD_CARD_FULL,				/* SD card is full */
    log_REC_APL_NEW_FRAG_BEGAN_VIDEO_ON,    /* New fragment started, video signal presents */
    log_REC_APL_NEW_FRAG_BEGAN_VIDEO_OFF,   /* New fragment started, video signal is missed */
    log_REC_APL_VIDEO_SYGNAL_CHANGED,
    log_REC_APL_INTERNAL_ERROR_OCCURED,		/* Internal application error occurred */
    log_REC_APL_WRONG_PROGRAM_STATE,
    log_REC_APL_FRAG_COMPLETED,
    log_SV_PROGRAM_RESET,                   /* Device was reset by software on command */
    
    log_SYSTEM_STARTUP_OVERHEAT,
    log_SYSTEM_NORMAL_POWEROFF_TIMER,
    log_SYSTEM_DEADPOWER,                   /* Battery is completely discharged */
    log_REC_APL_SD_NO,                      /* No SD card */
    log_REC_APL_VIDEO_NO,
    log_WATCHES_SET,                        /* Received the command for setting the real-time clock */
    log_WATCHES_NEW_TIME,                   /* Clock is set */
    log_SPI_SD_ERROR,				       // with_param, it means next log entry is treated by sd_error table
    
    log_SYSTEM_NET_SETTINGS_CHANGED,
    log_SYSTEM_FIRMWARE_UPDATE,
    
    log_REC_APL_SWITCH_REC_ON_EVENT,        /* Received the command to start recording from the slide switch */
    log_REC_APL_DU_REC_ON_EVENT,
    log_REC_APL_GSM_REC_ON_EVENT,
    log_REC_APL_RF_REC_ON_EVENT,            /* Received the command to start recording from RF */
    log_REC_APL_SWITCH_REC_OFF_EVENT,       /* Received the command to stop recording from the slide switch */
    log_REC_APL_DU_REC_OFF_EVENT,
    log_REC_APL_GSM_REC_OFF_EVENT,
    log_REC_APL_RF_REC_OFF_EVENT,           /* Received the command to stop recording from RF */
    log_REC_APL_SWITCH_REC_PRIORITIZED,
    log_REC_APL_DU_REC_PRIORITIZED,
    log_REC_APL_TIMER_EXT_DISABLED,
    
    log_SYSTEM_STARTUP_RF, 					/* Device is started due to RF */
    log_SYSTEM_STARTUP_GSM,
    log_SYSTEM_STARTUP_USB,                 /* Device is started due to USB */
    log_SYSTEM_USB_ATTACH,                  /* USB connection detected */
    
    log_REC_APL_EXTERNAL_MIC_USED,
    log_REC_APL_INTERNAL_MIC_USED,
    
    log_SYSTEM_STARTUP_ACOUSTIC_START,
    log_SYSTEM_SLEEP,
    log_EVENTS_NUM
} eLogEvents;

/* Event structure */
typedef struct Event 
{
    u16         year;
    u8          mon;
    u8          day;
    u8          hour;
    u8          min;
    u8          sec;
    eLogEvents  event_code;
} Event;

#define RING_BUF_SIZE 20    // имеем не больше 20 событий в кольцевом буффере

/* Structure for event ring buffer*/
typedef struct RingBuffer 
{
    Event           eventItem[RING_BUF_SIZE];
    int 			head;
    int 			tail;
    pthread_mutex_t mutex;
} RingBuffer;

#define CUR_EVT_INIT { 0 }

/* Global data */
extern RingBuffer evtBuf;

static inline Event newEvent(eLogEvents logEvt)
{
    Event        newEvt;
    time_t       seconds;
    struct tm   *timeinfo;

    seconds  = time(NULL);
    timeinfo = localtime(&seconds);

    newEvt.year         = timeinfo->tm_year + 1900;
    newEvt.mon          = timeinfo->tm_mon + 1;
    newEvt.day          = timeinfo->tm_mday;
    newEvt.hour         = timeinfo->tm_hour;
    newEvt.min          = timeinfo->tm_min;
    newEvt.sec          = timeinfo->tm_sec;
    newEvt.event_code   = logEvt;

    return newEvt;
}

static inline void initEventRingBuf()
{
	int i;

	/* Initialize the mutex which protects current event from erasure */
    pthread_mutex_init(&evtBuf.mutex, NULL);

    evtBuf.head = 0;
    evtBuf.tail = 0;

    for(i = 0; i < RING_BUF_SIZE; i++)
    {
    	evtBuf.eventItem[i].event_code = log_NO_EVENT;
    }
}

static inline void clearEventRingBuf()
{
    pthread_mutex_destroy(&evtBuf.mutex);
}

/* Sets a new event if buffer is not blocked */
static inline int trySetEvent(Event ev)
{
    pthread_mutex_lock(&evtBuf.mutex);

    if(evtBuf.eventItem[evtBuf.tail].event_code == log_NO_EVENT)
    {
        evtBuf.eventItem[evtBuf.tail] = ev;

        evtBuf.tail++;
        if(evtBuf.tail == RING_BUF_SIZE)
        {
        	evtBuf.tail = 0;
        }

        pthread_mutex_unlock(&evtBuf.mutex);
        return SUCCESS;
    }

    pthread_mutex_unlock(&evtBuf.mutex);
    return FAILURE;
}

static inline int logEvent(eLogEvents logEvt)
{
	Event 	curEvt;
	Command currentCommand;
	curEvt = newEvent(logEvt);

    while(1)
    {
    	currentCommand = gblGetCmd();
        if(trySetEvent(curEvt) == SUCCESS)
        {
        	sem_post(&semaphore);	
            return SUCCESS;
        }
        if(currentCommand == FINISH)
        {
        	ERR("Failed to log the event!!! \r\n");
            return FAILURE;
        }
        usleep(30000);
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
    }
    return SUCCESS;
}

/* Environment passed when creating the thread */
typedef struct LoggingEnv 
{
    Rendezvous_Handle hRendezvousInit;
    Rendezvous_Handle hRendezvousFinishLG;
} LoggingEnv;

/* Thread function prototype */
extern void *loggingThrFxn(void *arg);

#endif /* _LOGGING_H */
