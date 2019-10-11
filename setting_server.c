/*
 * setting_server.c
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "eth_iface.h"
#include "packet_io.h"
#include "avrec_service.h"

#include "setting_server.h"

#define SPECIAL_SSRC			0x0F4565C9
#define MAX_FIELDS_PER_CLASS	20

typedef enum PacketErrorTypes
{
	pacErrSize	= 0,
	pacErrClass,
	pacErrField,
	pacErrReadOnly,
	pacErrUnavail,
	pacErrNeedPass,
	pacErrValue,
	pacErrChange
} PacketErrorTypes;

app_server_interact_struct 		server_descr;
app_server_interact_struct  	av_rec_service_descr;
u8								app_quant;				// количество приложений
u8						   	   *in_packet;
u8 						       *out_packet;

u8 InitServer()
{
	Command         currentCommand;
	app_descriptor *appl 			= &server_descr.app;

	app_quant 						= 0;

	while(1)
	{
		if(EthIfaceInit(appl->number))
		{
			usleep(1000000);
			currentCommand = gblGetCmd();

	        if((currentCommand == FINISH) || (currentCommand == SLEEP))
	        {
	            ERR("Cannot initialize ethernet interface\r\n");
				return FAILURE;
	        }
	        continue;
		}
		else
		{
			break;
		}
	}

	in_packet 	= (u8*) malloc (sizeof(u8) * PACKET_MAX_SIZE);
	out_packet 	= (u8*) malloc (sizeof(u8) * PACKET_MAX_SIZE);
	if((in_packet == NULL)||(out_packet == NULL))
	{
		ERR("Cannot allocate memory for setting server packets\r\n");
		return FAILURE;
	}
	appl->status = sReady;
	return SUCCESS;
}

u8 DestroyServer()
{
	app_descriptor *appl 	= &server_descr.app;
	app_quant 				= 0;

	if(EthIfaceFree(appl->number))
	{
		ERR("Failed to free ethernet interface\r\n");
		return FAILURE;
	}

	if(in_packet != NULL)
	{
		free(in_packet);
	}

	if(out_packet != NULL)
	{
		free(out_packet);
	}

	return SUCCESS;
}

u8 SetAVApplication()
{
    app_descriptor *descr;
		
	descr 				= &av_rec_service_descr.app;
	descr->type 		= appAudioVideoRecord;
	descr->number 		= 1;
	descr->run_status 	= rsOff;
	descr->last_error 	= 0;
	descr->status 		= sReady;

	sprintf(descr->name, "AudioVideoRecordApp");
	app_quant++;
	return SUCCESS;
}

u8 DispatchPacket(u8 *packet, u16 packet_length, packet_info **pi)
{
	u8 	temp8;

	if(packet_length < PACKET_MIN_SIZE)
	{
		ERR("Wrong input setting packet length received\r\n");
		return FAILURE;
	}

	temp8 = packet[0];

	if((temp8 >> 6) != NET_PROT_VERSION)
	{
		ERR("Wrong net protocol version received\r\n");
		return FAILURE;
	}
	if((temp8 & 0x03) >= 2)	// command or command response
	{	
		temp8 = packet[1];
		if(temp8 == ncSet)
		{
			// printf("Inpacket_server:\r\n");
			// int z;
			// for(z = 0; z < packet_length; z++)
			// {
			// 	printf("%02x ", packet[z]);
			// }
			// printf("\r\n");
		}
		if((temp8 != ncGet) && (temp8 != ncSet) && (temp8 != ncErr))
		{
			ERR("Wrong command type received\r\n");
			return FAILURE;
		}
	}
	*pi = (packet_info*)packet;
	return SUCCESS;
}

u8 CheckPacket(packet_info *pi)
{
	if(pi == NULL)
	{
		ERR("There is no packet to check\r\n");
		return FAILURE;
	}
	if((pi->flags & 0x3) != npCommand)
	{
		ERR("There is no command in the packet to check\r\n");
		return FAILURE;
	}
	if((pi->command_type != ncGet) && (pi->command_type != ncSet))
	{
		ERR("There is ERROR packet\r\n");
		return FAILURE;
	}
	return SUCCESS;
}


/*
	pacErrSize	= 0,
	pacErrClass,
	pacErrField,
	pacErrReadOnly
err = 0 - wrong packet length
err = 1 - wrong class type
err = 2 - wrong field num
err = 3 - can't change this field, 
because of - wrong argument length - wrong argumnet value - previledged instruction
(need supervisor password)
*/
u8 CheckSetCmd(packet_info *pi, u8 dlen, u8 class_num, u8 *field_num, u8 pswd, u8*err, sock_info *src)
{
	u8 				in_pos = 1;
	u8 				size;
	u8 				temp;
	app_descriptor *ad;
	session_info   *si;
	
	size = pi->data[in_pos++];	// skip size
	if(size != dlen - 2)
	{
		*err = pacErrSize;
		ERR("Wrong cmd size to be set\r\n");
		return FAILURE;
	}

	if(class_num > MAX_APPS_NUM)
	{
		*err = pacErrClass;
		ERR("Wrong class number to be set\r\n");
		return FAILURE;
	}

	if(class_num == 0)			// server part
	{
		if(!pswd)
		{
			*err = pacErrNeedPass;
			ERR("Theres no password to be set\r\n");
			return FAILURE;
		}

		while(in_pos < dlen)
		{
			*field_num = pi->data[in_pos++];

			if(*field_num != 2)
			{
				if ((*field_num == 0)||(*field_num == 1)||(*field_num == 3))
				{
					*err = pacErrReadOnly;
				}
				else
				{
					*err = pacErrField;
				}
				ERR("Wrong field number to be set\r\n");
				return FAILURE;
			}
			size = pi->data[in_pos++];
			if (size != 1)
			{
				*err = pacErrSize;
				ERR("Wrong cmd size to be set\r\n");
				return FAILURE;
			}
			in_pos++;			// skip data
		}
	}
	else 						// application part
	{
		ad = &av_rec_service_descr.app;
		si = &av_rec_service_descr.session;
		if(ad->status < sReady)
		{
			*field_num 	= parAppDscStatus;
			*err 		= pacErrUnavail;
			ERR("Wrong application status\r\n");
			return FAILURE;
		}
		while(in_pos < dlen)
		{
			*field_num 	= pi->data[in_pos++];
			size 		= pi->data[in_pos++];
			switch(*field_num)
			{
				case parAppDscType:
				case parAppDscAppNumber:
					*err = pacErrReadOnly;
					ERR("Wrong field to be set (read only)\r\n");
					return FAILURE;
				case parAppDscTextID:
					if (!pswd)
					{
						*err = pacErrNeedPass;
						ERR("Wrong password for setting TextID field\r\n");
						return FAILURE;
					}
					if (size != APP_NAME_LENGTH)
					{
						*err = pacErrSize;
						ERR("Wrong length of TextID field\r\n");
						return FAILURE;
					}
					break;
				case parAppDscStatus:
					if (size != 1)
					{
						*err = pacErrSize;
						ERR("Wrong length of Status field\r\n");
						return FAILURE;
					}
					temp = pi->data[in_pos++];
					if((temp == sDisabled) && (!pswd))
					{
						*err = pacErrNeedPass;
						ERR("Wrong password for setting Status field\r\n");
						return FAILURE;
					}
					else if (temp == sCaptured)
					{
						if (ad->status != sReady)
						{
					  		if ((ad->status != sCaptured) || (si->host.addr != (*src).addr) ||
						  		(si->host.port != (*src).port) || (si->host_ssrc != pi->ssrc))
							{
								*err = pacErrChange;
								ERR("Wrong status of setting server0\r\n");
								return FAILURE;
							}
						}
					}
					else if (temp == sReady)
					{
				    	if(!pswd)
				    	{
				    		if (ad->status != sReady)
				    		{
				    			if((ad->status != sCaptured) || (si->host.addr != (*src).addr) ||
				    		 	   (si->host.port != (*src).port) || (si->host_ssrc != pi->ssrc))
					    		{
					  				*err = pacErrChange;
									ERR("Wrong status of setting server1\r\n");
									return FAILURE;
					    		}
					    	}
				    	}
				    }
				    else
				    {
				    	ERR("Wrong status of setting server2\r\n");
						return FAILURE;
					}
					break;
				default:
					ERR("Wrong field type to set\r\n");
					return FAILURE;
			}
		}
	}
	return SUCCESS;
}

