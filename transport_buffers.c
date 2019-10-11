//transport_buffers.c

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/BufTab.h>

#include "common.h"

#include "transport_buffers.h"

// для каждой группы буферов хранятся флаги, которые показывают, какие читатели к ней подключились
u32			TBufReadersMask[MAX_BUF_GROUP_NUM]; 
// для каждой группы - количество кадров/аудиочанков прошедших через группу буферов
u32 		TBufFrameNum[MAX_BUF_GROUP_NUM];	
u32 		TBufCount[MAX_BUF_GROUP_NUM];		// для каждой группы - количество буферов в группе
u32 		TBufFullCount[MAX_BUF_GROUP_NUM];	// для каждой группы - количество занятых буферов в группе
buffer_dsc	TransportBuffersGroup[MAX_BUF_GROUP_NUM][TBUF_NUMBER]; // массив групп буферов
// информация о каждом читателе для каждой группы (занят он или свободен, флаги - зарезервированы на будущее)
reader_info	bufReaders[MAX_BUF_GROUP_NUM][MAX_READERS_NUM];

//extern u8 IsWriterTerminated;

void InitTransportBuffers()
{
	memset(bufReaders, 0, sizeof(bufReaders));
	memset(TBufReadersMask, 0, sizeof(TBufReadersMask));
	memset(TBufFrameNum, 0, sizeof(TBufFrameNum));
	memset(TBufFullCount, 0, sizeof(TBufFullCount));
	return;
}

u8 RegisterTransportBufGroup(int grp_num, BufTab_Handle hBufTab)
{
	int i;
	int n;

	memset(&(TransportBuffersGroup[grp_num][0]), 0, sizeof(buffer_dsc) * TBUF_NUMBER);

	n = BufTab_getNumBufs(hBufTab);
	for(i = 0; i < n; i++)
	{
		TransportBuffersGroup[grp_num][i].hBuf 		= BufTab_getBuf(hBufTab, i);
		TransportBuffersGroup[grp_num][i].status 	= eBUF_FREE;
	}

	TBufFrameNum[grp_num] 	= 0;
	TBufFullCount[grp_num] 	= 0;
	TBufCount[grp_num]    	= n;

	return SUCCESS;
}

// добавить буферы в группу после основной регистрации буферов
u8 AddBuffers(int grp_num, BufTab_Handle hBufTab)
{
	int i;
	int n;
	int c = TBufCount[grp_num];

	n = BufTab_getNumBufs(hBufTab);
	for(i = 0; i < n; i++)
	{
		TransportBuffersGroup[grp_num][c + i].hBuf 		= BufTab_getBuf(hBufTab, i);
		TransportBuffersGroup[grp_num][c + i].status 	= eBUF_FREE;
	}
	TBufFrameNum[grp_num]  = 0;
	TBufCount[grp_num]    += n;
	return SUCCESS;
}


void SetBufferReady(int grp_num, Buffer_Handle hBuf, u64 timestamp, u8 keyF)
{
 	int i 		= 0;
 	u32 size 	= Buffer_getNumBytesUsed(hBuf);

 	if(grp_num == eVENCODERRF_SRC)
 	{
 		if(size%4 != 0)
 		{
 			size += (4 - size%4);
 			Buffer_setNumBytesUsed(hBuf, size);
 		}
 	}

	if(size == 0)
	{
		ERR("Out of memory error!!! g - %i\r\n", grp_num);
		return;
	}
	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if(TransportBuffersGroup[grp_num][i].hBuf == hBuf)
		{
			break;
		}
	}
	if(i >= TBUF_NUMBER)
	{
		ERR("Buffer is not exist!!! g - %i\r\n", grp_num);
		return;
	}

	if(TransportBuffersGroup[grp_num][i].status != eBUF_WRITING)
	{
		ERR("Invalid buffer status!!! g-%i\r\n", grp_num );
		return;
	}
	// if(IsWriterTerminated)
	// {
	//     TransportBuffersGroup[grp_num][i].status = eBUF_FREE;
	//     return;
	// }
	pthread_mutex_lock(&buf_mutex[grp_num]);

	TransportBuffersGroup[grp_num][i].properties 			= (keyF == 1) ? isKey : noKey;
	TransportBuffersGroup[grp_num][i].timestamp 			= timestamp; 		// in ms
	TransportBuffersGroup[grp_num][i].clients 				= TBufReadersMask[grp_num];
	TransportBuffersGroup[grp_num][i].data_size 			= size;
	TransportBuffersGroup[grp_num][i].status 				= eBUF_READY;	
	TransportBuffersGroup[grp_num][i].number 				= TBufFrameNum[grp_num]++;
	TransportBuffersGroup[grp_num][i].number_of_reading 	= 0;				// must be zero - not necessary

	TBufFullCount[grp_num]++;

	pthread_mutex_lock(&rcond_mutex[grp_num]);
	pthread_cond_broadcast(&rbuf_cond[grp_num]);
	pthread_mutex_unlock(&rcond_mutex[grp_num]);

	pthread_mutex_unlock(&buf_mutex[grp_num]);
}


