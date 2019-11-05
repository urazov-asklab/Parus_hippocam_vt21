/*
 * packet_io.c
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include "avrec_service.h"

#include "packet_io.h"

#define	IF_RELEASE_PERIOD	10000000		// us
#define MAX_CLASS_COUNT		4
#define NP_ACKREQ_MASK		(1 << 2)

const char	dev_type_str[] = "Parus-VT20-v1.00";

const AppParameterDescr AppInterParameterDescr[parAppIntCount] = {
	{1, 1},
	{1, 0},
	{1, 0}};

const AppParameterDescr AppSyncParameterDescr[parAppSynCount] = {
	{4, 0},
	{2, 0},
	{4, 0},
	{2, 0}};

const AppParameterDescr AppDescrParameterDescr[parAppDscCount] = {
	{1, 				0},
	{1, 				0},
	{APP_NAME_LENGTH, 	0},
	{1, 				0},
	{0, 				0}};		// length = 0, can be different

typedef enum AppInterStatusTypes
{
	isSetUnavailable = 0,
	isSetAvailable
} AppInterStatusTypes;

void PacketIOInit(PacketIO *comm)
{
	comm->app_serv 			= NULL;
	comm->cfg_table 		= NULL;
	comm->field_storage_pos = 0;
	comm->isAppOwnerExists 	= 0;
	comm->LastReadTimer 	= 0;
}

void PacketIOSetAppStruct(PacketIO *comm, app_server_interact_struct *as)
{
	comm->app_serv = as;
	memcpy(&comm->owner_status, &comm->app_serv->app.status, sizeof(AppStatus));
}

void PacketIOSetOwner(PacketIO *comm, u8 flag)
{
	comm->isAppOwnerExists = flag;
}

//выделяем место под таблицу параметров аудио/видио записи
int PacketIOSetParamTable(PacketIO *comm, AppCfgParameterTable *ptr, int num_items)
{
	comm->cfg_table = (AppCfgParameterTable*) malloc (sizeof(AppCfgParameterTable) * num_items);
	if(comm->cfg_table == NULL)
	{
		ERR("Cannot allocate memory for comm->cfg_table\r\n");
		return FAILURE;
	}
	memcpy(comm->cfg_table, ptr, sizeof(AppCfgParameterTable) * num_items);
	comm->cfg_table_count 	= num_items;
	return SUCCESS;
}


// проверяем - захвачено ли приложение установки настроек или свободно(готово к захвату)
// если в течение 10 сек. не велось обмена информацией - захват снимаем
void CheckReleaseTime(PacketIO *comm)
{
	u8 				changed 	= 0;
    u8 				is_captured = 0;
    struct timeval  read_time;
    struct timeval  cur_time;
    u64    			diff;

   	if(comm->app_serv->app.status == sCaptured)
	{   
    	is_captured = 1;
	}
	// else
	// {
	// 	debug("NOT CAPTURED\r\n");
	// }

	if(!is_captured)
	{
		gettimeofday(&read_time, NULL); 
    	comm->LastReadTimer = ((u64)read_time.tv_sec * (u64)US_IN_SEC + (u64)read_time.tv_usec);
    	return;
	}

	gettimeofday(&cur_time, NULL); 
	diff = ((u64)cur_time.tv_sec * (u64)US_IN_SEC + (u64)cur_time.tv_usec) - comm->LastReadTimer;
	// debug("DIFF PERIOD: %llu us\r\n", diff);
    if((diff < IF_RELEASE_PERIOD) /*&& (set_wor_mode == 0)*/)
    {
    	return;
    }
    gettimeofday(&read_time, NULL); 
    comm->LastReadTimer = ((u64)read_time.tv_sec * (u64)US_IN_SEC + (u64)read_time.tv_usec);

    //free captured interfaces
    changed = 0;
    if(comm->app_serv->app.status == sCaptured)
    {
    	debug("====READY\r\n");
    	comm->app_serv->app.status = sReady;
    	changed = 1;
    }
}


int CompareStatuses(PacketIO *comm)
{
	u8 			changed;

	if((!comm->isAppOwnerExists)||(!comm->app_serv))
	{
		return FAILURE;
	}

	changed = 0;

	if(comm->owner_status != comm->app_serv->app.status)
	{	
		changed = 1;
	}

	if(changed == 0)
	{
		return FAILURE;
	}
	return SUCCESS;
}