u32 GetNewSSRC()
{
	u32 tssrc;
	srand(time(NULL));
	while((tssrc = rand()) == SPECIAL_SSRC);
	return tssrc;
}

u8 ExecuteAction(packet_info *pi, u16 *data_length, packet_info **pi_out, sock_info *src)
{
	u8 							class_num;
	u8 							f_num;
	u8 							field;
	u8							fld_len;
	u16							fs_pos;
	u16							ps_pos;
	app_server_interact_struct *ais;
	app_descriptor 			   *ad;
	session_info 			   *si;
	AppStatus 					sta;

	u8 				is_pswd 	= 0;
	u8 				completed 	= 0;
	u8 				err_t		= 0;
	u32 			own_ssrc 	= 0x12345678;	//GetNewSSRC();
	u16 			dlength 	= *data_length;
	u16 			in_pos 		= 0;
	u16 			out_pos		= 0;
	u16 			fields_size	= 0;
	int 			i 			= 0;
	packet_info    *pitemp 		= (packet_info*)out_packet;
	
	switch(pi->command_type)
	{
		case ncGet:
			pitemp->flags 			= NP_VERSION_BASIC | npCommandAck;
			pitemp->command_type 	= ncGet;
			pitemp->sequence_num 	= pi->sequence_num;
			pitemp->ssrc 			= own_ssrc;

			while(in_pos < dlength)
			{
				class_num 	= pi->data[in_pos++];
				f_num 		= pi->data[in_pos++];
				if (f_num > MAX_FIELDS_PER_CLASS)
				{
					break;
				}

				pitemp->data[out_pos++] = class_num;

				if(class_num > MAX_APPS_NUM)
				{
					pitemp->data[out_pos++]  = 0;
					in_pos 					+= f_num;
				}
				else
				{
				    fields_size = 0;
				    ais 		= (class_num == 0 ? &server_descr : &av_rec_service_descr);
					ad 			= &ais->app;	

					fs_pos 					= out_pos;
					pitemp->data[out_pos++] = 0;

					for(i = 0; i < f_num; i++)
					{
					    fld_len 				= 0;					    
						field 					= pi->data[in_pos++];
						pitemp->data[out_pos++] = field;
						ps_pos 					= out_pos;

						switch(field)
						{
							case parAppDscType:
								pitemp->data[++out_pos] = ((ad->type << 4)|(APP_VERSION & 0xF));
								break;
							case parAppDscAppNumber:
								pitemp->data[++out_pos] = (class_num == 0 ? app_quant : ad->number);
								break;
							case parAppDscTextID:
								memcpy(&pitemp->data[++out_pos], ad->name, 
									AppDescrParameterDescr[field].size);
								break;
							case parAppDscStatus:
								pitemp->data[++out_pos] = ad->status;
								break;
							case parAppDscDevID:
								memcpy(&pitemp->data[++out_pos], dev_type_str, strlen(dev_type_str));
								fld_len = strlen(dev_type_str);
								break;
							default:
								pitemp->data[out_pos++]  = 0; // size
								fields_size 			+= 2;
								continue;
						}
						if(fld_len == 0)
						{
							fld_len = AppDescrParameterDescr[field].size;
						}
						pitemp->data[ps_pos] = fld_len;						
						out_pos 			+= fld_len;
						fields_size 		+= (fld_len + 2);
					}
					pitemp->data[fs_pos] = fields_size;
				}
			}
			if(out_pos > 0)
			{
				*pi_out 		= pitemp;
				*data_length 	= out_pos + 8;
			}
			else
			{
				*data_length 	= 0;
				*pi_out 		= (packet_info*)NULL;
			}
			break;
		//===============================================	
		case ncSet:
			is_pswd 	= (pi->ssrc == SPECIAL_SSRC);
			completed 	= 0;
			err_t		= 0;
			
			do
			{
				pitemp->flags 			= NP_VERSION_BASIC | npCommandAck;
				pitemp->command_type 	= ncSet;
				pitemp->sequence_num 	= pi->sequence_num;
				pitemp->ssrc 			= own_ssrc;
				class_num 				= pi->data[in_pos++];

				in_pos++;	// skip size

				if(CheckSetCmd(pi, dlength, class_num, &field, is_pswd, &err_t, src))
				{
					break;
				}
				ais = (class_num == 0 ? &server_descr : &av_rec_service_descr);
				ad 	= &ais->app;
				si 	= &ais->session;

				while(in_pos < dlength)
				{
					field = pi->data[in_pos++];
					in_pos++;					// skip size

					switch(field)
					{
						case parAppDscTextID:
							memcpy(ad->name, &pi->data[in_pos], AppDescrParameterDescr[field].size);

							in_pos 	   += AppDescrParameterDescr[field].size;
							completed 	= 1;
							break;
						case parAppDscStatus:
							sta = (AppStatus)pi->data[in_pos++];

							if (sta == sCaptured)
							{
								si->host.addr 	= (*src).addr;
								si->host.port 	= (*src).port;
								si->host_ssrc 	= pi->ssrc;
								si->own_ssrc 	= GetNewSSRC();
							}

							ad->status 	= sta;
							completed 	= 1;
							break;
						default:
							ERR("Wrong field type to set\r\n");
							return FAILURE;
					}
				}
			}
			while (0);

			usleep (10000); // let the app socket to get up

			if(completed)
			{
				*data_length 	= 8;
				*pi_out 		= pitemp;
			}
			else 										// composing output packet
			{	
				pitemp->command_type = ncErr;

				switch(err_t)
				{
					case pacErrSize:
						break;
					case pacErrClass:
						pitemp->data[out_pos++] = class_num;
						pitemp->data[out_pos++] = 0;
						break;
					case pacErrField:
						pitemp->data[out_pos++] = class_num;
						pitemp->data[out_pos++] = 2;
						pitemp->data[out_pos++] = field;
						pitemp->data[out_pos++] = 0;
						break;
					case pacErrReadOnly:
						ais 					= (class_num == 0 ? &server_descr : &av_rec_service_descr);
						ad 						= &ais->app;
						si 						= &ais->session;
						pitemp->data[out_pos++] = class_num;
						fs_pos 					= out_pos;
						pitemp->data[out_pos++] = 0;
						pitemp->data[out_pos++] = field;
					    ps_pos 					= out_pos;

						switch(field)
						{
							case parAppDscType:
								pitemp->data[++out_pos] = ad->type;
								break;
							case parAppDscAppNumber:
								pitemp->data[++out_pos] = ad->number;
								break;
							case parAppDscTextID:
								memcpy(&pitemp->data[++out_pos], ad->name, 
									AppDescrParameterDescr[field].size);
								break;
							case parAppDscStatus:
								pitemp->data[++out_pos] = ad->status;
								break;
							default:
								ERR("Wrong field type to set\r\n");
								return FAILURE;
						}					
						pitemp->data[ps_pos] = AppDescrParameterDescr[field].size;
						pitemp->data[fs_pos] = AppDescrParameterDescr[field].size + 1;
						out_pos 			+= AppDescrParameterDescr[field].size;
						break;
					default:
						out_pos = 0;
						break;
				}
				*data_length 	= 8 + out_pos;
				*pi_out 		= pitemp;
			}
			break;
		default:
			ERR("Wrong command type to set\r\n");
			return FAILURE;
	}
	return SUCCESS;
}

