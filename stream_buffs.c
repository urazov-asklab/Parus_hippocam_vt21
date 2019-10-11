//stream_buffs.c

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h> 

#include "common.h"
#include "writer_data.h"
#include "transport_buffers.h"

#include "stream_buffs.h"

struct timeval          get_time;
struct timeval          previn_time;
struct timeval          in_time;
int                     vbuf_stop = 0;
int                     abuf_stop = 0;


// буферы для стриминга по wifi выделяем и удаляем каждый раз при старте и по завершении стриминга
void *StreamBuffsThrFxn(void *arg) 
{   
    debug("Streaming started\r\n");
    u8              trdFlags = 0;
    StreamBuffsEnv *envp     = (StreamBuffsEnv *) arg;
    void           *status   = THREAD_SUCCESS;

    if(PlugToTransportBufGroup(vcStreamB_trd, trdFlags, eVENCODER_SRC) != 1)
    {
        ERR("Failed to plug buffers to streaming thread\r\n");
        is_stream_failed    = 1;
        is_stream_finishing = 1;
        cleanup(THREAD_FAILURE, STRM_QID);
    }
#ifdef SOUND_EN
    if(PlugToTransportBufGroup(vcStreamB_trd, trdFlags, eAENCODER_SRC) != 1)
    {
        ERR("Failed to plug buffers to streaming thread\r\n");
        is_stream_failed    = 1;
        is_stream_finishing = 1;
        cleanup(THREAD_FAILURE, STRM_QID);
    }
#endif

cleanup:

    // подключились к храниличу видеокадров и аудиочанков, ждём сигнала, что надо отключиться

    if(envp->hRendezvousFinishSTRM != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinishSTRM);
    }

    is_stream_finishing = 1;

    while(1)
    {
        if(vbuf_stop == 1) // Отключаемся от буферов только если никакие функции из файла с ними не работают
        {
            ReleaseTransportBufGroup(vcStreamB_trd, eVENCODER_SRC);
            break;
        }
    }
#ifdef SOUND_EN
    while(1)
    {
        if(abuf_stop == 1)
        {
            ReleaseTransportBufGroup(vcStreamB_trd, eAENCODER_SRC);
            break;
        }
    }
#endif

    debug("Streaming thread finished\r\n");
    return status;
}

// эти функции вызываются библиотекой live и отдают туда данные

int GetMP4_Serial(AV_DATA *avdata)  // вернуть номер кадра видео по порядку
{
    log_threads("enter GetMP4_Serial\r\n");

    int             err         = 0;
    int             ser         = -1;  
    u8              isKeyFlag   = -1;
    struct timespec cond_time;

    vbuf_stop = 0;

    while(1)
    {
        err = GetLastBuffer(vcStreamB_trd, eVENCODER_SRC, NULL, NULL, NULL, (u32*) &ser, &isKeyFlag);
        if(ser > 0)
        {
            avdata->serial = ser;
            avdata->flags = (isKeyFlag != 0) ? AV_FLAGS_MP4_I_FRAME : 0;
            log_threads("exit GetMP4_Serial\r\n");
            vbuf_stop = 1;
            return 0;
        }
        else
        {
            makewaittime(&cond_time, 0, 500000000);            // 500 ms
            pthread_mutex_lock(&rcond_mutex[eVENCODER_SRC]);
            err = pthread_cond_timedwait(&rbuf_cond[eVENCODER_SRC], &rcond_mutex[eVENCODER_SRC], &cond_time);
            if(err != 0)
            {
                if((err == ETIMEDOUT) && (is_stream_request == 0))
                {
                    log_threads("exit GetMP4_Serial\r\n");
                    ERR("GetMP4_Serial failed!\r\n");
                    pthread_mutex_unlock(&rcond_mutex[eVENCODER_SRC]);
                    vbuf_stop   = 1;
                    strm_error  = 1;
                    return FAILURE;
                }
                else if(err != ETIMEDOUT)
                {
                    ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                    pthread_mutex_unlock(&rcond_mutex[eVENCODER_SRC]);
                    vbuf_stop   = 1;
                    strm_error  = 1;
                    return FAILURE;
                }
            }
            pthread_mutex_unlock(&rcond_mutex[eVENCODER_SRC]);
        }
    }

    log_threads("exit GetMP4_Serial\r\n");
    return 1;
}