int ReadPacket(app_server_interact_struct *ad, u16 length, u8 *buffer, sock_info *src)
{
	// int z;
	u32 ssrc;
	u8 	flags;
	int r = EthIfaceRead(ad->app.number, length, buffer, src);

    if (r > 0)
    {
		// printf("Inpacket_app:\r\n");
		// for(z = 0; z < r; z++)
		// {
		// 	printf("%02x ", buffer[z]);
		// }
		// printf("\r\n");

		flags = buffer[0];
        if ((flags & NP_VERSION_MASK) == NP_VERSION_BASIC)
		{
			if (r < PACKET_MIN_SIZE)
			{
				return 0;
			}
			memcpy(&ssrc, &buffer[4], 4);
		    if ((ad->session.host_ssrc != ssrc) || (src->addr != ad->session.host.addr) || 
		    	(src->port != ad->session.host.port))
		    {
		    	return 0;
		    }
		}
    }
	return r;
}

u8 PacketStructClear(packet_field *params, int count)
{
	int i;

	for(i = 0; i < count; i++)
	{
		memset(&params[i], 0, sizeof(packet_field));
		params[i].fInfo = pfNoField;
	}
	return SUCCESS;
}

u8 PacketCheckHeader(const u8 *packet, int size, packet_info2 *pi)
{
	u8 flags;
	u8 cd;

	if (size < PACKET_MIN_SIZE)
	{
		ERR("The received packet is too small\r\n");
		return FAILURE;
	}
	
	flags 	= packet[0];
	cd 		= packet[1];
	switch (flags & NP_VERSION_MASK)
	{
		case NP_VERSION_BASIC:
			/*
			if(((flags & NP_TYPE_MASK) == npCommandAck) && (cd != ncGet) && (cd != ncSet) 
			&& (cd != ncErr))
			{
				return FAILURE;
			}
			*/
			if((flags & NP_TYPE_MASK) == npCommandAck) 
			{
				ERR("Failed header\r\n");
				return FAILURE;
			}
			if(((flags & NP_TYPE_MASK) == npCommand) && (cd != ncGet) && (cd != ncSet))
			{
				ERR("Failed header\r\n");
				return FAILURE;
			}
			memcpy(pi, packet, 8);
			pi->data 		= &packet[8];
			pi->data_size 	= size - 8;
			break;
		case NP_VERSION_SHORT:
			if(((flags & NP_TYPE_MASK) != npDataAck) && ((flags & NP_TYPE_MASK) != npData))
			{
				ERR("Failed header\r\n");
				return FAILURE;
			}
			pi->flags 			= flags;
			pi->sequence_num 	= packet[1] + (packet[2]<<8);
			pi->data 			= &packet[3];
			pi->data_size 		= size - 3;
			break;
		default:
			ERR("Failed header\r\n");
			return FAILURE;
	}
	return SUCCESS;
}

u8 PacketParse(packet_info2 *pi, packet_field *params, int *parCount, const u8 *buffer, int size)
{
	u16 inPos 	= 0;
	u16 pPos 	= 0;
	u8 	cType;
	u8 	cSize;
	u8 	s;
	u16 maxPos;

	if (PacketCheckHeader(buffer, size, pi))
	{
		return FAILURE;
	}
	switch (pi->flags & NP_TYPE_MASK)
	{
		case npCommand:
			while(inPos < pi->data_size)
			{
				cType 	= pi->data[inPos++];
				cSize 	= pi->data[inPos++];
				maxPos 	= inPos + cSize;
				if (pi->data_size < maxPos)
				{
					ERR("Packet data size is invalid\r\n");
					return FAILURE;
				}
				while((inPos < maxPos) && (pPos < *parCount))
				{
					params[pPos].fInfo 	= 0;
					params[pPos].fClass = cType;
					params[pPos].fType 	= pi->data[inPos++];

					if (pi->command_type == ncSet)
					{
					    s = pi->data[inPos++];					    
					    if((inPos + s > maxPos) ||((s == 0)))
					    {
					    	pPos++;
					    	break;
					    }
						params[pPos].fSize = s;
						params[pPos].fData = (u8*)&pi->data[inPos];
						inPos 			  += s;
					}
					pPos ++;
				}
				if (pPos == *parCount)
				{
					break;
				}
				if (inPos != maxPos)
				{
					break;
				}
			}
			*parCount = pPos;
			return SUCCESS;
		case npData:		// не исползуем сейчас
		case npDataAck:		// не исползуем сейчас
			*parCount = 0;
			return SUCCESS;
	}
	// device can't receive commands ack's yet
	ERR("Cannot parse packet: invalid packet type\r\n");
	return FAILURE;
}

