/*
 * eth_iface.c
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>

#include "eth_iface.h"

u8 EthIfaceCheckIP()	// have we got ip address already or not?
{
	int 			sock_fd;
    struct ifreq 	ifr 		= {};

    sock_fd = socket(PF_INET, SOCK_DGRAM, 0);

    strncpy(ifr.ifr_name, "wlan0", sizeof(ifr.ifr_name));

    if(ioctl(sock_fd, SIOCGIFADDR, &ifr) < 0)
    {
    	close(sock_fd);
    	return FAILURE;
    }

    close(sock_fd);
    return SUCCESS;
}

u8 EthIfaceCreate()
{
	int i = 0;
	for(; i < MAX_APPS_NUM; i++)
	{
		curEthIf.apps[i].inited 	= 0;
		curEthIf.apps[i].if_handle 	= 0;
	}
	curEthIf.ip_addr = 0;
	return SUCCESS;
}


u8 EthIfaceInit(u8 appnum)
{
	u16					port_num;
	int 				socket_fd;
	struct ifreq		ifr;
	struct sockaddr_in 	host;
	// int 				i;
	int                 option_value    = 1;

	if(appnum > (MAX_APPS_NUM - 1))
	{
		ERR("EthIfaceInit got wrong appnum!\r\n");
		return FAILURE;
	}
	if(curEthIf.apps[appnum].inited)
	{
		return SUCCESS;
	}
	socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(socket_fd < 0)
	{
		ERR("Socket opening failed!\r\n");
		return FAILURE;
	}

	// to get an IPv4 IP address
	ifr.ifr_addr.sa_family = AF_INET;

	// to get IP address attached to "wlan0"
	strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);

	if(ioctl(socket_fd, SIOCGIFADDR, &ifr) != 0)
	{
		ERR("SIOCGIFADDR failed, is the interface up and configured?\r\n");
		close(socket_fd);
		return FAILURE;
	}
	curEthIf.ip_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
	// debug("OUR IPADDR is %lx\r\n", curEthIf.ip_addr);

	if(ioctl(socket_fd, SIOCGIFNETMASK, &ifr) != 0)
	{
		ERR("SIOCGIFNETMASK failed, is the interface up and configured?\r\n");
		close(socket_fd);
		return FAILURE;
	}
	curEthIf.netmask = ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr;
	// debug("OUR NETMASK is %lx\r\n", curEthIf.netmask);

	if(ioctl(socket_fd, SIOCGIFHWADDR, &ifr) != 0)
	{
		ERR("SIOCGIFHWADDR failed, is the interface up and configured?\r\n");
		close(socket_fd);
		return FAILURE;
	}
	memcpy(curEthIf.mac_addr, ifr.ifr_hwaddr.sa_data, 6);

	// printf("OUR HWADDR is ");
	// for(i = 0; i < 6; i++)
	// {
	// 	if(i > 0)
	// 	{
	// 		printf(":");
	// 	}
	// 	printf("%02x", curEthIf.mac_addr[i]);
	// }
	// printf("\r\n");

	host.sin_family 			= AF_INET;
	if(appnum != 0)
	{
		host.sin_addr.s_addr 	= curEthIf.ip_addr;
	}
	else
	{
		host.sin_addr.s_addr 	= INADDR_ANY;
	}
	
	port_num 					= (u16)ETH_BASE_PORT + (u16)appnum;
	host.sin_port 				= htons(port_num);

	// debug("BIND port %i to id %i\r\n", host.sin_port, socket_fd);

	if (bind(socket_fd, (struct sockaddr *)&host, sizeof(struct sockaddr_in)) == -1)
	{
		close(socket_fd);
		ERR("Cannot bind app#%i socket on ip %lx \r\n", appnum, curEthIf.ip_addr);
		return FAILURE;
	}

	if(appnum == 0)
	{
		// setting server (app#0) can get or send broadcast packets
		setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, (char*) &option_value, sizeof(int)); 
	}

	curEthIf.apps[appnum].if_handle 	= socket_fd;
	curEthIf.apps[appnum].inited 		= 1;
	return SUCCESS;
}


int EthIfaceRead(u8 appnum, u16 length, u8* buffer, sock_info* src)
{
    struct sockaddr_in 	from;
    int 				rcvlen = 0;
    socklen_t 			srclen = sizeof(struct sockaddr_in);
    
 	if(appnum > (MAX_APPS_NUM - 1))
 	{
		ERR("EthIfaceRead got wrong appnum!\r\n");
		return FAILURE;
 	}
	if(!curEthIf.apps[appnum].inited)
	{
		ERR("App#%i is not inited0!\r\n", appnum);
		return FAILURE;
	}

	rcvlen = recvfrom(curEthIf.apps[appnum].if_handle, buffer, length, MSG_DONTWAIT, 
						(struct sockaddr*) &from, &srclen);
	if(rcvlen > 0)
	{
		// debug("RECV APPNUM %i id %i\r\n", appnum, curEthIf.apps[appnum].if_handle);
		src->addr = from.sin_addr.s_addr;
		src->port = from.sin_port;
	}
	return rcvlen;
}


int EthIfaceWrite(u8 appnum, u16 length, const u8 *buffer, const sock_info *dst)
{
	// int 				z;
    struct sockaddr_in 	host;
 	int 				sendlen;
 	
 	if(appnum > (MAX_APPS_NUM - 1))
 	{
		ERR("EthIfaceWrite got wrong appnum!\r\n");
		return FAILURE;
 	}
	if(!curEthIf.apps[appnum].inited)
	{
		ERR("App#%i is not inited1!\r\n", appnum);
		return FAILURE;
	}

	host.sin_family 		= AF_INET;
	host.sin_addr.s_addr 	= dst->addr;
	host.sin_port 			= dst->port;

	// debug("SEND APPNUM %i IP %x PORT %x\r\n", appnum, host.sin_addr.s_addr, host.sin_port);
	// printf("Outpacket:\r\n");
	// for(z = 0; z < length; z++)
	// {
	// 	printf("%02x ", buffer[z]);
	// }
	// printf("\r\n");

	sendlen = sendto(curEthIf.apps[appnum].if_handle, (void*)buffer, length, 0,
				(struct sockaddr*) &host, sizeof(host));
	
	return sendlen;
}


u8 EthIfaceFree(u8 appnum)
{
 	if(appnum > (MAX_APPS_NUM - 1))
 	{
		ERR("EthIfaceFree got wrong appnum!\r\n");
		return FAILURE;
 	}
	if(!curEthIf.apps[appnum].inited)
	{
		if(curEthIf.apps[appnum].if_handle == 0)
		{
			return SUCCESS;
		}
		ERR("App#%i is not inited2!\r\n", appnum);
		return FAILURE;
	}
	curEthIf.apps[appnum].inited = 0;
	if(curEthIf.apps[appnum].if_handle)
	{
		close(curEthIf.apps[appnum].if_handle);
		debug("Close app#%i socket\r\n", appnum);
		curEthIf.apps[appnum].if_handle = 0;
	}
	if(appnum == 1)
	{
		pthread_mutex_lock(&socket_mutex);
	    pthread_cond_broadcast(&socket_cond);
	    pthread_mutex_unlock(&socket_mutex);
	}
	return SUCCESS;
}