u8 DoDataExchange()
{
	// int 			z;
	packet_info    *pinfo;
	packet_info    *poutinfo;
	sock_info 		source;
	int 			len;
	u16 			out_len;
	
	len = EthIfaceRead(server_descr.app.number, PACKET_MAX_SIZE, in_packet, &source);
	if(len < 0)
	{
		return FAILURE;
	}
	if(len > 0)
	{
		// printf("Inpacket:\r\n");
		// for(z = 0; z < len; z++)
		// {
		// 	printf("%02x ", in_packet[z]);
		// }
		// printf("\r\n");

		if(!DispatchPacket(in_packet, len, &pinfo))
		{
			if(!CheckPacket(pinfo))
			{
				out_len = len - 8;
				if(ExecuteAction(pinfo, &out_len, &poutinfo, &source))
				{
					return FAILURE;
				}

				len = EthIfaceWrite(server_descr.app.number, out_len, out_packet, &source);
				if(len != out_len)
				{
					ERR("Failed to send packet from setting server\r\n");
					return FAILURE;
				}
			}
		}
	}
	return SUCCESS;
}


void *settingServerThrFxn(void *arg)
{
	u32 			  dev_addr;
	char 			  str[10];
	Command           currentCommand;
	void      		 *status 			= THREAD_SUCCESS;
    SettingServerEnv *envp   			= (SettingServerEnv *) arg;

	server_descr.app.type 				= appServer;
	server_descr.app.number 			= 0;
	server_descr.app.status 			= sReady;
	server_descr.app.last_error 		= 0;
	
	sprintf(server_descr.app.name, "PA0000");

	// To change device default serial number PA0000 -> PAxxxx
	dev_addr = getDeviceAddr();
	sprintf(str, "%08lu", dev_addr);
	memcpy(&server_descr.app.name[2], &str[4], 4);
	debug("NAME: %s\r\n", server_descr.app.name);

	EthIfaceCreate();

	if(InitServer())
    {
    	ERR("Failed to init setting server\r\n");
        //logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    envp->app_descr = &av_rec_service_descr;
    envp->own_descr = &server_descr;

	SetAVApplication();

	/* Signal that initialization is done */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

	while (1)
    {
    	currentCommand = gblGetCmd();

        if((currentCommand == FINISH) || (currentCommand == SLEEP) /*|| (stop_netconnect == 1)*/)
        {
            debug("Setting server thread finishing ...\r\n");
            goto cleanup;
        }
      
    	DoDataExchange();
		usleep(20000);
    }

    cleanup:

    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }

    if(envp->hRendezvousFinish != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinish);
    }

    if(DestroyServer())
    {
    	ERR("Failed to destroy setting server\r\n");
    }
    debug("Setting server thread finished\r\n");
    return status;
}