// найти номер определенного параметра в таблице
int	GetPosInCfgTable(PacketIO *comm, u8 fType)
{
	int i;

	if(!comm->cfg_table)
	{
		WARN("PacketIO doesn't have cfg_table\r\n");
		return FAILURE;
	}

	for(i = 0; i < comm->cfg_table_count; i++)
	{
		if(comm->cfg_table[i].field_num == fType)
		{
			return i;
		}
	}
	// WARN("Field with such num %02x doesn't exist\r\n", fType);
	return FAILURE;
}


// проверить корректность всех указанных в пакете параметров
u8 CheckAllParams(PacketIO *comm, app_descriptor *ad, int srcCount)
{
	int class_num 	= -1;
	int i;
	int pos;

	switch(comm->p_inf.command_type)
	{		
		case ncSet:
		{
			for(i = 0; i < srcCount; i++)
			{
				if(class_num < 0)
				{
					class_num = comm->src_fld[i].fClass;
				}
				else
				{
					if(class_num != comm->src_fld[i].fClass)
					{
						ERR("Wrong class number in the packet\r\n");
						return FAILURE;
					}
				}

				switch(comm->src_fld[i].fClass)
				{
					case clsAppDescr: 	// Descriptor of service
						if(comm->src_fld[i].fType >= parAppDscCount)
						{
							continue;
						}
						if((comm->src_fld[i].fSize == AppDescrParameterDescr[comm->src_fld[i].fType].size) &&
						   (AppDescrParameterDescr[comm->src_fld[i].fType].flags == apdWritable))
						{
						   	continue;
						}
						ERR("Invalid class clsAppDescr in the packet\r\n");
						return FAILURE;
					case clsAppSync: 	// Information about synchronization
						if(comm->src_fld[i].fType >= parAppSynCount)
						{
							continue;
						}
						if((comm->src_fld[i].fSize == AppSyncParameterDescr[comm->src_fld[i].fType].size) &&
						   (AppSyncParameterDescr[comm->src_fld[i].fType].flags == apdWritable))
						{
						   	continue;
						}
						ERR("Invalid class clsAppSync in the packet\r\n");
						return FAILURE;
					case clsAppInter:	// General service management
						if(comm->src_fld[i].fType >= parAppIntCount)
						{
							continue;
						}
						if((comm->src_fld[i].fSize == AppInterParameterDescr[comm->src_fld[i].fType].size) &&
						   (AppInterParameterDescr[comm->src_fld[i].fType].flags == apdWritable))
						{
						   		continue;
						}
						ERR("Invalid class clsAppInter in the packet\r\n");
						return FAILURE;
					case clsAppCfg:		// Specific service configuration
						if(comm->cfg_table)
						{
							pos = GetPosInCfgTable(comm, comm->src_fld[i].fType);
							if(pos < 0)
							{
								continue;		// field with such num doesn't exist
							}
							if(comm->cfg_table[pos].access_flag == apdReadableOnly)
							{
								ERR("Cannot update read-only field #%i of clsAppCfg class\r\n", comm->src_fld[i].fType);
								return FAILURE;
							}
							if(comm->cfg_table[pos].size != comm->src_fld[i].fSize)
							{
								ERR("Invalid size of field #%i of clsAppCfg class\r\n", comm->src_fld[i].fType);
								return FAILURE;
							}
							if(comm->isAppOwnerExists)
							{
								if(AVRecParamFunc(apdParamCheck, comm->src_fld[i].fType, comm->src_fld[i].fData))
								{
									return FAILURE;
								}
							}
						}
						continue;
					default:
						continue;
				}
			}
		}
		case ncGet:		// we will process any get request without error throwing
			break;
		default:
			ERR("Failed command type \r\n");
			return FAILURE;
	}
	return SUCCESS;
}

