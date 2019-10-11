/*
 * mp4ff.c
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/stat.h>

#include "common.h"
#include "moov.h"
#include "logging.h"
#include "avrec_service.h"
#include "transport_buffers.h"

#include "mp4ff.h"

#define LIMIT_SPACE			(4*1024*1024 - 1024) 		// in KB. Limit 4GB size for each file

#define SWAP(a)				((((a)>>24)&0xFF)|(((a)>>8)&0xFF00)|(((a)<<8)&0xFF0000)|(((a)<<24)&0xFF000000))
#define set32(c,a,b) 		*(u32*)((c) + (a)) = SWAP((b))

#define ONE_WORD 			4
#define SECS_TO_SAVE   		6
#define MAX_ACHUNKS_IN_SEC	16
#define MIN_RECFILE_SIZE 	1313 	// in bytes

#define ALL_REC_INFO		0x0FFF
#define VSTCO_TAG 			1 << 0
#define VSTSZ_TAG			1 << 1
#define VSTSS_TAG			1 << 2
#define STIM_TAG			1 << 3
#define AVCC_TAG			1 << 4
#define FPS_TAG				1 << 5
#define FHGT_TAG			1 << 6
#define FWDT_TAG			1 << 7
#define VSRN_TAG			1 << 8
#define CTIM_TAG			1 << 9
#define ASTSZ_TAG			1 << 10
#define ASTCO_TAG			1 << 11

u8     *recFileBuffer;
u8 	   *tempavcc;
u8 	   *tempvstco;
u8 	   *tempvstsz;
u8 	   *tempvstss;
u8 	   *tempastco;
u8 	   *tempastsz;
int   	tempTablesIndex;
int 	fileSize;

long long GetDiskFreeSpace(const char *pDisk)
{
	long long int 		freespace 				= 0;
	struct 				statfs disk_statfs;
	struct 	timespec    cond_time;
	int 				err;

    while(1)
    {
        if(is_sd_mounted == 1)
        {
        	if(is_sdcard_off_status)
		    {
		    	sd_failed       = 1;
		        WARN("SD card isn't inserted\r\n");
		        is_sd_mounted   = 0;
		        sd_totalspace   = 0;
		        sd_freespace    = 0;
		        sd_status       = SD_STATUS_EMPTY;
		        return 0;
		    }
        	break;
        }
        else
        {
            makewaittime(&cond_time, 0, 300000000); // 300 ms
            pthread_mutex_lock(&sd_mount_mutex);
            err = pthread_cond_timedwait(&sd_mount_cond, &sd_mount_mutex, &cond_time);
            if(err != 0) 
            {
            	if(err == ETIMEDOUT)
                {
                    ERR("SD is not mounted!\r\n");
                }
                else if(err != ETIMEDOUT)
                {
                    ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                }
            	pthread_mutex_unlock(&sd_mount_mutex);
            	sd_status 		= SD_STATUS_EMPTY;
            	sd_totalspace 	= 0;
                return 0;
            }
            pthread_mutex_unlock(&sd_mount_mutex);
        }
    }

	if(statfs(pDisk, &disk_statfs) >= 0)
	{
		freespace = ((long long int)disk_statfs.f_bsize  
						* (long long int)disk_statfs.f_bfree) >> 10;
		if(sd_totalspace == 0)
		{
			sd_totalspace = ((long long int)disk_statfs.f_bsize  
						* (long long int)disk_statfs.f_blocks) >> 10;
		}
	}
	else
	{
		sd_status = SD_STATUS_ERROR;
	}
	// debug("TOTAL %lld\r\n", sd_totalspace);
	// debug("FREE %lld\r\n", freespace);
	return freespace;
}

int	CheckCardFreeSpace(FILE* fp)
{
	sd_freespace 	= GetDiskFreeSpace("/media/card");
	// sd_freespace 	= GetDiskFreeSpace("/media/mmcblk0");

	long long int curfilesize 	= 0;

	if(sd_freespace <= 0)
	{
		ERR("SD card has no empty space\r\n");
		return FAILURE;
	}

	if(fp != NULL)
	{
		curfilesize = ftello64(fp);
		if( (curfilesize>>10) >= LIMIT_SPACE )		// in KBytes for 32-bit OS
		{
			ERR("Meet 4GB limit!\r\n");
			ERR("Current file size = %d !\r\n",(int)curfilesize);

			return FAILURE;
		}
	}

	if(sd_freespace > (RESERVE_SPACE))
	{
		is_memory_full = 0;
		sd_status = SD_STATUS_READY;
		return SUCCESS;
	}
	else
	{
		if(is_memory_full == 0)
		{
			is_memory_full 	= 1;
			sd_status       = SD_STATUS_FULL;
			logEvent(log_REC_APL_SD_CARD_FULL);
		}
		ERR("Space = %ld Kbyte is not enough \r\n", (long)sd_freespace);		
		return FAILURE;
	}
}

// разбираем служебный файл, ищем в нем теги
// это поможет восстановить недописанное видео, если в процессе записи sd-карту извлекли
int ParseRecoveryData(FILE *pFile, t_moov_params *mp4MoovData)
{
	size_t 	ret;
	int 	realSize;
	int 	subtagptr 	= 0;
	int 	tagptr 		= 0;
	int 	curptr 		= 0;
	int 	result 		= 0;
	int 	tagSize 	= 0;
	int 	tagName 	= 0;
	int 	subTagSize 	= 0;
	int 	subTagName 	= 0;
	int 	tint 		= 0;
	int 	vStcoCount	= 0;
	int 	vStszCount 	= 0;
	u8 		t8 			= 0;
	u8		breakSub 	= 0;

	tempTablesIndex = 0;

	frames_per_sec 					= 0;
	frame_height					= 0;
	actual_frame_width				= 0;
	video_source_num 				= 3;		// init with incorrect value
	mp4MoovData->chunkCountInGroup 	= 0;		// always 0 for recovery	
	mp4MoovData->avcCSize 			= 0;	
	mp4MoovData->frameCount 		= 100000;	// > 90000 frames per hour
	mp4MoovData->stssCount 			= 0;
	mp4MoovData->chunkCount 		= 0; 		// audio stsz
	mp4MoovData->chunkGroupsCount 	= 0; 		// audio stco

	fseek(pFile, 0, SEEK_END);
	fileSize = ftell(pFile);
	if(fileSize < MIN_RECFILE_SIZE)
	{
		ERR("Recovery file is too short\r\n");
		return FAILURE;
	}
	rewind (pFile);

	// allocate memory to contain the whole file
    recFileBuffer = (u8*) malloc (sizeof(u8) * fileSize);
    if(recFileBuffer == NULL)
    {
        ERR("Cannot allocate memory for recovery file\r\n");
        free(recFileBuffer);
        recFileBuffer = 0;
        return FAILURE;
    }

    // copy the file into the buffer
    ret = fread(recFileBuffer, 1, fileSize, pFile);
    if(ret != fileSize) 
    {
        ERR("Cannot read recovery file\r\n");
        free(recFileBuffer);
        recFileBuffer = 0;
        return FAILURE;
    }

	while(tagptr < fileSize)
	{
		breakSub 	= 0;
		curptr 		= tagptr;
		tagSize 	= get32v(&recFileBuffer[curptr]);
		curptr     += ONE_WORD;

  		if(tagSize == 0)
  		{
  			tagptr += ONE_WORD;
  			continue;
  		}

  		tagName = get32v(&recFileBuffer[curptr]);
  		curptr += ONE_WORD;

  		switch(tagName)
  		{
  			case to_num('s','t','i','m'):
  				//debug("===>stim\r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(struct tm))
  				{
  					ERR("Cannot read \"stim\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy(&mp4MoovData->startTime, &recFileBuffer[curptr], sizeof(struct tm));
  				result |= STIM_TAG;
		  		break;  			
  			case to_num('a','v','c','c'):
  				//debug("===>avcc\r\n");
  				mp4MoovData->avcCSize = tagSize - ONE_WORD - ONE_WORD;
  				if(mp4MoovData->avcCSize < 16)
  				{
  					ERR("Cannot read \"avcc\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				tempavcc = &recFileBuffer[curptr];
  				result |= AVCC_TAG;
  				break;
  			case to_num('f','p','s',' '):
  				//debug("===>fps \r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(int))
  				{
  					ERR("Cannot read \"fps \" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy((void *)&tint, &recFileBuffer[curptr], ONE_WORD);
  				if((tint == 5) || (tint == 25) || (tint == 30) || (tint == 12))
  				{
  					frames_per_sec = tint;
  					result        |= FPS_TAG;
  				}
  				else
  				{
  					ERR("Value of \"fps \" tag from recovery file is invalid\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				break;
  			case to_num('c','t','i','m'):
  				//debug("===>ctim\r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(time_t))
  				{
  					ERR("Cannot read \"ctim\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy(&mp4MoovData->creationTime, &recFileBuffer[curptr], sizeof(time_t));
  				result |= CTIM_TAG;
  				break;
  			case to_num('f','h','g','t'):
  				//debug("===>fhgt\r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(int))
  				{
  					ERR("Cannot read \"fhgt\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy((void *)&tint, &recFileBuffer[curptr], ONE_WORD);
  				if((tint == 480) || (tint == 576) || (tint == 720))
  				{
  					frame_height	= tint;
  					result         |= FHGT_TAG;
  				}
  				else
  				{
  					ERR("Value of \"fhgt\" tag from recovery file is invalid\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				break;
  			case to_num('f','w','d','t'):
  				//debug("===>fwdt\r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(int))
  				{
  					ERR("Cannot read \"fwdt\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy((void *)&tint, &recFileBuffer[curptr], ONE_WORD);
  				if((tint == 640) || (tint == 720) || (tint == 704) || (tint == 1264))
  				{
  					actual_frame_width	= tint;
  					result      	   |= FWDT_TAG;
  				}
  				else
  				{
  					ERR("Value of \"fwdt\" tag from recovery file is invalid\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				break;
  			case to_num('v','s','r','n'):
  				//debug("===>vsrn\r\n");
  				if(tagSize != ONE_WORD + ONE_WORD + sizeof(int))
  				{
  					ERR("Cannot read \"vsrn\" tag from recovery file\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				memcpy((void *)&tint, &recFileBuffer[curptr], ONE_WORD);
  				t8 = (u8)tint;
  				if((t8 == 0) || (t8 == 1) || (t8 == 2))
  				{
  					video_source_num = t8;
  					result          |= VSRN_TAG;
  				}
  				else
  				{
  					ERR("Value of \"vsrn\" tag from recovery file is invalid\r\n");
			        free(recFileBuffer);
			        recFileBuffer = 0;
			        return FAILURE;
  				}
  				break;
  			case to_num('m','p','4','v'):
  				//debug("===>mp4v\r\n");
  				subtagptr = curptr;
  				if(tempTablesIndex == 0)
  				{
  					tempTablesIndex	= curptr - ONE_WORD - ONE_WORD;
  				}
  				while(subtagptr < tagptr + tagSize)
				{
					curptr 		= subtagptr;
					subTagSize 	= get32v(&recFileBuffer[curptr]);
					curptr     += ONE_WORD;

			  		if(subTagSize == 0)
			  		{
			  			subtagptr += ONE_WORD;
			  			continue;
			  		}

			  		subTagName = get32v(&recFileBuffer[curptr]);
			  		curptr    += ONE_WORD;

			  		switch(subTagName)
  					{
  						case to_num('s','t','c','o'):
  							//debug("===>mp4v=>stco\r\n");
  							realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stco\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				vStcoCount 	+= realSize;
			  				result 		|= VSTCO_TAG;
  							break;
	  					case to_num('s','t','s','z'):
	  						//debug("===>mp4v=>stsz\r\n");
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stsz\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				vStszCount 	+= realSize;
			  				result 		|= VSTSZ_TAG;
	  						break;
	  					case to_num('s','t','s','s'):
	  						//debug("===>mp4v=>stss\r\n");
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stss\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				mp4MoovData->stssCount += realSize;
			  				result |= VSTSS_TAG;
	  						break;
	  					default:
	  						breakSub = 1;
  					}

  					if(breakSub)
  					{
  						break;
  					}

  					subtagptr += subTagSize;
  				}
  				break;
  			case to_num('m','p','4','a'):
  				//debug("===>mp4a\r\n");
  			  	subtagptr = curptr;
  			  	if(tempTablesIndex == 0)
  				{
  					tempTablesIndex = curptr - ONE_WORD - ONE_WORD;
  				}
  				while(subtagptr < tagptr + tagSize)
				{
					curptr 		= subtagptr;
					subTagSize 	= get32v(&recFileBuffer[curptr]);
					curptr     += ONE_WORD;

			  		if(subTagSize == 0)
			  		{
			  			subtagptr += ONE_WORD;
			  			continue;
			  		}

			  		subTagName = get32v(&recFileBuffer[curptr]);
			  		curptr += ONE_WORD;

			  		switch(subTagName)
  					{
  						case to_num('s','t','s','z'):
  							//debug("===>mp4a=>stsz\r\n");
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stsz\" subtag of \"mp4a\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				mp4MoovData->chunkCount += realSize;
			  				result |= ASTSZ_TAG;
	  						break;
  						case to_num('s','t','c','o'):
  							//debug("===>mp4a=>stco\r\n");
  							realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stco\" subtag of \"mp4a\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				mp4MoovData->chunkGroupsCount += realSize;
			  				result |= ASTCO_TAG;
  							break;
	  					default:
	  						breakSub = 1;
  					}

  					if(breakSub)
  					{
  						break;
  					}

  					subtagptr += subTagSize;
  				}
  				break;
  			default:
  				break;
  		}
		tagptr += tagSize;
	}

	// если в файле присутствовали все теги, то разбор прошел успешно - информации для восстановления хватает
	if(result == ALL_REC_INFO)
	{
		if(mp4MoovData->frameCount > vStcoCount)
		{
			mp4MoovData->frameCount = vStcoCount;
		}
		if(mp4MoovData->frameCount > vStszCount)
		{
			mp4MoovData->frameCount = vStszCount;
		}

		mp4MoovData->chunkCount = mp4MoovData->chunkGroupsCount << 2;


		debug("vStcoCount %i\r\n", vStcoCount);
		debug("vStszCount %i\r\n", vStszCount);
		debug("mp4MoovData->stssCount %i\r\n", mp4MoovData->stssCount);
		debug("mp4MoovData->chunkCount %i\r\n", mp4MoovData->chunkCount);
		debug("mp4MoovData->chunkGroupsCount %i\r\n", mp4MoovData->chunkGroupsCount);
		debug("mp4MoovData->frameCount %i\r\n", mp4MoovData->frameCount);
		return SUCCESS;
	}

	ERR("Recovery file is failed\r\n");
	return FAILURE;
}


// из файла для восстановления собрать таблицы для заголовка mp4
// таблицы в файле хранятся частями, которые здесь собираем воедино в выделенные под них массивы
int FillMovieTables(t_moov_params* mp4MoovData)
{
	u8		breakSub 		= 0;
	int 	subTagSize 		= 0;
	int 	subTagName 		= 0;
	int 	subtagptr 		= 0;
	int 	tagSize 		= 0;
	int 	tagName 		= 0;
	int 	curptr 			= 0;
	int 	tagptr 			= 0;
	int 	v_stco_index 	= 0;
	int 	v_stsz_index 	= 0;
	int 	v_stss_index 	= 0;
	int 	a_stsz_index 	= 0;
	int 	a_stco_index 	= 0;
	int 	realSize;
	// int 	n;

	if(!mp4MoovData->moov)
	{
		ERR("Memory for movie tables is not allocated\r\n");
		return FAILURE;
	}
	memcpy(mp4MoovData->avcC, tempavcc, mp4MoovData->avcCSize);
	tempavcc = 0;

	if(!mp4MoovData->v_stsd)
	{
		ERR("Memory for video stsd table is not allocated\r\n");
		return FAILURE;
	}

	if(!mp4MoovData->v_stco)
	{
		ERR("Memory for video stco table is not allocated\r\n");
		return FAILURE;
	}
	if(!mp4MoovData->v_stsz)
	{
		ERR("Memory for video stsz table is not allocated\r\n");
		return FAILURE;
	}
	if(!mp4MoovData->v_stss)
	{
		ERR("Memory for video stss table is not allocated\r\n");
		return FAILURE;
	}

#ifdef SOUND_EN
	if(!mp4MoovData->s_stsc)
	{
		ERR("Memory for audio stsc table is not allocated\r\n");
		return FAILURE;
	}
		
	if(!mp4MoovData->s_stsz)
	{
		ERR("Memory for audio stsz table is not allocated\r\n");
		return FAILURE;
	}
	if(!mp4MoovData->s_stco)
	{
		ERR("Memory for audio stco table is not allocated\r\n");
		return FAILURE;
	}
#endif


	// copy all parts of table in one

	tagptr = tempTablesIndex;

	while(tagptr < fileSize)
	{
		breakSub 	= 0;
		curptr 		= tagptr;
		tagSize 	= get32v(&recFileBuffer[curptr]);
		curptr     += ONE_WORD;

  		if(tagSize == 0)
  		{
  			tagptr += ONE_WORD;
  			continue;
  		}

  		tagName = get32v(&recFileBuffer[curptr]);
  		curptr += ONE_WORD;

  		switch(tagName)
  		{
  			case to_num('m','p','4','v'):
  				subtagptr = curptr;

  				while(subtagptr < tagptr + tagSize)
				{
					curptr 		= subtagptr;
					subTagSize 	= get32v(&recFileBuffer[curptr]);
					curptr     += ONE_WORD;

			  		if(subTagSize == 0)
			  		{
			  			subtagptr += ONE_WORD;
			  			continue;
			  		}

			  		subTagName = get32v(&recFileBuffer[curptr]);
			  		curptr    += ONE_WORD;

			  		switch(subTagName)
  					{
  						case to_num('s','t','c','o'):
  							realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stco\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				memcpy(&mp4MoovData->v_stco[v_stco_index], &recFileBuffer[curptr], realSize << 2);
			  				v_stco_index += realSize;
  							break;
	  					case to_num('s','t','s','z'):
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stsz\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				memcpy(&mp4MoovData->v_stsz[v_stsz_index], &recFileBuffer[curptr], realSize << 2);
			  				v_stsz_index += realSize;
	  						break;
	  					case to_num('s','t','s','s'):
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stss\" subtag of \"mp4v\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
			  				memcpy(&mp4MoovData->v_stss[v_stss_index], &recFileBuffer[curptr], realSize << 2);
			  				v_stss_index += realSize;
	  						break;
	  					default:
	  						breakSub = 1;
  					}

  					if(breakSub)
  					{
  						break;
  					}

  					subtagptr += subTagSize;
  				}
  				break;
  			case to_num('m','p','4','a'):
  			  	subtagptr = curptr;
  				while(subtagptr < tagptr + tagSize)
				{
					curptr 		= subtagptr;
					subTagSize 	= get32v(&recFileBuffer[curptr]);
					curptr     += ONE_WORD;

			  		if(subTagSize == 0)
			  		{
			  			subtagptr += ONE_WORD;
			  			continue;
			  		}

			  		subTagName = get32v(&recFileBuffer[curptr]);
			  		curptr += ONE_WORD;

			  		switch(subTagName)
  					{
  						case to_num('s','t','s','z'):
	  						realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stsz\" subtag of \"mp4a\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
		  					memcpy(&mp4MoovData->s_stsz[a_stsz_index], &recFileBuffer[curptr], realSize << 2);
			  				a_stsz_index += realSize;
	  						break;
  						case to_num('s','t','c','o'):
  							realSize = get32v(&recFileBuffer[curptr]);
							curptr  += ONE_WORD;

			  				if(realSize < 1)
			  				{
			  					ERR("Cannot read \"stco\" subtag of \"mp4a\" tag from recovery file\r\n");
						        free(recFileBuffer);
						        recFileBuffer = 0;
						        return FAILURE;
			  				}
		  					memcpy(&mp4MoovData->s_stco[a_stco_index], &recFileBuffer[curptr], realSize << 2);
			  				a_stco_index += realSize;
  							break;
	  					default:
	  						breakSub = 1;
  					}

  					if(breakSub)
  					{
  						break;
  					}

  					subtagptr += subTagSize;
  				}
  				break;
  			default:
  				break;
  		}
		tagptr += tagSize;
	}

	free(recFileBuffer);
	recFileBuffer = 0;

	return SUCCESS;
}

// рассчитать основные параметры видеозаписи для заголовка mp4 файла (moov atom)
// это можно сделать для файла продолжительностью 1 час, если ещё неизвестна реальная длительность, 
// а если известна - рассчитываем исходя из неё
void SetMoovParams(t_moov_params* mp4MoovData)
{
	int videoFramesNum;
	int stssCount;

#ifdef SOUND_EN
	int sStscEntityCount;
	int soundChunkGpsNum;
	int soundChunkNum;
#endif // SOUND_EN

	if(mp4MoovData->frameCount == 0)											// if capture not started
	{ 
		videoFramesNum = (frames_per_sec != 12) ? (frames_per_sec * MAX_DURATION) : 90360 /*== 12.5 * MAX_DURATION ==*/;
	}
	else 
	{ 
		videoFramesNum = mp4MoovData->frameCount; 
	}

	if(mp4MoovData->stssCount == 0) 
	{
		stssCount = (frames_per_sec != 12) ? ((videoFramesNum + frames_per_sec - 1)/frames_per_sec) 
											: ((videoFramesNum + 25 - 1) / 25); 
	}
	else 
	{ 
		stssCount = mp4MoovData->stssCount; 
	}

	mp4MoovData->vStsdSize 		= sizeof(qta_vt_stsd) + mp4MoovData->avcCSize;
	mp4MoovData->vStssSize 		= sizeof(qta_vt_stss) + (stssCount << 2);
	mp4MoovData->vStcoSize		= sizeof(qta_vt_stco) + (videoFramesNum << 2);
	mp4MoovData->vStszSize		= sizeof(qta_vt_stsz) + (videoFramesNum << 2);

	mp4MoovData->vTrackSize 	= sizeof(qta_vt) + mp4MoovData->vStsdSize + mp4MoovData->vStssSize
								+ mp4MoovData->vStcoSize + mp4MoovData->vStszSize;