int GetMP4Vol(AV_DATA *avdata)  // вернуть SPS и PPS
{
    log_threads("enter GetMP4Vol\r\n");

    struct timespec cond_time;
    int             ret         = 0;

    while(mp4_vol_size == 0)
    {
        makewaittime(&cond_time, 0, 500000000); // 500 ms
        pthread_mutex_lock(&mp4_vol_mutex);
        ret = pthread_cond_timedwait(&mp4_vol_cond, &mp4_vol_mutex, &cond_time);
        if(ret != 0) 
        {
            if(ret == ETIMEDOUT)
            {
                log_threads("exit GetMP4Vol\r\n");
                ERR("GetMP4Vol failed! size %i\r\n", mp4_vol_size);
            }
            else if(ret != ETIMEDOUT)
            {
                ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
            }
            pthread_mutex_unlock(&mp4_vol_mutex);
            return FAILURE;
        }
        pthread_mutex_unlock(&mp4_vol_mutex);
    }

    avdata->ptr = (char*) mp4_vol;
    avdata->size = mp4_vol_size;

    log_threads("exit GetMP4Vol\r\n");
    return 0;
}

int get_data_nal_unit_pos(u8 *src, u32 src_size)
{
    log_threads("enter get_data_nal_unit_pos\r\n");
    int pos = 0;
    do
    {
        if((src[pos] == 0) && (src[pos+1] == 0) && (src[pos+2] == 0) && (src[pos+3] == 1))
        {
            if(((src[pos+4] & 0x1F) == 5) || ((src[pos+4] & 0x1F) == 1))
            {
                log_threads("exit get_data_nal_unit_pos\r\n");
                return pos;
            }
        }
        pos++;
    }
    while((pos + 5 < src_size)||(pos < 512));

    log_threads("exit get_data_nal_unit_pos\r\n");
    return 0;
}

int prev_num;

int GetMP4_Frame(int num, AV_DATA *avdata) // вернуть кадр видео по номеру
{
    log_threads("enter GetMP4_Frame\r\n");
    int             data_pos    = 0;
    u32             bufSize     = 0;
    u8              isKeyFlag   = -1;
    u8             *buffPtr     = NULL;
    Buffer_Handle   curBuff     = NULL;
    u64             timestamp;
    struct timeval  frame_time;

    GetBufferByNumToRead(vcStreamB_trd, eVENCODER_SRC, num, &curBuff, &bufSize, &timestamp, &isKeyFlag);
    if(curBuff != NULL)
    {
        buffPtr             = (u8 *) Buffer_getUserPtr(curBuff);
        data_pos            = get_data_nal_unit_pos(buffPtr, bufSize);

        avdata->ptr         = (char *)(buffPtr + data_pos);
        avdata->size        = bufSize - data_pos;
        avdata->flags       = (isKeyFlag != 0) ? AV_FLAGS_MP4_I_FRAME : 0;

        gettimeofday(&frame_time, NULL); 
        // временную метку генерируем при отправке (не берем из структуры), 
        // т.к. во время записи кадров время может переводиться вперед/назад с помощью настроек
        avdata->timestamp   = frame_time.tv_sec * (u64)MS_IN_SEC + frame_time.tv_usec / (u64)MS_IN_SEC;
        avdata->serial      = num;
        internal_error      = 0;
        return 0;
    }
    return 1;
}

int FreeMP4_Frame(int num)  // библиотека прочитала кадр - помечаем это в хранилище кадров
{
    BufferReadCompleteByNum(vcStreamB_trd, eVENCODER_SRC, num);
    return 0;
}