u8 ProcessAllParams(PacketIO *comm, app_descriptor *ad, int srcCount, int *dstCount)
{
	int 			pos;
	int 			i;
	packet_field   *field;
	time_t          seconds;
    struct tm      	timeinfo;

	*dstCount 				= 0;
	comm->field_storage_pos = 0;

	switch(comm->p_inf.command_type)
	{		
		case ncGet:	// если чтение - собираем параметры в структуру
		{
			for(i = 0; i < srcCount; i++, (*dstCount)++)
			{
				field 			= &comm->dst_fld[*dstCount];
				field->fInfo 	= 0;
				field->fClass 	= comm->src_fld[i].fClass;
				field->fType 	= comm->src_fld[i].fType;
				switch(comm->src_fld[i].fClass)
				{
					case clsAppDescr:
						switch (comm->src_fld[i].fType)
						{
							case parAppDscType:
								comm->field_storage[comm->field_storage_pos]	
															= (ad->type << 4) | (APP_VERSION & 0xF);
								field->fData 							
															= &comm->field_storage[comm->field_storage_pos];								
								field->fSize 				= 1;
								comm->field_storage_pos    += field->fSize;
								// field->fData 						= (u8*)&ad->type;
								break;
							case parAppDscAppNumber:
								field->fData = (u8*)&ad->number;
								field->fSize = 1;
								break;
							case parAppDscTextID:
								field->fData = (u8*)&ad->name;
								field->fSize = APP_NAME_LENGTH;
								break;
							case parAppDscStatus:
								field->fData = (u8*)&ad->status;
								field->fSize = 1;
								break;
							case parAppDscDevID:
								field->fData = (u8*)dev_type_str;
								field->fSize = strlen(dev_type_str);
								break;
							default:
								field->fSize = 0;
								break; // write free field
						}
						break;
					case clsAppSync:
						switch (comm->src_fld[i].fType)
						{
							case parAppSynTSValue:								
								field->fData = (u8*)&ad->sync.ts_value;
								field->fSize = 4;
								break;
							case parAppSynTSFreq:
								field->fData = (u8*)&ad->sync.ts_freq;
								field->fSize = 2;
								break;
							case parAppSynTUValue:
								field->fData = (u8*)&ad->sync.tu_value;
								field->fSize = 4;
								break;
							case parAppSynTUFreq:
								field->fData = (u8*)&ad->sync.tu_freq;
								field->fSize = 2;
								break;
							default:
								field->fSize = 0;
								break; // write free field
						}
						break;
					case clsAppInter:
						switch (comm->src_fld[i].fType)
						{
							case parAppIntSetAvail:
								comm->field_storage[comm->field_storage_pos] 	
															= isSetAvailable;
								field->fData 				= &comm->field_storage[comm->field_storage_pos];
								field->fSize 				= 1;
								comm->field_storage_pos    += field->fSize;
								// field->fInfo 					   |= pfCalcField;
								break;
							case parAppIntRunStatus:
								field->fData = (u8*)&ad->run_status;
								field->fSize = 1;
								break;
							case parAppIntLastError:
								field->fData = (u8*)&ad->last_error;
								field->fSize = 1;
								break;
							default:
								field->fSize = 0;
								break;
						}
						break;
					case clsAppCfg:
						if(!comm->cfg_table)
						{
							field->fSize 	= 0;
							field->fInfo   |= pfNoField;
							break;
						}
						else
						{
							pos = GetPosInCfgTable(comm, comm->src_fld[i].fType);
							if(pos < 0)
							{ 	// field with such num doesn't exist
								field->fSize = 0;
								break;
							}
							field->fSize = comm->cfg_table[pos].size;
							if(comm->src_fld[i].fType == 0x5F)
							{
									seconds  = time(NULL);
								    timeinfo = *localtime(&seconds);

									comm->cfg_table[pos].data[0] = to_bcd(timeinfo.tm_sec);
									comm->cfg_table[pos].data[1] = to_bcd(timeinfo.tm_min);
									comm->cfg_table[pos].data[2] = to_bcd(timeinfo.tm_hour);
									comm->cfg_table[pos].data[3] = to_bcd(timeinfo.tm_mday);
									comm->cfg_table[pos].data[4] = to_bcd(timeinfo.tm_mon + 1);
									comm->cfg_table[pos].data[5] = to_bcd(timeinfo.tm_year % 100);
							}
							field->fData = comm->cfg_table[pos].data;
							if(comm->cfg_table[pos].access_flag & apdCountable)
							{
								if(AVRecParamFunc(apdParamCount, comm->src_fld[i].fType, comm->src_fld[i].fData))
								{
								    field->fSize = 0;
									break;
								}
							}
						}
						break;
					default:
						field->fInfo   |= pfNoField;
						field->fSize 	= 0;
						break;
				}
			}
			break;
		}
		case ncSet: // если запись, то сохраняем параметры в память, либо выполняем действия
		{
			for(i = 0; i < srcCount; i++)
			{
				switch(comm->src_fld[i].fClass)
				{
					case clsAppInter:
						switch(comm->src_fld[i].fType)
						{
							case parAppIntRunStatus:
								ad->run_status = (RunStatus)comm->src_fld[i].fData[0];
								break;
							default:
								break;
						}
						break;
					case clsAppCfg:
						if(!comm->cfg_table)
						{
							break;
						}
						else
						{
							pos = GetPosInCfgTable(comm, comm->src_fld[i].fType);
							if(pos < 0)
							{
								break;
							}
							if(comm->isAppOwnerExists)
							{
								// сохраняем параметры в память, либо выполняем действия
								AVRecParamFunc(apdParamSet, comm->src_fld[i].fType, comm->src_fld[i].fData);								
							}
						}
						break;
					default:
						break;
				}
			}
		}		
		break;
		default:
			ERR("Failed command type\r\n");
			return FAILURE;
	}
	return SUCCESS;
}

