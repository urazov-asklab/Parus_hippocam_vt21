/*
 * eth_iface.h
 */

#ifndef _ETH_IFACE_H
#define _ETH_IFACE_H

#include <sys/socket.h>

#include "common.h"

#define	MAX_APPS_NUM		2
#define ETH_BASE_PORT		1025

typedef struct sock_info
{
	u32 addr;
	u32 port;
} sock_info; // this pair is a socket

typedef struct app_info
{
	u8 	inited;
	int if_handle;
} app_info;

typedef struct EthIface 
{
	app_info	apps[MAX_APPS_NUM];
	u32 		ip_addr;
	u32 		netmask;
	u8  		mac_addr[6];
} EthIface;

EthIface curEthIf;

// public
u8   EthIfaceCheckIP();
u8 	 EthIfaceCreate();
u8   EthIfaceInit(u8 appnum);
int  EthIfaceRead(u8 appnum, u16 length, u8 *buffer, sock_info *src);
int  EthIfaceWrite(u8 appnum, u16 length, const u8 *buffer, const sock_info *dst);
u8 	 EthIfaceFree(u8 appnum);


#endif /* _ETH_IFACE_H */
