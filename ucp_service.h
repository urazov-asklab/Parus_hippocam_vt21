/*
 * ucp_service.h
 */

#include <ti/sdo/dmai/Rendezvous.h>

#ifndef _UCP_SERVICE_H_    
#define _UCP_SERVICE_H_

#define	UDAP_PORT	        0x4578
#define F_BROADCAST         1
#define PT_UDP              2
#define PT_ETH              1
#define PT_RAW              0
#define F_REQ               1
#define UAP_CLASS_UCP       0x00010001
#define UCP_METHOD_DISCOVER 0x0001
#define UCP_METHOD_GET_IP   0x0002
#define UCP_METHOD_SET_IP   0x0003
#define UCP_METHOD_RESET    0x0004
#define UCP_METHOD_GET_DATA 0x0005
#define UCP_METHOD_SET_DATA 0x0006
#define MAC_ADDR_OPTION     0x01
#define DEV_NAME_OPTION     0x02
#define DEV_TYPE_OPTION     0x03
#define USE_DHCP_OPTION     0x04
#define IP_ADDR_OPTION      0x05
#define NET_MASK_OPTION     0x06
#define GATE_ADDR_OPTION    0x07
#define UDAP_TYPE_UCP       0xC001

#pragma pack (push, 1)
typedef struct udp_addr
{
    u32 ip;
    u16 port;
} udp_addr;

typedef struct t_udap_addr
{		
    u8 flags;
    u8 p_type;
    union addr
    {        
        udp_addr udp;        
        u8 mac[6];        
        u8 raw_adr[6];
    } addr;
} t_udap_addr;

typedef struct ucp_packet
{
    u8  flags;		
    u32 class_num;
	u16 method_num;    
    u8  data[100];
} ucp_packet;

typedef	struct udap_header
{
    t_udap_addr     dest_addr;    
    t_udap_addr     source_addr;
	u16	            seq_num;		
	u16	            type;
}udap_header;

typedef struct t_target_packet
{  
    udap_header udaph;
    ucp_packet	ucpp;
} t_target_packet;

#pragma pack (pop)

/* Environment passed when creating the thread */
typedef struct UCPServiceEnv 
{
    app_server_interact_struct     *app_descr;
    Rendezvous_Handle               hRendezvousFinish;
} UCPServiceEnv;

/* Thread function prototype */
extern void *ucpServiceThrFxn(void *arg);

// private:
// u8   ProcessReq(t_target_packet *p, int *blen);

#endif /* _UCP_SERVICE_H_ */