u8 PacketProcessParams(PacketIO *comm, int srcCount, int *dstCount)
{
	app_descriptor *ad;
	ad 					= &comm->app_serv->app;

	comm->d_inf.flags 			= (comm->p_inf.flags & ~NP_TYPE_MASK) | npCommandAck;
	comm->d_inf.command_type 	= comm->p_inf.command_type;
	comm->d_inf.sequence_num 	= comm->p_inf.sequence_num;
	comm->d_inf.ssrc 			= comm->app_serv->session.own_ssrc;		// comm->p_inf.ssrc;

	// 1 - check part
	if(CheckAllParams(comm, ad, srcCount))
	{
		comm->d_inf.command_type 	= ncErr;
		comm->d_inf.data_size 		= 0;
		*dstCount 					= 0;
		return FAILURE;
	}
	PacketStructClear(comm->dst_fld, MAX_FIELDS_PER_PACKET);

	// 2 - process part
	if(ProcessAllParams(comm, ad, srcCount, dstCount))
	{
		comm->d_inf.command_type 	= ncErr;
		comm->d_inf.data_size 		= 0;
		*dstCount 					= 0;
		return FAILURE;
	}
	return SUCCESS;
}

u16 PacketCalcDataSize(const packet_field *params, int parCount, u8 *cSizes, u8 *cCount, u8*types)
{
	u8 	cType;
	u8 	i;
	u8 	j;
	u16 fSize 	= 0;
	u8 	tPos 	= 0;
	u8 	inc 	= 0;

	memset(types, 0xFF, MAX_FIELDS_PER_PACKET);
	memset(cSizes, 0, MAX_FIELDS_PER_PACKET);

	*cCount = 0;

	for (i = 0; i < parCount; i++)
	{
		cType = params[i].fClass;
		for(j = 0; j <= *cCount; j++)
		{
		    if(types[j] == 0xFF)
		    {
		        tPos 	= j;
		        inc 	= 1;
		        break;
		    }
		    if(types[j] == cType)
		    {
		        if(cSizes[j] + (2 + params[i].fSize) <= 255)
		        {
		            tPos = j;
		            break;		            
		        }
		    }
		}
		if(inc)
		{
			(*cCount)++;
			types[tPos] = cType;
			inc 		= 0;
		}
		if (cType < MAX_CLASS_COUNT)
		{
		    cSizes[tPos] += (2 + params[i].fSize);
		}
		fSize += (2 + params[i].fSize);
	}
	fSize += *cCount * 2;
	return fSize;
}