void GetBufferToWrite(int grp_num, Buffer_Handle *hBuf, int getanyway)
{
	int 			i;
	u32 			number 	= 0xFFFFFFFF;
	int 			bn 		= -1;

	pthread_mutex_lock(&buf_mutex[grp_num]);

	*hBuf 	= NULL;

	// сначала ищем свободный или никому не нужный буфер

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if( (TransportBuffersGroup[grp_num][i].status == eBUF_FREE) 						||

			((TransportBuffersGroup[grp_num][i].status == eBUF_READY)   					&& 
			((TransportBuffersGroup[grp_num][i].clients & TBufReadersMask[grp_num]) == 0))
			)
		{
			if(TransportBuffersGroup[grp_num][i].status == eBUF_READY)
			{
				TBufFullCount[grp_num]--;
			}

			TransportBuffersGroup[grp_num][i].status 	= eBUF_WRITING;
			*hBuf										= TransportBuffersGroup[grp_num][i].hBuf;

			pthread_mutex_unlock(&buf_mutex[grp_num]);
			return;
		}
	}

	// если нет и ищем для сжатого аудио или видео, то сгодится самый старый ещё не прочитанный
	
	if((grp_num == eVENCODER_SRC) || (grp_num == eAENCODER_SRC))
	{
		for(i = 0; i < TBUF_NUMBER; i++)
		{
			if((TransportBuffersGroup[grp_num][i].clients == (1 << vcStreamB_trd)) &&
				((TransportBuffersGroup[grp_num][i].status == eBUF_READY) /*|| 
				(TransportBuffersGroup[grp_num][i].status == eBUF_READING)*/))
			{
				if(TransportBuffersGroup[grp_num][i].number < number)
				{
					number 	= TransportBuffersGroup[grp_num][i].number;
					bn 		= i;
				}
			}
		}
	}

	// если всё равно нет, а есть флаг getanyway, то опять ищем самый старый ещё не прочитанный

	if(getanyway)
	{
		debug("GETANYWAY\r\n");
		for(i = 0; i < TBUF_NUMBER; i++)
		{
			if(TransportBuffersGroup[grp_num][i].status == eBUF_READY)
			{
				if(TransportBuffersGroup[grp_num][i].number < number)
				{
					number 	= TransportBuffersGroup[grp_num][i].number;
					bn 		= i;
				}
			}
		}
	}

	if(bn >= 0)
	{
		if(TransportBuffersGroup[grp_num][bn].status == eBUF_READY)
		{
			TBufFullCount[grp_num]--; 
		}

		TransportBuffersGroup[grp_num][bn].status 	= eBUF_WRITING;
		*hBuf								  		= TransportBuffersGroup[grp_num][bn].hBuf;
		pthread_mutex_unlock(&buf_mutex[grp_num]);
		return;
	}

	//WARN("All buffers are busy!!! - grp %i\r\n", grp_num);

	pthread_mutex_unlock(&buf_mutex[grp_num]);
	return;
}


