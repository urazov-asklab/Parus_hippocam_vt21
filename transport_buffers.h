//transport_buffers.h

#ifndef	_TRANSPORT_BUFFERS_H
#define _TRANSPORT_BUFFERS_H

#include <ti/sdo/dmai/Buffer.h>

#define TBUF_NUMBER			40 	// число буферов в группе
#define MAX_BUF_GROUP_NUM	8 	// максимальное количество групп
#define MAX_READERS_NUM		8 	// максимальное количество читателей для одного буфера

#define KEY_FRAME 			1

#define WAIT 				1
#define NOWAIT 				0

typedef enum
{
	no_flags,
} eBufferFlags;

typedef enum
{
	eNO_SRC,			// 0
	eVCAPTURE_SRC,		// 1
	eVENCODER_SRC,		// 2
	eVENCODERRF_SRC,	// 3
	eVRESIZER_SRC,		// 4
	eVRFCRYPTER_SRC,	// 5
	eACAPTURE_SRC,		// 6
	eAENCODER_SRC		// 7
} eBufferGroups;

typedef enum
{
	eBUF_FREE,			// 0
	eBUF_READY,			// 1
	eBUF_READING,		// 2
	eBUF_WRITING		// 3
} eBufferStatus;

typedef enum
{
    vcEncode_trd,		// two instantiations of this thread
    vcResize_trd,
    vcAVRec_trd,
    vcStreamB_trd,
    vcRFComm_trd,
    vcRFTX_trd,
    vcAEncode_trd
} eTransport_Buffer_Clients;

typedef enum
{
	noKey = 0,
	isKey = 1		// for buffers those stores encoded video data
} eProperties;

typedef	struct	buffer_dsc
{
	Buffer_Handle	hBuf;
	u32				data_size;
	eBufferStatus	status;
	eProperties     properties;
	u8				number_of_reading;
	u64				timestamp;				//ms
	u32				number;
	u32				clients;
} buffer_dsc;
	
typedef struct  reader_info
{
    u8	is_busy;
    u8	flags;
} reader_info;
	
pthread_mutex_t buf_mutex[MAX_BUF_GROUP_NUM];
pthread_mutex_t rcond_mutex[MAX_BUF_GROUP_NUM];
pthread_mutex_t wcond_mutex[MAX_BUF_GROUP_NUM];
pthread_cond_t 	rbuf_cond[MAX_BUF_GROUP_NUM];
pthread_cond_t 	wbuf_cond[MAX_BUF_GROUP_NUM];
	
void InitTransportBuffers();

// readers functions
int  PlugToTransportBufGroup(u8 reader_num, u8 flags, int grp_num);
u8   GetBufferToRead(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, u32 *sequence_num, 
					u8 *isKeyFlag);
u8 	 GetLastBuffer(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, u32 *sequence_num, 
	 				u8 *isKeyFlag);
u8 	 GetLastKeyBufferToRead(u8 reader_num, int grp_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, 
	 				u32 *sequence_num);
u8   GetBufferByNumToRead(u8 reader_num, int grp_num, u32 sequence_num, Buffer_Handle *hBuf, u32 *size, u64 *timestamp, 
	 				u8 *isKeyFlag);
void BufferReadComplete(u8 reader_num, int grp_num, Buffer_Handle hBuf);
void BufferReadCompleteByNum(u8 reader_num, int grp_num, int frame_num);
void ReleaseTransportBufGroup(u8 reader_num, int grp_num);
int  GetFreeBuffersNum(u8 reader_num, int grp_num);
int  IsKeyFramePresent(u8 reader_num, int grp_num);
int  GetAnyBufferPtr(int grp_num, u8 **ptr);

// writer functions
u8   RegisterTransportBufGroup(int grp_num, BufTab_Handle hBufTab);
u8   AddBuffers(int grp_num, BufTab_Handle hBufTab);
void SetBufferReady(int grp_num, Buffer_Handle hBuf, u64 timestamp, u8 keyF);
void GetBufferToWrite(int grp_num, Buffer_Handle *hBuf, int getanyway);
void FreeTransportBufGroup(int grp_num);

// internal functions
int  CheckReader(u8 reader_num, int grp_num);
int  AddReader(u8 reader_num, u8 flags, int grp_num);
int  RemoveReader(u8 reader_num, int grp_num);

#endif /* _TRANSPORT_BUFFERS_H */
