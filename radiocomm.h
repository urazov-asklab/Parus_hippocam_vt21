/*
 * radiocomm.h
 *
 * Copyright (C) 2019 Ask Lab
 *
 */

#ifndef _RADIOCOMM_H
#define _RADIOCOMM_H

#include <semaphore.h>
#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

sem_t rfSem;

/* Environment passed when creating the thread */
typedef struct RadioCommEnv 
{
    Rendezvous_Handle hRendezvousInit;
    Rendezvous_Handle hRendezvousFinishRC;
//    Rendezvous_Handle hRendezvousInitRFTX;
//    Rendezvous_Handle hRendezvousStopRFTX;    
} RadioCommEnv;

/* Thread function prototype */
extern void *radioCommThrFxn(void *arg);

#endif /* _RADIOCOMM_H */