#ifdef SOUND_EN
	if(mp4MoovData->chunkCount == 0)
	{ 
		soundChunkNum 	 = (int)((SAMPLING_FREQUENCY * MAX_DURATION) / SAMPLES_PER_CHUNK);
		soundChunkGpsNum = (int)(soundChunkNum / CHUNKS_IN_GROUP) + 1; 
		sStscEntityCount = 2;
	}
	else
	{ 
		soundChunkNum 	 = mp4MoovData->chunkCount;
		soundChunkGpsNum = mp4MoovData->chunkGroupsCount; 
		sStscEntityCount = (mp4MoovData->chunkCount % 4 == 0) ? 1 : 2;
	}

	mp4MoovData->sStscSize 	= sizeof(qta_st_stsc) + (sStscEntityCount * 3 << 2);
	mp4MoovData->sStszSize	= sizeof(qta_st_stsz) + (soundChunkNum << 2);
	mp4MoovData->sStcoSize	= sizeof(qta_st_stco) + (soundChunkGpsNum << 2);
	
	mp4MoovData->sTrackSize = sizeof(qta_st) + mp4MoovData->sStscSize + mp4MoovData->sStszSize 
							+ mp4MoovData->sStcoSize;
#endif

	mp4MoovData->moovSize 	= sizeof(qta_moov) +
