//steam_buffs.h

#ifndef __STREAMBUFFS_H
#define __STREAMBUFFS_H

#include <pthread.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

/* Environment passed when creating the thread */
typedef struct StreamBuffsEnv 
{
    Rendezvous_Handle hRendezvousFinishSTRM;
} StreamBuffsEnv;

extern void *StreamBuffsThrFxn(void *arg);


#endif//__STREAMBUFFS_H