int GetMP4_I_Frame(AV_DATA *avdata)  // вернуть ключевой кадр
{
    log_threads("enter GetMP4_I_Frame\r\n");
    int             ser         = -1;
    u32             bufSize     = 0;
    int             data_pos    = 0;
    u8             *buffPtr     = NULL;
    Buffer_Handle   curBuff     = NULL;
    u64             timestamp;
    struct timeval  frame_time;

    GetLastKeyBufferToRead(vcStreamB_trd, eVENCODER_SRC, &curBuff, &bufSize, &timestamp, (u32*) &ser);
    if(ser > 0)
    {
        buffPtr             = (u8 *) Buffer_getUserPtr(curBuff);
        data_pos            = get_data_nal_unit_pos(buffPtr, bufSize);

        avdata->ptr         = (char *) (buffPtr + data_pos);
        avdata->size        = bufSize - data_pos;
        avdata->flags       = AV_FLAGS_MP4_I_FRAME;

        gettimeofday(&frame_time, NULL); 
                // временную метку генерируем при отправке (не берем из структуры), 
        // т.к. во время записи кадров время может переводиться вперед/назад с помощью настроек
        avdata->timestamp   = frame_time.tv_sec * (u64)MS_IN_SEC + frame_time.tv_usec / (u64)MS_IN_SEC;
        avdata->serial      = ser;
        log_threads("exit GetMP4_I_Frame\r\n");
        return 0;
    }
    log_threads("exit GetMP4_I_Frame\r\n");
    return 1;
}

int GetAACSerial(AV_DATA *avdata)   // вернуть номер аудиочанка (кусок в 1024 аудиосэмплов)
{
    log_threads("enter GetAACSerial\r\n");

    int                 ser         = -1;  
    int                 err;  
    struct timespec     cond_time;

    abuf_stop = 0;

    while(1)
    {
        GetLastBuffer(vcStreamB_trd, eAENCODER_SRC, NULL, NULL, NULL, (u32*) &ser, NULL);
        if(ser > 0)
        {
            avdata->serial = ser;
            avdata->flags = 0;
            log_threads("exit GetAACSerial\r\n");
            abuf_stop = 1;
            return 0;
        }
        else
        {
            makewaittime(&cond_time, 0, 500000000); // 500 ms
            pthread_mutex_lock(&rcond_mutex[eAENCODER_SRC]);
            err = pthread_cond_timedwait(&rbuf_cond[eAENCODER_SRC], &rcond_mutex[eAENCODER_SRC], &cond_time);
            if(err != 0) 
            {
                if((err == ETIMEDOUT) && (is_stream_request == 0))
                {
                    log_threads("exit GetAACSerial\r\n");
                    ERR("GetAACSerial failed!\r\n");
                    pthread_mutex_unlock(&rcond_mutex[eAENCODER_SRC]);
                    abuf_stop   = 1;
                    strm_error  = 1;
                    return FAILURE;
                }
                else if(err != ETIMEDOUT)
                {
                    ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                    pthread_mutex_unlock(&rcond_mutex[eAENCODER_SRC]);
                    abuf_stop   = 1;
                    strm_error  = 1;
                    return FAILURE;
                }
            }
            pthread_mutex_unlock(&rcond_mutex[eAENCODER_SRC]);
        }
    }
    log_threads("exit GetAACSerial\r\n");
    return 1;
}

int GetAACFrame(int num, AV_DATA *avdata) // вернуть аудиочанк по номеру
{
    log_threads("enter GetAACFrame\r\n");
    
    u32             bufSize     = 0;
    u8             *buffPtr     = NULL;
    Buffer_Handle   curBuff     = NULL;
    u64             timestamp;
    struct timeval  frame_time;

    GetBufferByNumToRead(vcStreamB_trd, eAENCODER_SRC, num, &curBuff, &bufSize, &timestamp, NULL);
    if(curBuff != NULL)
    {

        buffPtr             = (u8 *) Buffer_getUserPtr(curBuff);
        avdata->ptr         = (char *) (buffPtr + 7);               // ADTS header only for that format!
        avdata->size        = bufSize - 7;
        avdata->flags       = 0;

        gettimeofday(&frame_time, NULL); 
        // временную метку генерируем при отправке (не берем из структуры), 
        // т.к. во время записи кадров время может переводиться вперед/назад с помощью настроек
        avdata->timestamp   = frame_time.tv_sec * (u64)MS_IN_SEC + frame_time.tv_usec / (u64)MS_IN_SEC;
        avdata->serial      = num;
        if(sound_only == 1)
        {
            internal_error = 0;
        }
        log_threads("exit GetAACFrame\r\n");
        return 0;
    }
    log_threads("exit GetAACFrame\r\n");
    return 1;
}

int FreeAACFrame(int num)       // библиотека прочитала аудиочанк - помечаем это в хранилище кадров
{
    BufferReadCompleteByNum(vcStreamB_trd, eAENCODER_SRC, num);
    return 0;
}