#ifdef SOUND_EN
							mp4MoovData->sTrackSize +
#endif // SOUND_EN
							mp4MoovData->vTrackSize;

	mp4MoovData->vTrackPos = sizeof(qta_moov);

	if(mp4MoovData->frameCount == 0) 	// calculate temp position of arrays
	{
		mp4MoovData->vStsdPos  = mp4MoovData->vTrackPos + sizeof(qta_vt);
		mp4MoovData->vStssPos  = mp4MoovData->vStsdPos + mp4MoovData->vStsdSize;
		mp4MoovData->vStcoPos  = mp4MoovData->vStssPos + mp4MoovData->vStssSize;
		mp4MoovData->vStszPos  = mp4MoovData->vStcoPos + mp4MoovData->vStcoSize;
	}
	
#ifdef SOUND_EN
	if(mp4MoovData->chunkCount == 0)
	{
		mp4MoovData->sTrackPos = mp4MoovData->vStszPos + mp4MoovData->vStszSize;
		mp4MoovData->sStscPos  = mp4MoovData->sTrackPos + sizeof(qta_st);
		mp4MoovData->sStszPos  = mp4MoovData->sStscPos + mp4MoovData->sStscSize;
		mp4MoovData->sStcoPos  = mp4MoovData->sStszPos + mp4MoovData->sStszSize;
	}
#endif // SOUND_EN
}

// выделяем место под таблицы для mp4 файла
// таблицы будут заполняться во время записи либо при разборе файла восстановления
int AllocMovieTables(t_moov_params* mp4MoovData)
{
	mp4MoovData->moov = (u8*) malloc(mp4MoovData->moovSize);
	if(!mp4MoovData->moov)
	{
		ERR("Failed to allocate memory for movie tables\r\n");
		return FAILURE;
	}
	mp4MoovData->v_stsd = (u8*)&mp4MoovData->moov[mp4MoovData->vStsdPos];
	mp4MoovData->v_stco = (u32*)&mp4MoovData->moov[mp4MoovData->vStcoPos + sizeof(qta_vt_stco)];
	mp4MoovData->v_stsz = (u32*)&mp4MoovData->moov[mp4MoovData->vStszPos + sizeof(qta_vt_stsz)];
	mp4MoovData->v_stss = (u32*)&mp4MoovData->moov[mp4MoovData->vStssPos + sizeof(qta_vt_stss)];
#ifdef SOUND_EN
	mp4MoovData->s_stsc = (u32*)&mp4MoovData->moov[mp4MoovData->sStscPos + sizeof(qta_st_stsc)];
	mp4MoovData->s_stsz = (u32*)&mp4MoovData->moov[mp4MoovData->sStszPos + sizeof(qta_st_stsz)];
	mp4MoovData->s_stco = (u32*)&mp4MoovData->moov[mp4MoovData->sStcoPos + sizeof(qta_st_stco)];
#endif
	return SUCCESS;
}

// запускаем восстановление параметров видео с помощью служебного файла new_video_file.temp
int RecoverMoovDataFromFile(FILE *file, t_moov_params *mp4MoovData)
{
	 memset(mp4MoovData, 0, sizeof(t_moov_params));
	 mp4MoovData->frameCount 	= 0;
	 mp4MoovData->chunkCount 	= 0;
	 mp4MoovData->avcCSize 		= 256;									// max value to calculate temp moov size
	 frames_per_sec				= 30;

	 // расчет для файла на 1 час
	SetMoovParams(mp4MoovData);
	if(AllocMovieTables(mp4MoovData) != SUCCESS)
	{
		return FAILURE;
	}

	// узнаём реальные параметры записи
	if(ParseRecoveryData(file, mp4MoovData) != SUCCESS)
	{
		return FAILURE;
	}
	if(FillMovieTables(mp4MoovData) != SUCCESS)
	{
		return FAILURE;
	}
	return SUCCESS;
}