void FreeTransportBufGroup(int grp_num)
{
    pthread_mutex_lock(&buf_mutex[grp_num]);
    memset(&(TransportBuffersGroup[grp_num][0]), 0, sizeof(buffer_dsc) * TBUF_NUMBER);
    pthread_mutex_unlock(&buf_mutex[grp_num]);
}


int PlugToTransportBufGroup(u8 reader_num, u8 flags, int grp_num)
{
	pthread_mutex_lock(&buf_mutex[grp_num]);
	int ok = 1;
	do
	{
	    if(CheckReader(reader_num, grp_num) != 0)
		{
			ok = -1; 
			break;
		}
	    if(AddReader(reader_num, flags, grp_num) < 0)
	    {
	    	ERR("Failed to add reader to group num %i!\r\n", grp_num);
	    	ok = -1; 
	    	break;
	    }
	}
	while(0);
	pthread_mutex_unlock(&buf_mutex[grp_num]);

	return ok;
}


int CheckReader(u8 reader_num, int grp_num)
{
    if(reader_num > MAX_READERS_NUM)
    {
    	ERR("Out of memory error!!! rn - %i\r\n", reader_num);
    	return -1;
    }
    if((!bufReaders[grp_num][reader_num].is_busy) && (!(TBufReadersMask[grp_num] & (1 << reader_num))))
    {
    	return 0;
    }
    if((bufReaders[grp_num][reader_num].is_busy) && (TBufReadersMask[grp_num] & (1 << reader_num)))
    {
    	return 1;
    }
    ERR("Invalid reader!!! r-%i, g-%i\r\n", reader_num, grp_num);
    return -1;
}


int AddReader(u8 reader_num, u8 flags, int grp_num)
{
	if(reader_num > MAX_READERS_NUM)
	{
		ERR("Out of memory error!!! rn - %i\r\n", reader_num);
		return -1;
	}
	bufReaders[grp_num][reader_num].flags 	= flags;
	bufReaders[grp_num][reader_num].is_busy = 1;
	TBufReadersMask[grp_num] 	   	       |= (1 << reader_num);
    return 1;
}

u8 GetBufferToRead(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, u32 *sequence_num, 
						u8 *isKeyFlag)
{
	int 			i;
	int  			bn 		= -1;
	u64 			tmstamp = 0xFFFFFFFFFFFFFFFFULL;

	*hBuf 	= NULL;

    if(CheckReader(reader_num, grp_num) != 1)
    {
		return FAILURE;
    }
	pthread_mutex_lock(&buf_mutex[grp_num]);

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if((TransportBuffersGroup[grp_num][i].status == eBUF_READY) 
		|| (TransportBuffersGroup[grp_num][i].status == eBUF_READING))
		{
			if((TransportBuffersGroup[grp_num][i].clients & (1 << reader_num)) 
			&& (tmstamp > TransportBuffersGroup[grp_num][i].timestamp))
			{
				tmstamp = TransportBuffersGroup[grp_num][i].timestamp;
				bn = i;
			}
		}
	}

	if(bn >= 0)
	{
		*hBuf 	= TransportBuffersGroup[grp_num][bn].hBuf;

		if(TransportBuffersGroup[grp_num][bn].status == eBUF_READY)
		{
			TransportBuffersGroup[grp_num][bn].status = eBUF_READING;
		}

		TransportBuffersGroup[grp_num][bn].number_of_reading++;
		
		if(size 		!= NULL)
		{
			*size 			= TransportBuffersGroup[grp_num][bn].data_size;
		}

		if(timestamp 	!= NULL)
		{
			*timestamp 		= tmstamp;
		}

		if(sequence_num != NULL)
		{
			*sequence_num 	= TransportBuffersGroup[grp_num][bn].number;
		}

		if(isKeyFlag    != NULL)
		{
			*isKeyFlag 		= TransportBuffersGroup[grp_num][bn].properties;
		}

		TBufFullCount[grp_num]--;
	}

	pthread_mutex_unlock(&buf_mutex[grp_num]);

	return (bn >= 0);
}