u8 PacketWriteBasic(const packet_info2 *pi, packet_field *params, int parCount, u8 *buffer, 
	int *size)
{
	// mask for every possible class(256 can be written in 1 byte)
	u8 cTypes[MAX_FIELDS_PER_PACKET];
	u8 cSizes[MAX_FIELDS_PER_PACKET];

	u16 outPos;
	u8 	sub_size;
	u8 	cType;
	u8 	cSize;
	u8 	i;
	u8 	s;

	u8 	cCount 	= 0;
	u8 	count 	= 0;	
	u16 fSize 	= PacketCalcDataSize(params, parCount, cSizes, &cCount, cTypes);

	if (*size < fSize + 8)
	{
		ERR("Wrong size of packet to be assemble\r\n");
		return FAILURE;
	}
	memcpy(buffer, pi, 8);
	
	outPos = 8;
		
	while(count < cCount)
	{
		cType = cTypes[count];
		if(cType == 0xFF)
		{
			ERR("Wrong parameter type in the packet to be assemble\r\n");
			return FAILURE;
		}

		buffer[outPos++] = cType;
		
		if(cType < MAX_CLASS_COUNT)
		{
			cSize = cSizes[count];
		}
		else
		{
			cSize = 0;
		}
		buffer[outPos++] = cSize;
		if(cSize > 0)
		{
		    sub_size = 0;
			for (i = 0; i < parCount; i++)
			{
				if(((u8)cType == params[i].fClass) && ((sub_size + params[i].fSize) <= 255))
				{
					buffer[outPos++] 	= params[i].fType;
					s 					= params[i].fSize;
					buffer[outPos++] 	= s;
					if (s > 0)
				  	{
				  		memcpy(&buffer[outPos], params[i].fData, s);
				  		outPos += s;
				  	}
				  	sub_size	    += (2 + s);
				  	// not to be parsed in next loop, because of class division
				  	params[i].fClass = 0xFF;
				}
				if(sub_size == cSize)
				{
					break;
				}
			}			
		}
		count++;
	}
	*size = outPos;
	return SUCCESS;
}

// собираем пакет в ответ на полученное сообщение
u8 PacketAssemble(const packet_info2 *pi, packet_field *params, int parCount, u8 *buffer, int *size)
{
	switch (pi->flags & NP_VERSION_MASK)
	{
		case NP_VERSION_BASIC:
			switch (pi->flags & NP_TYPE_MASK)
			{
				case npCommandAck:
					if ((pi->command_type == ncSet) || (pi->command_type == ncErr))
					{
						if (*size < 8)
						{
							ERR("Packet size is less than available\r\n");
							return FAILURE;
						}
						memcpy(buffer, pi, 8);
						*size = 8;
						return SUCCESS;
					}
					if (pi->command_type == ncGet)
					{
						return PacketWriteBasic(pi, params, parCount, buffer, size);
					}
					ERR("Wrong command type in the packet to be assemble\r\n");
					return FAILURE;
				case npDataAck:
					if (*size < 8)
					{
						ERR("Packet size is less than available\r\n");
						return FAILURE;
					}
					buffer[0] = pi->flags;
					buffer[1] = 0;
					buffer[2] = pi->sequence_num & 0xFF;
					buffer[3] = (pi->sequence_num >> 8) & 0xFF;
					memcpy(&buffer[4], &pi->ssrc, 4);
					*size = 8;
				case npData:	// data must be sent through async method
				default:
					ERR("Wrong type of the packet with acknoledge to be assemble\r\n");
			}
		case NP_VERSION_SHORT:
			switch (pi->flags & NP_TYPE_MASK)
			{
				case npDataAck:
					if (*size < 3)
					{
						ERR("Packet size is less than available\r\n");
						return FAILURE;
					}
					buffer[0] 	= pi->flags;
					buffer[1] 	= pi->sequence_num & 0xFF;
					buffer[2] 	= (pi->sequence_num >> 8) & 0xFF;
					*size 		= 3;
					return SUCCESS;
				case npData:	// data must be sent through async method
					/*
					if (*size < pi->data_size+3)
					{
						ERR("Packet size is less than available\r\n");
						return FAILURE;
					}
					buffer[0] = pi->flags;
					buffer[1] = pi->sequence_num & 0xFF;
					buffer[2] = (pi->sequence_num >> 8) & 0xFF;
					memcpy(&buffer[3], pi->data, pi->data_size);
					*size = pi->data_size+3;
					return SUCCESS;
					*/
				default:
					ERR("Wrong type of the packet with data to be assemble\r\n");
					return FAILURE;
			}
		default:
			ERR("Wrong packet version \r\n");
			return FAILURE;
	}
}