// запускаем полное восстановление видеозаписи
int RepairSDCard(const char *pDisk)
{
	int 			ret;
	FILE   		   *pMP4File;
	FILE   		   *pTempFile;
	char   			mp4_file_name[512];
	char    		temp_file_name[512];
	t_moov_params   mp4MoovAtomData;

	// недописанный mp4 файл
	sprintf(mp4_file_name, "%s/new_video_file.mp4", pDisk);

	pMP4File = fopen(mp4_file_name,"rb+");
	if(pMP4File == NULL)
	{
		return SUCCESS;
	}
	
	// служебный файл с параметрами записи
	sprintf(temp_file_name, "%s/new_video_file.temp", pDisk);

	pTempFile = fopen(temp_file_name,"rb+");
	if(pTempFile == NULL)
	{
		ERR("Failed to open the recovery file successfully\r\n");
		fclose(pMP4File);
		pMP4File = NULL;
		return FAILURE;
	}

	ret = RecoverMoovDataFromFile(pTempFile, &mp4MoovAtomData);
	if(ret != 0)
    {
    	fclose(pTempFile);
    	pTempFile 	= NULL;
    	fclose(pMP4File);
    	pMP4File 	= NULL;
        ERR("Failed to recover the MP4 file successfully\r\n");
        return FAILURE;
    }
    fclose(pTempFile);
    pTempFile = NULL;

    // на основании полученной из служебного файла инфы дописываем заголовок mp4 файла (moov atom)
	ret = SaveAndCloseMP4File(pMP4File, &mp4MoovAtomData); 
    if(ret != 0)
    {
    	if(pMP4File != NULL)
    	{
    		fclose(pMP4File);
    		pMP4File = NULL;
    	}
        ERR("Failed to recover and close the MP4 file successfully\r\n");
        return FAILURE;
    }
    return SUCCESS;
}

void CheckReserveSpace(void)
{
	long long int sd_freespace = GetDiskFreeSpace("/media/card");
	fprintf(stderr,"Space on %s = %ld Kbyte \r\n", "/media/card", (long)sd_freespace);
	// long long int sd_freespace = GetDiskFreeSpace("/media/mmcblk0");
	// fprintf(stderr,"Space on %s = %ld Kbyte \r\n", "/media/mmcblk0", (long)sd_freespace);
}

// выделяем место под таблицы, которые записываются в служебный файл для восстановления видеозаписи в случае сбоя
int AllocRecoveryTables(t_moov_params* mp4MoovData)
{
	u32 *tableByte;
	int allVSize  		= 0;
	int recTableSize	= 0;
	int currentPos 	 	= 0;

	int stcoVDataPos 	= ONE_WORD /*size*/ + ONE_WORD /* "mp4v" */;

	int stcoVDataSize	= ONE_WORD /*size*/ + ONE_WORD /* "stco" */ + ONE_WORD /* real size */
						  + frames_per_sec * SECS_TO_SAVE * ONE_WORD;

	int stszVDataPos 	= stcoVDataPos + stcoVDataSize;
						
	int stszVDataSize	= ONE_WORD /*size*/ + ONE_WORD /* "stsz" */ + ONE_WORD /* real size */
						+ frames_per_sec * SECS_TO_SAVE * ONE_WORD;

	int stssVDataPos 	= stszVDataPos + stszVDataSize;
						
	int stssVDataSize	= ONE_WORD /*size*/ + ONE_WORD /* "stss" */ + ONE_WORD /* real size */
						+ KEY_FRAME_INTERVAL * SECS_TO_SAVE * ONE_WORD;

	recTableSize		= stssVDataPos + stssVDataSize;
#ifdef SOUND_EN
	int stszADataPos	= recTableSize
						+ ONE_WORD /*size*/ + ONE_WORD /* "mp4a" */;

	int stszADataSize	= ONE_WORD /*size*/ + ONE_WORD /* "stsz" */ + ONE_WORD /* real size */
						+ MAX_ACHUNKS_IN_SEC * SECS_TO_SAVE * ONE_WORD;

	int stcoADataPos	= stszADataPos + stszADataSize;
						
	int stcoADataSize 	= ONE_WORD /*size*/ + ONE_WORD /* "stco" */ + ONE_WORD /* real size */
						+ (MAX_ACHUNKS_IN_SEC >> 2) * SECS_TO_SAVE * ONE_WORD;

	recTableSize		= stcoADataPos + stcoADataSize;						
#endif
	mp4MoovData->recdata = (u8*) malloc(recTableSize);
	if(!mp4MoovData->recdata)
	{
		ERR("Failed to allocate memory for movie tables\r\n");
		return FAILURE;
	}

	mp4MoovData->recDataSize = recTableSize;

	// === fill all known fields ===

	tableByte = (u32*)&mp4MoovData->recdata[0];

	currentPos++; 													// skip mp4v size
	tableByte[currentPos++] 	= SWAP(0x6D703476); 				/* "mp4v" */
	tableByte[currentPos] 		= SWAP(stcoVDataSize); 				/* stco size */
	tableByte[currentPos + 1] 	= SWAP(0x7374636F); 				/* "stco" */

	currentPos 				   += stcoVDataSize >> 2;
	tableByte[currentPos] 		= SWAP(stszVDataSize); 				/* stsz size */
	tableByte[currentPos + 1] 	= SWAP(0x7374737A); 				/* "stsz" */

	currentPos 				   += stszVDataSize >> 2;
	tableByte[currentPos] 		= SWAP(stssVDataSize); 				/* stss size */
	tableByte[currentPos + 1] 	= SWAP(0x73747373); 				/* "stss" */

	allVSize 					= 2 * ONE_WORD + stcoVDataSize + stszVDataSize + stssVDataSize;
	tableByte[0]				= SWAP(allVSize); 					/* mp4v size */
#ifdef SOUND_EN
	currentPos 					= (allVSize >> 2) + 1;
	tableByte[currentPos++] 	= SWAP(0x6D703461); 				/* "mp4a" */
	tableByte[currentPos] 		= SWAP(stszADataSize); 				/* stsz size */
	tableByte[currentPos + 1]	= SWAP(0x7374737A); 				/* "stsz" */

	currentPos 				   += stszADataSize >> 2;
	tableByte[currentPos] 		= SWAP(stcoADataSize); 				/* stco size */
	tableByte[currentPos + 1] 	= SWAP(0x7374636F); 				/* "stco" */

	tableByte[allVSize >> 2] 	= SWAP(recTableSize - allVSize);	/* mp4a size */

	mp4MoovData->arecStszPos 	= 3;
	mp4MoovData->arecStcoPos	= 3;

#endif

	mp4MoovData->vrecStcoPos 	= 3;
	mp4MoovData->vrecStszPos 	= 3;
	mp4MoovData->vrecStssPos 	= 3;

	// === fill all known fields ===
	mp4MoovData->recv_stco = (u32*)&mp4MoovData->recdata[stcoVDataPos];
	mp4MoovData->recv_stsz = (u32*)&mp4MoovData->recdata[stszVDataPos];
	mp4MoovData->recv_stss = (u32*)&mp4MoovData->recdata[stssVDataPos];

#ifdef SOUND_EN
	mp4MoovData->reca_stsz = (u32*)&mp4MoovData->recdata[stszADataPos];
	mp4MoovData->reca_stco = (u32*)&mp4MoovData->recdata[stcoADataPos];
#endif

	return SUCCESS;
}