u8 GetLastBuffer(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, u32 *sequence_num, 
	u8 *isKeyFlag)
{
	int 	i;
	int  	bn 		= -1;
	int 	ser     = -1;

	if(CheckReader(reader_num, grp_num) != 1)
    {
		return FAILURE;
    }

	pthread_mutex_lock(&buf_mutex[grp_num]);

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if((TransportBuffersGroup[grp_num][i].status == eBUF_READY) 
		|| (TransportBuffersGroup[grp_num][i].status == eBUF_READING))
		{
			if ((TransportBuffersGroup[grp_num][i].clients & (1 << reader_num)) &&
				((int)TransportBuffersGroup[grp_num][i].number > ser))
			{
				ser = TransportBuffersGroup[grp_num][i].number;
				bn  = i;
			}
		}
	}

	if(bn >= 0)
	{
		if(hBuf 		!= NULL)
		{
			*hBuf 		= TransportBuffersGroup[grp_num][bn].hBuf;
		}

		if(size 		!= NULL)
		{
			*size 		= TransportBuffersGroup[grp_num][bn].data_size;
		}

		if(timestamp 	!= NULL)
		{
			*timestamp 	= TransportBuffersGroup[grp_num][bn].timestamp;
		}

		if(sequence_num != NULL)
		{
			*sequence_num 	= ser;
		}

		if(isKeyFlag 	!= NULL)
		{
			*isKeyFlag 	= TransportBuffersGroup[grp_num][bn].properties;
		}
	}
	pthread_mutex_unlock(&buf_mutex[grp_num]);

	return (bn >= 0);
}


u8 GetLastKeyBufferToRead(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, u32 *sequence_num)
{
	int 	i;
	int  	bn 		= -1;
	int 	ser     = -1;

	*hBuf 	= NULL;

	if(CheckReader(reader_num, grp_num) != 1)
    {
		return FAILURE;
    }

	pthread_mutex_lock(&buf_mutex[grp_num]);

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if(((TransportBuffersGroup[grp_num][i].status == eBUF_READY) 
		|| (TransportBuffersGroup[grp_num][i].status == eBUF_READING)) &&
			(TransportBuffersGroup[grp_num][i].properties == isKey))
		{
			if ((TransportBuffersGroup[grp_num][i].clients & (1 << reader_num)) &&
				((int)TransportBuffersGroup[grp_num][i].number > ser))
			{
				ser = TransportBuffersGroup[grp_num][i].number;
				bn  = i;
			}
		}
	}

	if(bn >= 0)
	{
		if(TransportBuffersGroup[grp_num][bn].status == eBUF_READY)
		{
			TransportBuffersGroup[grp_num][bn].status = eBUF_READING;
		}

		TransportBuffersGroup[grp_num][bn].number_of_reading++;

		if(hBuf 		!= NULL)
		{
			*hBuf 			= TransportBuffersGroup[grp_num][bn].hBuf;
		}

		if(size 		!= NULL)
		{
			*size 			= TransportBuffersGroup[grp_num][bn].data_size;
		}

		if(timestamp 	!= NULL)
		{
			*timestamp 		= TransportBuffersGroup[grp_num][bn].timestamp;
		}

		if(sequence_num != NULL)
		{
			*sequence_num 	= ser;
		}

		TBufFullCount[grp_num]--;
	}
	pthread_mutex_unlock(&buf_mutex[grp_num]);

	return (bn >= 0);
}


