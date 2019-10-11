/*
 * setting_server.h
 */

#ifndef _SETTING_SERVER_H
#define _SETTING_SERVER_H

#include "eth_iface.h"
#include "packet_io.h"

#include <ti/sdo/dmai/Rendezvous.h>

/* Environment passed when creating the thread */
typedef struct SettingServerEnv
{
	Rendezvous_Handle 				hRendezvousInit;
	Rendezvous_Handle 				hRendezvousFinish;
	app_server_interact_struct     *app_descr;
	app_server_interact_struct     *own_descr;
} SettingServerEnv;

/* Thread function prototype */
extern void *settingServerThrFxn(void *arg);	

// private (эти функции используются только внутри .c файла)
// u8  InitServer();
// u8  SetAVApplication();
// u8  DispatchPacket(u8 *packet, u16 packet_length, packet_info **pi);
// u8  CheckPacket(packet_info *pi);
// u8  CheckSetCmd(packet_info *pi, u8 dlen, u8 class_num, u8 *field_num, u8 pswd, u8*err, sock_info *src);
// u32 GetNewSSRC();
// u8  ExecuteAction(packet_info *pi, u16 *data_length, packet_info **pi_out, sock_info *src);
// u8  DoDataExchange();

#endif /* _SETTING_SERVER_H */