u8 ReactOnPacket(PacketIO *comm, int parCount)
{
	int size 	= PACKET_MAX_SIZE;
	int dst_cnt = 0;
	u8 	fl;

	switch(comm->p_inf.flags & NP_TYPE_MASK)
	{
		case npCommand:
			PacketProcessParams(comm, parCount, &dst_cnt);
			if(PacketAssemble(&comm->d_inf, comm->dst_fld, dst_cnt, comm->packet, &size))
			{
				return FAILURE;
			}
			if(comm->app_serv->app.status == sCaptured)
			{
				if(size != EthIfaceWrite(comm->app_serv->app.number, size, comm->packet, 
					&comm->cur_socket))
				{
					return FAILURE;
				}
			}
			else
			{
				ERR("AVRec service is not captured \r\n");
				return FAILURE;
			}
			break;
		case npData: // если посылаем данные по wifi (не используется сейчас)
			fl = comm->p_inf.flags;
			if(comm->isAppOwnerExists)
			{
				AVRecDataFunc(&comm->p_inf); // (!!!) функция не реализована
			}
			if ((fl & NP_ACKREQ_MASK) > 0)
			{
				comm->p_inf.flags 	= (comm->p_inf.flags & ~(NP_TYPE_MASK | NP_ACKREQ_MASK)) 
										| npDataAck;
				comm->p_inf.ssrc 	= comm->app_serv->session.own_ssrc;
				if(PacketAssemble(&comm->p_inf, comm->src_fld, parCount, comm->packet, &size))
				{
					return FAILURE;
				}
				if(comm->app_serv->app.status == sCaptured)
				{
					if(size != EthIfaceWrite(comm->app_serv->app.number, size, comm->packet, 
						&comm->cur_socket))
					{
						return FAILURE;
					}
				}
				else
				{
					ERR("AVRec service is not captured \r\n");
					return FAILURE;
				}
			}
			break;
		case npDataAck:
			ERR("The request is not implemented in this hardware version\r\n");
			return FAILURE;
		default:
			ERR("Wrong packet type \r\n");
			return FAILURE;
	}
	return SUCCESS;
}

u8 PacketIOProcessInterfaces(PacketIO *comm)
{
	int 			paramCnt;
	int 			result;
	struct timeval  read_time;

	if(comm->app_serv == NULL)
	{
		ERR("Pointer app_serv is NULL\r\n");
		return FAILURE;
	}
	
	CheckReleaseTime(comm);
	CompareStatuses(comm);	

	if(comm->app_serv->app.status == sCaptured)
	{
		if(EthIfaceInit(comm->app_serv->app.number))
		{
			return FAILURE;
		}
	}
	else
	{
		if(EthIfaceFree(comm->app_serv->app.number)) // если захват приложения снят, то освобождаем сокет
		{
			ERR("Failed to free ethernet interface\r\n");
			return FAILURE;
		}
		return SUCCESS;	 	
	}

   	result = ReadPacket(comm->app_serv, PACKET_MAX_SIZE, comm->packet, &comm->cur_socket);

	if(result < 0)
	{
		//ERR("Failed to read packet from port %lu\r\n", comm->cur_socket.port);
		return FAILURE;
	}
	if(result == 0)
	{
		return SUCCESS;
	}
	
	gettimeofday(&read_time, NULL); 
    comm->LastReadTimer = ((u64)read_time.tv_sec * (u64)US_IN_SEC + (u64)read_time.tv_usec);

	PacketStructClear(comm->src_fld, MAX_FIELDS_PER_PACKET);
	
	paramCnt = MAX_FIELDS_PER_PACKET;
	if(PacketParse(&comm->p_inf, comm->src_fld, &paramCnt, comm->packet, result))
	{
		return SUCCESS;
	}
			
	if(ReactOnPacket(comm, paramCnt))
	{
		return FAILURE;
	}				
	return SUCCESS;
}