u8 GetBufferByNumToRead(u8 reader_num, int grp_num, u32 sequence_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, 
	u8 *isKeyFlag)
{
	int 	i;
	int  	bn 		= -1;

	*hBuf 	= NULL;

	if(CheckReader(reader_num, grp_num) != 1)
    {
		return FAILURE;
    }

	pthread_mutex_lock(&buf_mutex[grp_num]);

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if((TransportBuffersGroup[grp_num][i].number == sequence_num) &&
			((TransportBuffersGroup[grp_num][i].status == eBUF_READY) 
				|| (TransportBuffersGroup[grp_num][i].status == eBUF_READING)))
		{
			if (TransportBuffersGroup[grp_num][i].clients & (1 << reader_num))
			{
				bn  = i;
				break;
			}
		}
	}

	if(bn >= 0)
	{
		if(TransportBuffersGroup[grp_num][bn].status == eBUF_READY)
		{
			TransportBuffersGroup[grp_num][bn].status = eBUF_READING;
		}

		TransportBuffersGroup[grp_num][bn].number_of_reading++;

		if(hBuf 		!= NULL)
		{
			*hBuf 		= TransportBuffersGroup[grp_num][bn].hBuf;
		}

		if(size 		!= NULL)
		{
			*size 		= TransportBuffersGroup[grp_num][bn].data_size;
		}

		if(timestamp 	!= NULL)
		{
			*timestamp 	= TransportBuffersGroup[grp_num][bn].timestamp;
		}

		if(isKeyFlag 	!= NULL)
		{
			*isKeyFlag 	= TransportBuffersGroup[grp_num][bn].properties;
		}

		TBufFullCount[grp_num]--;
	}
	pthread_mutex_unlock(&buf_mutex[grp_num]);

	return (bn >= 0);
}

// каждый читатель может прочитать буфер 1 раз, после этого снимается флаг читателя для данного буфера
// если нет ни одного флага читателя у буфера - он помечается свободным
void BufferReadComplete(u8 reader_num, int grp_num, Buffer_Handle hBuf)
{
	int i;
	u32 reader_bit = (1 << reader_num);
	if(CheckReader(reader_num, grp_num) != 1)
	{
		WARN("The reader doesn't exist\r\n");
		return;
	}

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if(TransportBuffersGroup[grp_num][i].hBuf == hBuf)
		{
			break;
		}
	}

	if(i >= TBUF_NUMBER)
	{
		ERR("Buffer is not exist!!! r - %i, g - %i\r\n", reader_num, grp_num);
		return;
	}


	if(TransportBuffersGroup[grp_num][i].status != eBUF_READING)
	{
		ERR("Buffer is not being read!!!\r\n");
		return;
	}

	if(!(TransportBuffersGroup[grp_num][i].clients & reader_bit))
	{
		ERR("Invalid buffer's clients!!!\n");
		return;
	}

	if(TransportBuffersGroup[grp_num][i].number_of_reading == 0)
	{
		ERR("Buffer is not being read!!!\r\n");
		return;
	}

	pthread_mutex_lock(&buf_mutex[grp_num]);

	TransportBuffersGroup[grp_num][i].clients &= ~reader_bit;
	TransportBuffersGroup[grp_num][i].number_of_reading--;

	if(TransportBuffersGroup[grp_num][i].number_of_reading == 0)
	{
		TransportBuffersGroup[grp_num][i].status = (TransportBuffersGroup[grp_num][i].clients == 0) 
														? eBUF_FREE : eBUF_READY;

		if (TransportBuffersGroup[grp_num][i].clients == 0)
		{
			pthread_mutex_lock(&wcond_mutex[grp_num]);
			pthread_cond_broadcast(&wbuf_cond[grp_num]);
			pthread_mutex_unlock(&wcond_mutex[grp_num]);
		}
	}

	pthread_mutex_unlock(&buf_mutex[grp_num]);
}