FILE* NewMP4File(t_moov_params* mp4MoovData, struct tm* startTime)
{
	int 			tempCtimSize;
	int 			ctimSize;
	time_t          seconds;
    FILE   		   *f 			= NULL;

    seconds  = time(NULL);

    memset(mp4MoovData, 0, sizeof(t_moov_params));
    mp4MoovData->creationTime 	= seconds;
    mp4MoovData->avcCSize 		= 256;									// max value to calculate temp moov size
    mp4MoovData->startTime 		= *(startTime);
    
    SetMoovParams(mp4MoovData);
    AllocMovieTables(mp4MoovData);
    AllocRecoveryTables(mp4MoovData);
 	CheckReserveSpace();

	if(CheckCardFreeSpace(NULL) < 0)
	{
        return NULL;
    }

    // ===== info =====
    f = fopen64("/media/card/new_video_file.temp", "wb+");
    if(f == NULL) 
    {
        ERR("Failed to open new_video_file.temp for writing\r\n");
        logEvent(log_REC_APL_REC_RUNTIME_ERROR);
        return NULL;
    }
    if(fwrite(qti_stim, sizeof(qti_stim), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_stim\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite(startTime, sizeof(struct tm), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: startTime\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    tempCtimSize = sizeof(time_t) + sizeof(qti_ctim);
    ctimSize = SWAP(tempCtimSize);
    memcpy(qti_ctim, &ctimSize, ONE_WORD);
    if(fwrite(qti_ctim, sizeof(qti_ctim), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_ctim\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite(&seconds, sizeof(time_t), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: seconds\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
	if(fwrite(qti_fps, sizeof(qti_fps), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_fps\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite((const void *)&frames_per_sec, 4, 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: frames_per_sec\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
	if(fwrite(qti_fhgt, sizeof(qti_fhgt), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_fhgt\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite((const void *)&frame_height, 4, 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: frame_height\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite(qti_fwdt, sizeof(qti_fwdt), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_fwdt\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite((const void *)&actual_frame_width, 4, 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: actual_frame_width\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite(qti_vsrn, sizeof(qti_vsrn), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_vsrn\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    int vsrn_value = (int)video_source_num;
    if(fwrite((const void *)&vsrn_value, 4, 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: video_source_num\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    fclose(f);
    f = NULL;

    // ===== data =====

    /* Open the output mp4 file for writing */
    f = fopen64("/media/card/new_video_file.mp4", "wb+");
    if(f == NULL) 
    {
        ERR("Failed to open new_video_file.mp4 for writing\r\n");
        logEvent(log_REC_APL_REC_RUNTIME_ERROR);
        return NULL;
    }

    // write the beggining of the file - ftyp atom and mdat header
    if(fwrite(qta_ftyp, sizeof(qta_ftyp), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.mp4: qta_ftyp\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    if(fwrite(qta_mdat, sizeof(qta_mdat), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.mp4: qta_mdat\r\n");
    	fclose(f);
    	f = NULL;
    	return NULL;
    }
    return f;
}

int IsVideoFrameKey(Buffer_Handle videoBuf)
{
	u8  *buf   			= (u8*)Buffer_getUserPtr(videoBuf);
    int  videoDataSize  = Buffer_getNumBytesUsed(videoBuf);
	int  i 				= 0;
	int  isKeyFlag		= -1;

	if (videoDataSize < 5)			// dummy size, check real min size!
	{
		ERR("Video frame is too small\r\n");
		return FAILURE;
	}
	
	do
	{
		if((buf[i] == 0)&&(buf[i+1] == 0)&&(buf[i+2] == 0)&&(buf[i+3] == 1))
		{
			u8  nal;
			nal = (buf[i+4] & 0x1F);
			if(nal == 0x05)			// if key frame
			{
				isKeyFlag = 1;
				break;
			}
			else if (nal == 0x01)	// if non-key frame
			{
				isKeyFlag = 0;
				break;
			}
		}
		i++;
    }
	while(i < videoDataSize - 5);	// till the end of h264 block
	if(isKeyFlag < 0)
	{
		ERR("Frame without video content\r\n");
		return FAILURE;
	}

	return isKeyFlag;
}

int GetVideoParams(u8* h264FilePtr, int h264FileSize, int *isKeyFlag, u32 *ptime)
{
	int  i 	= 0;
	u8  *buf = h264FilePtr;

	if (h264FileSize < 5)			// dummy size, check real min size!
	{
		ERR("Video frame is too small\r\n");
		return FAILURE;
	}
	
	do
	{
		if((buf[i] == 0)&&(buf[i+1] == 0)&&(buf[i+2] == 0)&&(buf[i+3] == 1))
		{
			u8  nal;
			nal = (buf[i+4] & 0x1F);
			if(nal == 0x05)			// if key frame
			{
				(*isKeyFlag) = 1;
				break;
			}
			else if (nal == 0x01)	// if non-key frame
			{
				(*isKeyFlag) = 0;
				break;
			}
		}
		i++;
	}
	while(i < h264FileSize - 5);	// till the end of h264 file always

	if((*isKeyFlag)<0)
	{
		ERR("Frame without video content\r\n");
		return FAILURE;
	}

	return SUCCESS;
}

int GetAvcCFromH264(t_moov_params* mp4MoovData, Buffer_Handle videoBuf)
{
	FILE   *f 				= NULL;
	int 	sps_pos 		= -1;
	int 	pps_pos 		= -1;
	int 	sps_size 		= -1;
	int 	pps_size 		= -1;
	int 	tempAvccSize 	= 0;
	int 	avccSize 		= 0;
	int 	i 				= 0;
	//int 	n 				= 0;
	u8     *sps 			= 0;
 	u8     *pps 			= 0;
	u8     *buf 			= (u8*)Buffer_getUserPtr(videoBuf);
 	int 	h264FileSize 	= Buffer_getNumBytesUsed(videoBuf);

	if (h264FileSize < 16)					// dummy size, check real min size!
	{
		ERR("Video frame is too small\r\n");
		return FAILURE;
	}
	
	do
	{
		if((buf[i] == 0)&&(buf[i+1] == 0)&&(buf[i+2] == 0)&&(buf[i+3] == 1))
		{
			u8  nal;
			if((sps_pos > 0)&&(sps_size < 0))
			{
				sps_size = i - sps_pos;
			}
			if((pps_pos > 0)&&(pps_size < 0))
			{
				pps_size = i - pps_pos;
			}
			nal = (buf[i+4] & 0x1F);
			if(nal == 0x07)
			{
				sps_pos = i + 4;
			}
			if(nal == 0x08)
			{
				pps_pos = i + 4;
			}
		}
		i++;
	}
	while(i < h264FileSize - 5);			// till the end of h264 file always

	if((sps_pos<0) || (pps_pos<0))
	{
		ERR("Did not find sps or pps\r\n");
		return FAILURE;
	}

	if((sps_size<0) || (pps_size<0))
	{
		ERR("Size of sps or pps is too small\r\n");
		return FAILURE;						// if occurs it's an error!
	}

	if((sps_pos > 0)&&(sps_size < 0))
	{
		sps_size = h264FileSize - sps_pos;
	}

	if((pps_pos > 0)&&(pps_size < 0))
	{
		pps_size = h264FileSize - pps_pos;
	}

	sps = &buf[sps_pos];
	pps = &buf[pps_pos];

    // printf("SPS:\n\r");

    // for(n = 0; n < sps_size; n++)
    // {
    //     // if((n % 16)==0)
    //     // {
    //     //     printf("\r\n");
    //     // }
    //     printf(" %02X", sps[n]);         
    // }
    // printf("\r\n");

    // printf("PPS:\n\r");

    // for(n = 0; n < pps_size; n++)
    // {
    //     // if((n % 16)==0)
    //     // {
    //     //     printf("\r\n");
    //     // }
    //     printf(" %02X", pps[n]);         
    // }
    // printf("\r\n");

    if(mp4MoovData != NULL)
    {
		i = 0;

		mp4MoovData->avcC[i++] = 0x01;									// version
		mp4MoovData->avcC[i++] = sps[1];								// avc profile
		mp4MoovData->avcC[i++] = sps[2];								// avc compability
		mp4MoovData->avcC[i++] = sps[3];								// avc level
		mp4MoovData->avcC[i++] = 0xFF;									// 6 bits reserved and 2 bit NALUnitLengthMinusOne
		mp4MoovData->avcC[i++] = 0xE1;									// 3 bits reserved and 5 bit - num of SPS NALUs(1)
		mp4MoovData->avcC[i++] = (sps_size>>8)&0xFF;					// sps size 16 bit, exclude size itself (swapped)
		mp4MoovData->avcC[i++] = (sps_size)&0xFF;

		memcpy(&(mp4MoovData->avcC[i]), sps, sps_size); 
		i+= sps_size;													// sps hexdump

		mp4MoovData->avcC[i++] = 1;										// without this QT doesn't play video (?)
		mp4MoovData->avcC[i++] = (pps_size>>8)&0xFF;					// sps size 16 bit, exclude size itself (swapped)
		mp4MoovData->avcC[i++] = (pps_size)&0xFF;

		memcpy(&(mp4MoovData->avcC[i]), pps, pps_size); i+= pps_size;	// pps hexdump
		mp4MoovData->avcCSize = i;
	}
    f = fopen64("/media/card/new_video_file.temp", "ab+");
    if(f == NULL) 
    {
        ERR("Failed to open new_video_file.temp for writing\r\n");
        logEvent(log_REC_APL_REC_RUNTIME_ERROR);
        return FAILURE;
    }
    tempAvccSize 	= mp4MoovData->avcCSize + sizeof(qti_avcc);
	avccSize 		= SWAP(tempAvccSize);
	memcpy(qti_avcc, &avccSize, ONE_WORD);
    if(fwrite(qti_avcc, sizeof(qti_avcc), 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: qti_avcc\r\n");
    	fclose(f);
    	f = NULL;
    	return FAILURE;
    }
    if(fwrite(mp4MoovData->avcC, 1, mp4MoovData->avcCSize, f) != mp4MoovData->avcCSize)
    {
    	ERR("Failed writing to new_video_file.temp: mp4MoovData->avcC\r\n");
    	fclose(f);
    	f = NULL;
    	return FAILURE;
    }

    fclose(f);
    f = NULL;
	return SUCCESS;
}

void MovieAddFrame(t_moov_params* mp4MoovData, int pos, int size, int keyFrame)
{
	int frNum = mp4MoovData->frameCount++;

	mp4MoovData->v_stco[frNum] = SWAP(pos);
	mp4MoovData->v_stsz[frNum] = SWAP(size);

	// сохраняем информацию для файла восстановления
	if(mp4MoovData->vrecStcoPos < (frames_per_sec * SECS_TO_SAVE))
	{
		mp4MoovData->recv_stco[mp4MoovData->vrecStcoPos++] = SWAP(pos);
	}
	if(mp4MoovData->vrecStszPos < (frames_per_sec * SECS_TO_SAVE))
	{
		mp4MoovData->recv_stsz[mp4MoovData->vrecStszPos++] = SWAP(size);
	}

	if(keyFrame == 1)
	{
		int sync_sample = frNum + 1;
		mp4MoovData->v_stss[mp4MoovData->stssCount++] 		= SWAP(sync_sample);	// first is 1, not 0!
		if(mp4MoovData->vrecStssPos < (KEY_FRAME_INTERVAL * SECS_TO_SAVE))
		{
			mp4MoovData->recv_stss[mp4MoovData->vrecStssPos++] 	= SWAP(sync_sample);
		}
	}
}

int AddVideoFrame(t_moov_params* mp4MoovData, Buffer_Handle videoBuf, FILE * file)
{
    sd_freespace 		= GetDiskFreeSpace("/media/card");
    // sd_freespace 		= GetDiskFreeSpace("/media/mmcblk0");

    int fileTailSize 	= 20480 + ((mp4MoovData->moovSize + AUDIO_CHUNK_GROUP_SIZE) >> 10); // reserved - 1MB

	if(sd_freespace <= 0)
	{
		ERR("SD card has no empty space\r\n");
		return FAILURE;
	}
	else if(sd_freespace <= fileTailSize)
	{
		if(is_memory_full == 0)
		{
			is_memory_full 	= 1;
			sd_status       = SD_STATUS_FULL;
			logEvent(log_REC_APL_SD_CARD_FULL);
		}
		ERR("Space = %ld Kbyte is not enough. Needed %i Kbyte\r\n", (long)sd_freespace, fileTailSize);
		return FAILURE;
	}

	int 			videoDataSize	= Buffer_getNumBytesUsed(videoBuf);
    int 			vFramePos  		= ftello64(file);
    u8             *videoDataAddr	= (u8*)Buffer_getUserPtr(videoBuf);
    u8             *buf 			= videoDataAddr;
	u32 	 		videoTime 		= 0;
	int 			pos 			= 0;
	int 			isLast 			= 0;
	int 			isKeyFlag		= -1;
	int 			nextNalPos;
	int 			nalSize;
	int 			p;
	u32 			swappedNalSize;

	is_memory_full = 0;
   	
   	if(GetVideoParams(videoDataAddr, videoDataSize, &isKeyFlag, &videoTime) < 0)
   	{
   		logEvent(log_REC_APL_REC_RUNTIME_ERROR);
   		ERR("Failed to get video frame parameters\r\n");
		return FAILURE;
	}

	while(!isLast)	// till the last NALU in frame
	{

		if((buf[pos] == 0)&&(buf[pos+1] == 0)&&(buf[pos+2] == 0)&&(buf[pos+3] == 1))
		{
			pos+=4;

			for(p = pos;;p++)
			{
				if(p >= videoDataSize)
				{
					isLast 		= 1;
					nextNalPos 	= p;
					break;
				}

				if((buf[p] == 0)&&(buf[p+1] == 0)&&(buf[p+2] == 0)&&(buf[p+3] == 1))
				{
					nextNalPos = p;
					break;
				}
			}
		}
		else
		{
			ERR("Invalid video frame\r\n");
			logEvent(log_REC_APL_REC_RUNTIME_ERROR);
			return FAILURE;
		}

		if(nextNalPos > 0)
		{
			nalSize 		= nextNalPos - pos;
			swappedNalSize 	= SWAP(nalSize);

			if(fwrite(&swappedNalSize, 4, 1, file) != 1)
			{
				break;
			}

			if(fwrite(&buf[pos], nalSize, 1, file) != 1)
			{
				break;
			}
			
			pos = nextNalPos;
		}
	}

    MovieAddFrame(mp4MoovData, vFramePos, videoDataSize, isKeyFlag);

	return SUCCESS;
}

#ifdef SOUND_EN
int MovieAddChunksGroup(t_moov_params* mp4MoovData, FILE * file)
{
	int pos = ftello64(file);

	// set offset for each chunk group
	mp4MoovData->s_stco[mp4MoovData->chunkGroupsCount++] = SWAP(pos);

	// сохраняем информацию для файла восстановления
	if(mp4MoovData->arecStcoPos < ((MAX_ACHUNKS_IN_SEC >> 2) * SECS_TO_SAVE))
	{
		mp4MoovData->reca_stco[mp4MoovData->arecStcoPos++] = SWAP(pos);
	}

    // write group of 4 chunks to file
    if(fwrite((void*)mp4MoovData->chunkGroup, mp4MoovData->chunksSizeInGroup, 1, file) != 1)
    {
    	ERR("Can't write chunks group to file\r\n");
    	return FAILURE;
    }

	mp4MoovData->chunkCountInGroup = 0;
	mp4MoovData->chunksSizeInGroup = 0;
	return  SUCCESS;
}

int AddAudioChunk(t_moov_params* mp4MoovData, Buffer_Handle audioBuf, FILE * file)
{
	int s;
	sd_freespace 		= GetDiskFreeSpace("/media/card");
	// sd_freespace 		= GetDiskFreeSpace("/media/mmcblk0");
	u8             *audioChunkAddr	= (u8*)Buffer_getUserPtr(audioBuf);
	int 			soundChunkSize	= Buffer_getNumBytesUsed(audioBuf);
					// reserved - 1MB
    int 			fileTailSize 	= 20480 + ((mp4MoovData->moovSize + AUDIO_CHUNK_GROUP_SIZE) >> 10); 

    if(sd_freespace <= 0)
	{
		ERR("SD card has no empty space\r\n");
		return FAILURE;
	}
	else if( sd_freespace <= fileTailSize)
	{
		if(is_memory_full == 0)
		{
			is_memory_full 	= 1;
			sd_status       = SD_STATUS_FULL;
			logEvent(log_REC_APL_SD_CARD_FULL);
		}
		ERR("Space = %ld Kbyte is not enough. Needed %i Kbyte\r\n", (long)sd_freespace, fileTailSize);
		return FAILURE;
	}

	is_memory_full = 0;

    // set size for each chunk
    s = soundChunkSize - AUDIO_HEADER_SIZE;
    mp4MoovData->s_stsz[mp4MoovData->chunkCount++] 		= SWAP(s);

    // сохраняем информацию для файла восстановления
    if(mp4MoovData->arecStszPos < (MAX_ACHUNKS_IN_SEC * SECS_TO_SAVE))
    {
    	mp4MoovData->reca_stsz[mp4MoovData->arecStszPos++] 	= SWAP(s);
    }

    memcpy((void*)(mp4MoovData->chunkGroup + mp4MoovData->chunksSizeInGroup),
    	(void*)(audioChunkAddr + AUDIO_HEADER_SIZE), soundChunkSize - AUDIO_HEADER_SIZE);

    mp4MoovData->chunkCountInGroup++;
    mp4MoovData->chunksSizeInGroup += soundChunkSize - AUDIO_HEADER_SIZE;

    if(mp4MoovData->chunkCountInGroup == 4)
    {
		if(MovieAddChunksGroup(mp4MoovData, file) < 0)
		{
            logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            return FAILURE;
		}
	}
	return  SUCCESS;
}
#endif // SOUND_EN

void FillMovieHeader(t_moov_params* mp4MoovData)
{
	u8* 	ptr;
	int 	vStssPosPrev			= 0;
	int 	vStcoPosPrev			= 0;
	int 	vStszPosPrev			= 0;
	int 	sStscPosPrev			= 0;
	int 	sStcoPosPrev			= 0;
	int 	sStszPosPrev			= 0;
	int 	sDuration 				= 0;
	int 	stscTableSize			= 0;
	int 	frameDur 				= (frames_per_sec != 12) ? (MOVIE_TIMESCALE / frames_per_sec) : 48;
	int 	vDuration 				= mp4MoovData->frameCount * frameDur;
	time_t  seconds  				= time(NULL);
	time_t 	modifTime 				= seconds;
	struct 	tm timeinfo;

	timeinfo = *localtime(&seconds);

	last_rec_time  = 0;
    last_rec_time |= ((timeinfo.tm_year + 1900)%100) << 25;
    last_rec_time |= (timeinfo.tm_mon + 1) << 21;
    last_rec_time |= timeinfo.tm_mday << 16;
    last_rec_time |= timeinfo.tm_hour << 11;
    last_rec_time |= timeinfo.tm_min << 5;
    last_rec_time |= timeinfo.tm_sec / 2;

#ifdef SOUND_EN
	int sndDuration = mp4MoovData->chunkCount * SAMPLES_PER_CHUNK;
	sDuration 		= (int)ceil(((double)MOVIE_TIMESCALE/(double)SAMPLING_FREQUENCY) * (double)sndDuration);
#endif // SOUND_EN

	ptr = mp4MoovData->moov;

	memcpy(&ptr[0], qta_moov, sizeof(qta_moov));								// moov atom header
	set32(ptr, 0, mp4MoovData->moovSize);										// moov atom size
	set32(ptr, 20, (u32)(mp4MoovData->creationTime));							// mvhd creation time
	set32(ptr, 24, (u32)modifTime);												// mvhd modification time
	set32(ptr, 32, (vDuration > sDuration) ? vDuration : sDuration);			// mvhd duration
	debug("\r\n Movie duration: %x\r\n", vDuration);
	debug("\r\n Sound duration: %x\r\n", sDuration);

	if(frame_height == 576)
	{
		debug("moov frame_height: 576\n");
		qta_vt[311] 	= 0x18;
		qta_vt[96] 		= 0x02; 
		qta_vt[97] 		= 0x40;
		qta_vt_stsd[50] = 0x02; 
		qta_vt_stsd[51] = 0x40;
	}
	else if((frame_height == 480) && (video_source_num == 1))
	{
		debug("moov frame_height: 480\n");
		qta_vt[311] 	= 0x14;
		qta_vt[96] 		= 0x01; 
		qta_vt[97] 		= 0xE0;
		qta_vt_stsd[50] = 0x01; 
		qta_vt_stsd[51] = 0xE0;
	}
	else if(frame_height == 720)
	{
		qta_vt[311] 	= 0x18;

		debug("moov frame_height: 720\n");
		qta_vt[96] 		= 0x02; 
		qta_vt[97] 		= 0xD0;
		qta_vt_stsd[50] = 0x02; 
		qta_vt_stsd[51] = 0xD0;

		debug("moov frame_width: 1264\n");
		qta_vt[92] 		= 0x04; 
		qta_vt[93] 		= 0xF0;
		qta_vt_stsd[48] = 0x04; 
		qta_vt_stsd[49] = 0xF0;
	}

	if(frames_per_sec == 12)
	{
		qta_vt[311] 	= 0x30;
	}

	memcpy(&ptr[mp4MoovData->vTrackPos], qta_vt, sizeof(qta_vt));				// video trak atom header
	set32(ptr, mp4MoovData->vTrackPos, mp4MoovData->vTrackSize);				// video trak atom size
	set32(ptr, mp4MoovData->vTrackPos + 20, (u32)(mp4MoovData->creationTime));	// video tkhd creation time
	set32(ptr, mp4MoovData->vTrackPos + 24, (u32)modifTime);					// video tkhd modification time
	set32(ptr, mp4MoovData->vTrackPos + 36, vDuration);							// video tkhd duration
	set32(ptr, mp4MoovData->vTrackPos + 100, mp4MoovData->vTrackSize - 100);	// mdia size
	set32(ptr, mp4MoovData->vTrackPos + 120, (u32)(mp4MoovData->creationTime)); // mdhd creation time
	set32(ptr, mp4MoovData->vTrackPos + 124, (u32)modifTime);					// mdhd modification time
	set32(ptr, mp4MoovData->vTrackPos + 132, vDuration);						// mdhd duration
	set32(ptr, mp4MoovData->vTrackPos + 188, mp4MoovData->vTrackSize - 188);	// minf size
	set32(ptr, mp4MoovData->vTrackPos + 252, mp4MoovData->vTrackSize - 252);	// stbl size
	set32(ptr, mp4MoovData->vTrackPos + 304, mp4MoovData->frameCount);			// stts sample count

	mp4MoovData->vStsdPos = mp4MoovData->vTrackPos + sizeof(qta_vt);
	memcpy(&ptr[mp4MoovData->vStsdPos], qta_vt_stsd, sizeof(qta_vt_stsd));		// stsd atom header
	memcpy(&ptr[mp4MoovData->vStsdPos + sizeof(qta_vt_stsd)], mp4MoovData->avcC, mp4MoovData->avcCSize);	// avcC table
	set32(ptr, mp4MoovData->vStsdPos, mp4MoovData->vStsdSize);					// stsd size
	set32(ptr, mp4MoovData->vStsdPos + 16, mp4MoovData->vStsdSize - 16);		// stsd avc1 table size
	set32(ptr, mp4MoovData->vStsdPos + 102, mp4MoovData->vStsdSize - 102); 		// avcC description size

	vStssPosPrev 			= mp4MoovData->vStssPos;
	mp4MoovData->vStssPos  	= mp4MoovData->vStsdPos + mp4MoovData->vStsdSize;
	memcpy(&ptr[mp4MoovData->vStssPos], qta_vt_stss, sizeof(qta_vt_stss));		// stss atom header
	if(vStssPosPrev != mp4MoovData->vStssPos)									// move stss table if there's free space
	{
		memmove(&ptr[mp4MoovData->vStssPos + sizeof(qta_vt_stss)], &ptr[vStssPosPrev + sizeof(qta_vt_stss)], 
			mp4MoovData->vStssSize - sizeof(qta_vt_stss));
	}
	set32(ptr, mp4MoovData->vStssPos, mp4MoovData->vStssSize);					// stss size
	set32(ptr, mp4MoovData->vStssPos + 12, mp4MoovData->stssCount);				// stss number of entries

	vStcoPosPrev 			= mp4MoovData->vStcoPos;
	mp4MoovData->vStcoPos  	= mp4MoovData->vStssPos + mp4MoovData->vStssSize;
	memcpy(&ptr[mp4MoovData->vStcoPos], qta_vt_stco, sizeof(qta_vt_stco));		// stco atom header
	if(vStcoPosPrev != mp4MoovData->vStcoPos)									// move stco table if there's free space
	{
		memmove(&ptr[mp4MoovData->vStcoPos + sizeof(qta_vt_stco)], &ptr[vStcoPosPrev + sizeof(qta_vt_stco)], 
			mp4MoovData->vStcoSize - sizeof(qta_vt_stco));
	}
	set32(ptr, mp4MoovData->vStcoPos, mp4MoovData->vStcoSize);					// stco size
	set32(ptr, mp4MoovData->vStcoPos + 12, mp4MoovData->frameCount);			// stco number of entries

	vStszPosPrev 			= mp4MoovData->vStszPos;
	mp4MoovData->vStszPos  	= mp4MoovData->vStcoPos + mp4MoovData->vStcoSize;
	memcpy(&ptr[mp4MoovData->vStszPos], qta_vt_stsz, sizeof(qta_vt_stsz));		// stsz atom header
	if(vStszPosPrev != mp4MoovData->vStszPos)									// move stsz table if there's free space
	{
		memmove(&ptr[mp4MoovData->vStszPos + sizeof(qta_vt_stsz)], &ptr[vStszPosPrev + sizeof(qta_vt_stsz)], 
			mp4MoovData->vStszSize - sizeof(qta_vt_stsz));
	}
	set32(ptr, mp4MoovData->vStszPos, mp4MoovData->vStszSize);					// stsz size
	set32(ptr, mp4MoovData->vStszPos + 16, mp4MoovData->frameCount);			// stsz number of entries

#ifdef SOUND_EN
	mp4MoovData->sTrackPos = mp4MoovData->vStszPos + mp4MoovData->vStszSize;	// if changed
	memcpy(&ptr[mp4MoovData->sTrackPos], qta_st, sizeof(qta_st));				// sound trak atom header
	set32(ptr, mp4MoovData->sTrackPos, mp4MoovData->sTrackSize);				// sound trak atom size
	set32(ptr, mp4MoovData->sTrackPos + 20, (u32)(mp4MoovData->creationTime));	// sound tkhd creation time
	set32(ptr, mp4MoovData->sTrackPos + 24, (u32)modifTime);					// sound tkhd modification time
	set32(ptr, mp4MoovData->sTrackPos + 36,  sDuration);						// sound tkhd duration in 600 ts
	set32(ptr, mp4MoovData->sTrackPos + 100, mp4MoovData->sTrackSize - 100);	// mdia size
	set32(ptr, mp4MoovData->sTrackPos + 120, (u32)(mp4MoovData->creationTime)); // mdhd creation time
	set32(ptr, mp4MoovData->sTrackPos + 124, (u32)modifTime);					// mdhd modification time
	set32(ptr, mp4MoovData->sTrackPos + 132, sndDuration);						// mdhd duration
	set32(ptr, mp4MoovData->sTrackPos + 184, mp4MoovData->sTrackSize - 184);	// minf size
	set32(ptr, mp4MoovData->sTrackPos + 244, mp4MoovData->sTrackSize - 244);	// stbl size	

	set32(ptr, mp4MoovData->sTrackPos + 359, mp4MoovData->chunkCount);		   	// stts sample count

	sStscPosPrev 			= mp4MoovData->sStscPos;
	mp4MoovData->sStscPos  	= mp4MoovData->sTrackPos + sizeof(qta_st);
	memcpy(&ptr[mp4MoovData->sStscPos], qta_st_stsc, sizeof(qta_st_stsc));		// stsc atom header
	if(sStscPosPrev != mp4MoovData->sStscPos)									// move stsc table if there's free space
	{
		memmove(&ptr[mp4MoovData->sStscPos + sizeof(qta_st_stsc)], &ptr[sStscPosPrev + sizeof(qta_st_stsc)], 
			mp4MoovData->sStscSize - sizeof(qta_st_stsc));
	}

	stscTableSize = (mp4MoovData->chunkCount % 4 == 0) ? 0x00000001 : 0x00000002;
	set32(ptr, mp4MoovData->sStscPos, mp4MoovData->sStscSize);					// stsc size
	set32(ptr, mp4MoovData->sStscPos + 12, stscTableSize); 						// stsc number of entries

	sStszPosPrev 			= mp4MoovData->sStszPos;
	mp4MoovData->sStszPos  	= mp4MoovData->sStscPos + mp4MoovData->sStscSize;
	memcpy(&ptr[mp4MoovData->sStszPos], qta_st_stsz, sizeof(qta_st_stsz));		// stsz atom header
	if(sStszPosPrev != mp4MoovData->sStszPos)									// move stsz table if there's free space
	{
		memmove(&ptr[mp4MoovData->sStszPos + sizeof(qta_st_stsz)], &ptr[sStszPosPrev + sizeof(qta_st_stsz)], 
			mp4MoovData->sStszSize - sizeof(qta_st_stsz));
	}
	set32(ptr, mp4MoovData->sStszPos, mp4MoovData->sStszSize);					// stsz size
	set32(ptr, mp4MoovData->sStszPos + 16, mp4MoovData->chunkCount); 			// stsz number of entries

	sStcoPosPrev 			= mp4MoovData->sStcoPos;
	mp4MoovData->sStcoPos  	= mp4MoovData->sStszPos + mp4MoovData->sStszSize;
	memcpy(&ptr[mp4MoovData->sStcoPos], qta_st_stco, sizeof(qta_st_stco));		// stco atom header
	if(sStcoPosPrev != mp4MoovData->sStcoPos)									// move stco table if there's free space
	{
		memmove(&ptr[mp4MoovData->sStcoPos + sizeof(qta_st_stco)], &ptr[sStcoPosPrev + sizeof(qta_st_stco)], 
			mp4MoovData->sStcoSize - sizeof(qta_st_stco));
	}
	set32(ptr, mp4MoovData->sStcoPos, mp4MoovData->sStcoSize);					// stco size
	set32(ptr, mp4MoovData->sStcoPos + 12, mp4MoovData->chunkGroupsCount); 		// stco number of entries
#endif // SOUND_EN
}

void CloseMP4File(FILE* file, t_moov_params* mp4MoovData)
{
	debug("CloseMP4File \r\n");
    int ret;

    if(mp4MoovData->moov)
    {
    	free(mp4MoovData->moov);
    }
    if(file)
    {
    	fclose(file);
    }
    file = NULL;
    ret = remove("/media/card/new_video_file.mp4");
    if(ret !=0)
    {
    	ERR("File new_video_file.mp4 cannot be deleted: %s\r\n", strerror(errno));
    }

    ret = remove("/media/card/new_video_file.temp");
    if(ret !=0)
    {
    	ERR("File new_video_file.temp cannot be deleted: %s\r\n", strerror(errno));
    }
}

int SaveAndCloseMP4File(FILE* file, t_moov_params* mp4MoovData)
{
	int 	ret;
	int 	pos;
	int 	tempMdatSize;
	u32 	mdatSize;
	u32 	ftypSize;
	char    inFileName[256];
	char 	outFileName[256];

	if((is_sd_mounted == 0)
		|| is_sdcard_off_status
		)
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Cannot close the file right - sd is not mounted\r\n");
		return FAILURE;
	}

#ifdef SOUND_EN
	mp4MoovData->s_stsc[0] = SWAP(0x00000001);								// all chunk groups contain 4 chunks ...
	mp4MoovData->s_stsc[1] = SWAP(0x00000004);
	mp4MoovData->s_stsc[2] = SWAP(0x00000001);

	if(mp4MoovData->chunkCountInGroup != 0)									// if less than 4 chunks left
	{
		mp4MoovData->s_stsc[3] = SWAP(mp4MoovData->chunkGroupsCount + 1);	// the last chunk differs
		mp4MoovData->s_stsc[4] = SWAP(mp4MoovData->chunkCountInGroup);
		mp4MoovData->s_stsc[5] = SWAP(0x00000001);

		MovieAddChunksGroup(mp4MoovData, file);
	}

#endif // SOUND_EN

	if(fseeko64(file, 0, SEEK_END))
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Cannot seek for the end of the file: %s\r\n", strerror(errno));
		return FAILURE;
	}

	pos = ftello64(file);
	if(pos < 0)
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Cannot return file position: %s\r\n", strerror(errno));
		return pos;
	}

	ftypSize 		= sizeof(qta_ftyp);
	tempMdatSize 	= pos - ftypSize;

	SetMoovParams(mp4MoovData);
	FillMovieHeader(mp4MoovData);

	if(fwrite(mp4MoovData->moov, mp4MoovData->moovSize, 1, file) != 1) 
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Write proccess failed: %s\r\n", strerror(errno));
		return FAILURE;
	}
	if(fseeko64(file, 0, SEEK_SET))
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Cannot seek for the beginning of the file: %s\r\n", strerror(errno));
		return FAILURE;
	}
	if(fseeko64(file, ftypSize, SEEK_CUR))
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Cannot seek for the needed position of the file: %s\r\n", strerror(errno));
		return FAILURE;
	}
	mdatSize = SWAP(tempMdatSize);
	if(fwrite(&mdatSize, 4, 1, file) != 1) 
	{
		if(mp4MoovData->moov)
		{
			free(mp4MoovData->moov);
		}
		if(file)
		{
			fclose(file);
		}
		file = NULL;
		ERR("Write proccess failed: %s\r\n", strerror(errno));
		return FAILURE;
	}

	if(mp4MoovData->moov)
	{
		free(mp4MoovData->moov);
	}
	if(file)
	{
		fclose(file);
	}
	file = NULL;

	// rename mp4 file
   	sprintf(outFileName, "/media/card/video_%.4i_%.2i_%.2i_%.2i-%.2i-%.2i.mp4", 
    mp4MoovData->startTime.tm_year + 1900, mp4MoovData->startTime.tm_mon + 1, mp4MoovData->startTime.tm_mday,
    mp4MoovData->startTime.tm_hour, mp4MoovData->startTime.tm_min, mp4MoovData->startTime.tm_sec);
    sprintf(inFileName, "/media/card/new_video_file.mp4");
    ret = remove("/media/card/new_video_file.temp");
    if(ret !=0)
    {
    	ERR("File new_video_file.temp cannot be deleted: %s\r\n", strerror(errno));
    }
   	
    ret = rename(inFileName, outFileName);
    if(ret !=0)
    {
    	ERR("File new_video_file.mp4 cannot be rename: %s\r\n", strerror(errno));
    }

   	sync();
	debug("Saved MP4 file %s\r\n", outFileName);

	return SUCCESS;
}


// сохранить данные для восстановления в файл
int SaveRecoveryData(t_moov_params* mp4MoovData)
{

	FILE *f = NULL;
    f = fopen64("/media/card/new_video_file.temp", "ab+");
    if (f == NULL) 
    {
        ERR("Failed to open new_video_file.temp for writing\r\n");
        return FAILURE;
    }

    // update recdata
    mp4MoovData->vrecStcoPos 	= mp4MoovData->vrecStcoPos - 3;
	mp4MoovData->vrecStszPos 	= mp4MoovData->vrecStszPos - 3;
	mp4MoovData->vrecStssPos 	= mp4MoovData->vrecStssPos - 3;

    mp4MoovData->recv_stco[2] 	= SWAP(mp4MoovData->vrecStcoPos); 
    mp4MoovData->recv_stsz[2] 	= SWAP(mp4MoovData->vrecStszPos);
    mp4MoovData->recv_stss[2] 	= SWAP(mp4MoovData->vrecStssPos);
#ifdef SOUND_EN

   	mp4MoovData->arecStszPos 	= mp4MoovData->arecStszPos - 3;
	mp4MoovData->arecStcoPos	= mp4MoovData->arecStcoPos - 3;

    mp4MoovData->reca_stsz[2] = SWAP(mp4MoovData->arecStszPos);
    mp4MoovData->reca_stco[2] = SWAP(mp4MoovData->arecStcoPos);
#endif

    if(fwrite(mp4MoovData->recdata, mp4MoovData->recDataSize, 1, f) != 1) 
    {
    	ERR("Failed writing to new_video_file.temp: mp4MoovData->recdata\r\n");
    	fclose(f);
    	f = NULL;
    	return FAILURE;
    }

    fclose(f);
    f = NULL;

#ifdef SOUND_EN
    mp4MoovData->arecStszPos 	= 3;
	mp4MoovData->arecStcoPos	= 3;
#endif
	mp4MoovData->vrecStcoPos 	= 3;
	mp4MoovData->vrecStszPos 	= 3;
	mp4MoovData->vrecStssPos 	= 3;

    return SUCCESS;
}
