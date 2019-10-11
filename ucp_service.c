/*
 * ucp_service.c
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>

#include "common.h"
#include "logging.h"
#include "packet_io.h"

#include "ucp_service.h"

t_target_packet                 ucp_in_p;
t_target_packet                 ucp_out_p;
app_server_interact_struct     *app_descr;

typedef struct net_config_info
{
    u8  mac_addr[6];
    u32 ipaddr;
    u32 netmask;
    u32 gateway;
} net_config_info;

struct net_config_info user_net_config_info;

#define BUFSIZE 8192

struct route_info 
{
    struct in_addr dstAddr;
    struct in_addr gateWay;
};


u8 ProcessReq(t_target_packet *p, int *blen)
{
    u8             *d;
    u8             *sd;
    u16             mt;
    u32             t32;
    u32             gateway;
    u32             ip;
    u32             nmask;
    ucp_packet     *up;
    ucp_packet     *oup;

    gateway = user_net_config_info.gateway;
    nmask   = user_net_config_info.netmask;
    ip      = user_net_config_info.ipaddr;
  
    if(ucp_in_p.udaph.type != htons(UDAP_TYPE_UCP))
    {
        WARN("UDAP header has wrong type \r\n");
        return FAILURE;
    }
    if(!(ucp_in_p.udaph.dest_addr.flags & F_BROADCAST))
    {
        switch(ucp_in_p.udaph.dest_addr.p_type)
        {
            case PT_UDP:
                if(ucp_in_p.udaph.dest_addr.addr.udp.ip != htonl(ip))
                {
                    WARN("Packet from wrong ip address was received\r\n");
                    return FAILURE;
                }
                break;
            case PT_ETH:
                if(0 != memcmp(ucp_in_p.udaph.dest_addr.addr.mac, user_net_config_info.mac_addr, 6))
                {
                    WARN("Packet from wrong hw address was received\r\n");
                    return FAILURE;
                }
                break;
            case PT_RAW:                
            default:
                WARN("Wrong protocol type in the packet\r\n");
                return FAILURE;
        }
    }

    memcpy(ucp_out_p.udaph.source_addr.addr.mac, user_net_config_info.mac_addr, 6);
    ucp_out_p.udaph.source_addr.flags   = 0;
    ucp_out_p.udaph.source_addr.p_type  = PT_ETH;
    memcpy(&ucp_out_p.udaph.dest_addr, &ucp_in_p.udaph.source_addr, 8);
    memcpy(&ucp_out_p.udaph.seq_num, &ucp_in_p.udaph.seq_num, 2);

    ucp_out_p.udaph.type    = htons(UDAP_TYPE_UCP);
    up                      = &ucp_in_p.ucpp;
    oup                     = &ucp_out_p.ucpp;

    if((up->flags & F_REQ)==0)
    {
        WARN("Wrong flag type in the packet\r\n");
        return FAILURE;
    }

    oup->flags = 0;
    
    memcpy(&t32, &up->class_num, 4);
    if(t32 != htonl(UAP_CLASS_UCP))
    {
        WARN("Wrong class type in the packet\r\n");
        return FAILURE;
    }

    memcpy(&oup->class_num, &up->class_num, 4);
    memcpy(&oup->method_num, &up->method_num, 2);   
    memcpy(&mt, &up->method_num, 2);
    
    sd  = up->data;
    d   = oup->data;    
    
    switch(htons(mt))
    {
        case UCP_METHOD_DISCOVER:
            *d++ = DEV_TYPE_OPTION;
            *d++ = strlen(dev_type_str);
            memcpy(d, dev_type_str, strlen(dev_type_str));

            d      += strlen(dev_type_str);
            *d++    = DEV_NAME_OPTION;
            t32     = strlen(app_descr->app.name);  // 0x05;

            if(t32 > APP_NAME_LENGTH)
            {
                t32 = APP_NAME_LENGTH;
            }
            *d++ = t32;

            memcpy(d, app_descr->app.name, t32);
            d += t32;
            break;

        case UCP_METHOD_GET_IP:
            *d++ = USE_DHCP_OPTION;
            *d++ = 1;
            *d++ = 0;
            *d++ = GATE_ADDR_OPTION;
            *d++ = 4;
            *d++ = (gateway >> 24) & 0xFF;
            *d++ = (gateway >> 16) & 0xFF;
            *d++ = (gateway >> 8) & 0xFF;
            *d++ = (gateway) & 0xFF;
            *d++ = NET_MASK_OPTION;
            *d++ = 4;
            *d++ = (nmask >> 24) & 0xFF;
            *d++ = (nmask >> 16) & 0xFF;
            *d++ = (nmask >> 8) & 0xFF;
            *d++ = (nmask) & 0xFF;
            *d++ = IP_ADDR_OPTION;
            *d++ = 4;
            *d++ = (ip >> 24) & 0xFF;
            *d++ = (ip >> 16) & 0xFF;
            *d++ = (ip >> 8) & 0xFF;
            *d++ = (ip) & 0xFF;
            break;

        case UCP_METHOD_SET_IP:
            ip      = (sd[0] << 24) | (sd[1] << 16) | (sd[2] << 8) | (sd[3]);
            sd     += 4;
            nmask   = (sd[0] << 24) | (sd[1] << 16) | (sd[2] << 8) | (sd[3]);
            sd     += 4;
            gateway = (sd[0] << 24) | (sd[1] << 16) | (sd[2] << 8) | (sd[3]);
            sd     += 4;

            user_net_config_info.gateway    = gateway;
            user_net_config_info.netmask    = nmask;
            user_net_config_info.ipaddr     = ip;

            logEvent(log_SYSTEM_NET_SETTINGS_CHANGED);
            is_config_changed_from_wlan = 1;
            break;

        case UCP_METHOD_RESET:
            break;
        case UCP_METHOD_GET_DATA:
            break;
        case UCP_METHOD_SET_DATA:
            break;
        default:
            WARN("Wrong UCP method\r\n");
            return FAILURE;
    }
    *blen   = (u32)d - (u32)&oup->data;
    *blen  += 27;
    return SUCCESS;
}


void *ucpServiceThrFxn(void *arg)
{
    Command             currentCommand;
    void               *status          = THREAD_SUCCESS;
    UCPServiceEnv       *envp           = (UCPServiceEnv *) arg;
    struct sockaddr_in  host;
    struct sockaddr_in  from;
    int                 socket_fd;
    int                 length          = 0;
    int                 rcvlen          = 0;
    u32                 flen            = 0;
    int                 sendlen         = 0;
    int                 option_value    = 1;

    app_descr = envp->app_descr;

    if(curEthIf.ip_addr == 0)
    {
        ERR("Getting ip address failed\r\n");
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(socket_fd < 0)
    {
        ERR("Socket opening failed\r\n");
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    if(is_access_point == 0)
    {
        user_net_config_info.gateway = inet_addr("0.0.0.0");
    }
    else
    {
        user_net_config_info.gateway = curEthIf.ip_addr;
    }

    user_net_config_info.ipaddr     = curEthIf.ip_addr;
    user_net_config_info.netmask    = curEthIf.netmask;
    memcpy(&user_net_config_info.mac_addr[0], (const void *)&curEthIf.mac_addr[0], 6);

    memset(&host, 0, sizeof(struct sockaddr_in));
    host.sin_family         = AF_INET;
    host.sin_addr.s_addr    = INADDR_ANY;
    host.sin_port           = htons(UDAP_PORT);
    if (bind(socket_fd, (struct sockaddr *)&host, sizeof(struct sockaddr_in)) == -1)
    {
        close(socket_fd);
        ERR("Cannot bind ucp socket on ip %x \r\n", host.sin_addr.s_addr);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    // без этой опции есть проблемы с отправкой и получением широковещательных сообщений
    setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, (char*) &option_value, sizeof(int)); 

    while(1)
    {
        currentCommand = gblGetCmd();

        if((currentCommand == FINISH) || (currentCommand == SLEEP) /*|| (stop_netconnect == 1)*/)
        {
            close(socket_fd);
            debug("UCP service thread finishing\r\n");
            goto cleanup;
        }
        length = sizeof(t_target_packet);
        rcvlen = recvfrom(socket_fd, (char *) &ucp_in_p, length, MSG_DONTWAIT, (struct sockaddr *)&from, 
                            (socklen_t *) &flen);
        if(rcvlen > 0)
        {
            length = rcvlen;
            if(!ProcessReq(&ucp_in_p, &length))
            {
                host.sin_family         = AF_INET;
                if(is_access_point == 0)
                {
                    host.sin_addr.s_addr = INADDR_BROADCAST;
                }
                else
                {
                    host.sin_addr.s_addr = inet_addr("192.168.0.255");
                }
                host.sin_port           = from.sin_port;
                sendlen = sendto(socket_fd, (char*)&ucp_out_p, length, MSG_DONTWAIT, (struct sockaddr*)&host, sizeof(host));
                if(sendlen != length)
                {
                    WARN("Failed to send UCP response: length %i, error #%i\r\n", sendlen, errno);
                }
            }
        }

        usleep(20000);
    }
    cleanup:

    if(envp->hRendezvousFinish != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinish);
    }

    debug("UCP service thread finished\r\n");
    return status;  
}