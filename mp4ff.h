/*
 * mp4ff.h
 *
*/

#ifndef _MP4FF_H
#define _MP4FF_H

#include <sys/time.h>

#include <ti/sdo/dmai/Buffer.h>

#include "common.h"

#define RESERVE_SPACE 	5120//20480						// reserve RESERVE_SPACE kbytes for write tail

typedef struct _moov_params
{
	struct tm 	startTime;

	int 		moovSize;
	int 		vTrackSize;
	int 		vStsdSize;
	int 		avcCSize;
	int 		vStssSize;
	int 		vStcoSize;
	int 		vStszSize;

	int 		vTrackPos;
	int 		vStsdPos;
	int 		avcCPos;
	int 		vStssPos;
	int 		vStcoPos;
	int 		vStszPos;

	int			frameCount;
	int 		stssCount;

	time_t		creationTime;		

	u8     	   *moov;
	u8         *v_stsd;			// sample description AvcC
	u32        *v_stsz;			// samples sizes
	u32        *v_stco;			// chunks offsets
	u32        *v_stss;			// numbers of sync samples (key frames)
	u8			avcC[256];

	u8 		   *recdata;
	u32        *recv_stco;
	u32        *recv_stsz;
	u32        *recv_stss;

	int 		vrecStssPos;
	int 		vrecStcoPos;
	int 		vrecStszPos;

	int 		recDataSize;

#ifdef SOUND_EN
	u32        *reca_stsz;
	u32        *reca_stco;

	int 		arecStcoPos;
	int 		arecStszPos;

	int 		sTrackSize;
	int 		sStscSize;
	int 		sStcoSize;
	int 		sStszSize;

	int 		sTrackPos;
	int 		sStscPos;
	int 		sStcoPos;
	int 		sStszPos;

	int			chunkCount;
	int			chunkGroupsCount;

	u32        *s_stsc;			// sample-to-chunk relation: how much chunks are in one sample
	u32        *s_stsz;			// samples sizes
	u32        *s_stco;			// chunk groups offsets

	int	 		chunkCountInGroup;
	int			chunksSizeInGroup;
	u8 			chunkGroup[AUDIO_CHUNK_GROUP_SIZE];

#endif //SOUND_EN

} t_moov_params;

// public
int 		IsVideoFrameKey(Buffer_Handle videoBuf);
int			CheckCardFreeSpace(FILE *fp);
int 		RepairSDCard(const char *pDisk);
FILE* 		NewMP4File(t_moov_params *mp4MoovData, struct tm *startTime);
int 		GetAvcCFromH264(t_moov_params *mp4MoovData, Buffer_Handle buf);
int 		AddVideoFrame(t_moov_params *mp4MoovData, Buffer_Handle videoBuf, FILE *file);
int 		SaveAndCloseMP4File(FILE *file, t_moov_params *mp4MoovData);
void 		CloseMP4File(FILE *file, t_moov_params *mp4MoovData);
int 		SaveRecoveryData(t_moov_params* mp4MoovData);
#ifdef SOUND_EN
int 		AddAudioChunk(t_moov_params *mp4MoovData, Buffer_Handle audioBuf, FILE *file);
#endif //SOUND_EN

// private (используются тольк овнутри .c файла)
// long long 	GetDiskFreeSpace(const char* pDisk);
// void 		CheckReserveSpace(void);
// int 			AllocMovieTables(t_moov_params* mp4MoovData);
// int    		GetVideoParams(u8* h264FilePtr, int h264FileSize, int* IsKey, u32* ptime);
// void 		MovieAddFrame(t_moov_params* mp4MoovData, long long pos, int size, int keyFrame);
// int 			MovieAddChunksGroup(t_moov_params* mp4MoovData, FILE * file);
// void 		SetMoovParams(t_moov_params* mp4MoovData);
// void 		FillMovieHeader(t_moov_params* mp4MoovData);
// int 			RecoverMoovDataFromFile(FILE *file, t_moov_params *mp4MoovData);


 #endif /* _MP4FF_H */