void BufferReadCompleteByNum(u8 reader_num, int grp_num, int frame_num)
{
	int i;
	u32 reader_bit = (1 << reader_num);
	if(CheckReader(reader_num, grp_num) != 1)
	{
		WARN("The reader doesn't exist\r\n");
		return;
	}

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if(TransportBuffersGroup[grp_num][i].number == frame_num)
		{
			break;
		}
	}
	if(i >= TBUF_NUMBER)
	{
		ERR("Buffer is not exist!!! r - %i, g - %i\r\n", reader_num, grp_num);
		return;
	}

	if(TransportBuffersGroup[grp_num][i].status != eBUF_READING)
	{
		ERR("Buffer is not being read!!!\r\n");
		return;
	}
	if(!(TransportBuffersGroup[grp_num][i].clients & reader_bit))
	{
		ERR("Invalid buffer's clients!!!\r\n");
		return;
	}
	if(TransportBuffersGroup[grp_num][i].number_of_reading == 0)
	{
		ERR("Buffer is not being read!!!\r\n");
		return;
	}

	pthread_mutex_lock(&buf_mutex[grp_num]);

	TransportBuffersGroup[grp_num][i].clients &= ~reader_bit;
	TransportBuffersGroup[grp_num][i].number_of_reading--;

	if(TransportBuffersGroup[grp_num][i].number_of_reading == 0)
	{
		TransportBuffersGroup[grp_num][i].status = (TransportBuffersGroup[grp_num][i].clients == 0) 
														? eBUF_FREE : eBUF_READY;

		if (TransportBuffersGroup[grp_num][i].clients == 0)
		{
			pthread_mutex_lock(&wcond_mutex[grp_num]);
			pthread_cond_broadcast(&wbuf_cond[grp_num]);
			pthread_mutex_unlock(&wcond_mutex[grp_num]);
		}
	}

	pthread_mutex_unlock(&buf_mutex[grp_num]);
}


void ReleaseTransportBufGroup(u8 reader_num, int grp_num)
{	
    pthread_mutex_lock(&buf_mutex[grp_num]);
	if(CheckReader(reader_num, grp_num) < 0)
	{
		pthread_mutex_unlock(&buf_mutex[grp_num]);
		WARN("The reader doesn't exist\r\n");
		return;
	}
	if(RemoveReader(reader_num, grp_num) < 0)
	{
		pthread_mutex_unlock(&buf_mutex[grp_num]);
		WARN("The reader doesn't exist\r\n");
		return;
	}
	pthread_mutex_unlock(&buf_mutex[grp_num]);
}


int RemoveReader(u8 reader_num, int grp_num)
{
    if(reader_num > MAX_READERS_NUM)
    {
    	ERR("Out of memory error!!! rn - %i\r\n", reader_num);
    	return -1;
    }
    if(!bufReaders[grp_num][reader_num].is_busy)
    {
    	ERR("Reader is free!!!\r\n");
    	return -1;
    }
    bufReaders[grp_num][reader_num].is_busy = 0;
    bufReaders[grp_num][reader_num].flags 	= 0;
    TBufReadersMask[grp_num] 	   	       &= ~(1 << reader_num);
    return 1;
}


int  GetFreeBuffersNum(u8 reader_num, int grp_num)
{
	int i;
	int num_free = 0;

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if((TransportBuffersGroup[grp_num][i].status == eBUF_READY) 
		&& (TransportBuffersGroup[grp_num][i].clients & (1 << reader_num)))
		{
			num_free++;
		}
	}
	return num_free;
}

int GetAnyBufferPtr(int grp_num, u8 **ptr)
{
	int 	i;
	int  	bn 		= -1;
	u64 	tmstamp = 0x000000000000ULL;

	for(i = 0; i < TBUF_NUMBER; i++)
	{			
		if(tmstamp < TransportBuffersGroup[grp_num][i].timestamp)
		{
			tmstamp = TransportBuffersGroup[grp_num][i].timestamp;
			bn 		= i;
		}
	}
	if(bn >= 0)
	{
		*ptr = (u8 *)Buffer_getUserPtr(TransportBuffersGroup[grp_num][bn].hBuf);
	}
	return (bn >= 0);
}

int IsKeyFramePresent(u8 reader_num, int grp_num)
{
	u8	found = 0;
 	int i;

	for(i = 0; i < TBUF_NUMBER; i++)
	{
		if(    (TransportBuffersGroup[grp_num][i].status 	 == eBUF_READY)
			&& (TransportBuffersGroup[grp_num][i].properties == isKey)
			&& (TransportBuffersGroup[grp_num][i].clients & (1 << reader_num)) )
		{
			found = 1;
		}
	}
	return found;
}