/*
 * audio.c
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/
#include <errno.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <sys/time.h>
#include <xdc/std.h>

#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/dmai/ce/Aenc1.h>

#include "common.h"
#include "logging.h"
#include "transport_buffers.h"

#include "audio.h"

#define BUFSIZEALIGN            128


void *audioThrFxn(void *arg)
{
    debug("Audio thread started\r\n");

    void                   *status              = THREAD_SUCCESS;
    AudioEnv               *envp                = (AudioEnv *) arg;
    Buffer_Attrs            bAttrs              = Buffer_Attrs_DEFAULT;
    BufTab_Handle           hBufTab             = NULL;
    AUDENC1_Params          params              = Aenc1_Params_DEFAULT;
    AUDENC1_DynamicParams   dynParams           = Aenc1_DynamicParams_DEFAULT;
    Engine_Handle           hEngine             = NULL;
    Aenc1_Handle            hAe1                = NULL;
    Buffer_Handle           hSampleBuf;
    Buffer_Handle           hDstBuf;
    int 					ret;
    int                     iters;
    int                     chunks              = 0;
    u8                      trdFlags            = 0;
    struct timespec         cond_time;
    u64                     timestamp_aenc;

    params.encMode      = IAUDIO_CBR;
    params.sampleRate   = dynParams.sampleRate  = SAMPLING_FREQUENCY;
    params.bitRate      = dynParams.bitRate     = AUDIO_BITRATE;
    params.channelMode  = dynParams.channelMode = (audio_channels == 2 ? IAUDIO_2_0 : IAUDIO_1_0); // mono or stereo

    pthread_mutex_lock(envp->encEngineLock);    // разделение доступа между нитью сжатия аудио и нитью сжатия видео

    /* Open the codec engine */
    hEngine = Engine_open(envp->engineName, NULL, NULL);
    if (hEngine == NULL) 
    {
        ERR("Failed to open codec engine %s\r\n", envp->engineName);
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        is_stream_failed    = 1;
        is_enc_finishing    = 1;
        pthread_mutex_unlock(envp->encEngineLock);
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
    }

    pthread_mutex_unlock(envp->encEngineLock);

    /* Create the audio encoder */
    hAe1 = Aenc1_create(hEngine, envp->audioEncoder, &params, &dynParams);
    if (hAe1 == NULL) 
    {
        ERR("Failed to create audio decoder: %s\r\n", envp->audioEncoder);
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        is_stream_failed    = 1;
        is_enc_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
    }

    /* Create the output buffer */
    envp->outBufSize  = Dmai_roundUp(Aenc1_getOutBufSize(hAe1), BUFSIZEALIGN);

    //hDstBuf = Buffer_create(envp->outBufSize, &bAttrs);

    hBufTab = BufTab_create(TBUF_NUMBER, envp->outBufSize, &bAttrs);
    if (hBufTab == NULL) 
    {
        ERR("Failed to allocate contiguous buffers\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        is_stream_failed    = 1;
        is_enc_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
    }

    // зарегистрировать буферы в группе, чтобы другие нити имели доступ к сжатым порциям данных
    RegisterTransportBufGroup(eAENCODER_SRC, hBufTab); 

    /* Signal that initialization is done and wait for other threads */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    // подключаем тред к пулу буферов, чтобы он мог запрашивать буферы с захваченным аудио
    PlugToTransportBufGroup(vcAEncode_trd, trdFlags, eACAPTURE_SRC);

    iters = 0;

    while (!gblGetQuit(REC_QID | STRM_QID))  // пока не придет сигнал останова
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); // если цикл затянулся - сбрасываем watchdog

        while(1)
        {
            if(gblGetQuit(REC_QID | STRM_QID))
            {
                is_enc_finishing    = 1;
                goto cleanup;
            }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

            // Get a buffer to encode from the capture thread
            GetBufferToRead(vcAEncode_trd, eACAPTURE_SRC, &hSampleBuf, NULL, &timestamp_aenc, NULL, NULL);
            if(hSampleBuf != NULL)
            {
                break;
            }
            else // ждем, если буфер не готов сразу
            {
                makewaittime(&cond_time, 0, 500000000); // 500 ms
                pthread_mutex_lock(&rcond_mutex[eACAPTURE_SRC]);
                ret = pthread_cond_timedwait(&rbuf_cond[eACAPTURE_SRC], &rcond_mutex[eACAPTURE_SRC], &cond_time);
                if(ret != 0)
                {
                    if(ret == ETIMEDOUT)
                    {
                        ERR("AudioEncoder failed to get a buffer to read\r\n");
                    }
                    else if(ret != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                    }
                    pthread_mutex_unlock(&rcond_mutex[eACAPTURE_SRC]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    is_rec_failed       = 1;
                    is_stream_failed    = 1;
                    is_enc_finishing    = 1;
                    cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
                }
                pthread_mutex_unlock(&rcond_mutex[eACAPTURE_SRC]);
            }
        }

        while(1)
        {
            if(gblGetQuit(REC_QID | STRM_QID))
            {
                is_enc_finishing    = 1;
                goto cleanup;
            }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

            GetBufferToWrite(eAENCODER_SRC, &hDstBuf, 0);
            if(hDstBuf != NULL)
            {
                break;
            }
            else // ждем, если буфер не готов сразу
            {
                makewaittime(&cond_time, 0, 500000000); // 500 ms
                pthread_mutex_lock(&wcond_mutex[eAENCODER_SRC]);
                ret = pthread_cond_timedwait(&wbuf_cond[eAENCODER_SRC], &wcond_mutex[eAENCODER_SRC], &cond_time);
                if(ret != 0)
                {
                    if(ret == ETIMEDOUT)
                    {
                        ERR("AudioEncoder failed to get a buffer to write\r\n");
                    }
                    else if(ret != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                    }
                    pthread_mutex_unlock(&wcond_mutex[eAENCODER_SRC]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    is_rec_failed       = 1;
                    is_stream_failed    = 1;
                    is_enc_finishing    = 1;
                    cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
                }
                pthread_mutex_unlock(&wcond_mutex[eAENCODER_SRC]);
            }
        }

        log_threads("enter a_enc\r\n");
        /* Encode the audio buffer */
        if (Aenc1_process(hAe1, hSampleBuf, hDstBuf) < 0) 
        {
            ERR("Failed to encode audio buffer\r\n");
            logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            is_rec_failed       = 1;
            is_stream_failed    = 1;
            is_enc_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
        }

        log_threads("exit a_enc\r\n");

        // if(Buffer_copy (hSampleBuf, hDstBuf) < 0){
        //     ERR("Failed to copy audio buffer\r\n");
        //     is_rec_failed       = 1;
        //     is_stream_failed    = 1;
        //     is_enc_finishing    = 1;
        //     cleanup(THREAD_FAILURE, REC_QID | STRM_QID);
        // }

        BufferReadComplete(vcAEncode_trd, eACAPTURE_SRC, hSampleBuf); // закончили чтение буфера с несжатым звуком
        SetBufferReady(eAENCODER_SRC, hDstBuf, timestamp_aenc, noKey); // готов новый буфер со сжатым звуком

        iters++; // счетчик итераций в течение часа (каждый час пишем новый файл)
        chunks++; // счетчик сжатых аудиочанков от начала захвата
        // if(iters % 20 == 0)
        // {
        //     debug("aud_enc: %i\r\n", iters);
        // }

        if(iters == AUDIO_CHUNKS_PER_HOUR)
        {
            iters = 0;
        }
    }

cleanup:

    debug("Audio thread finishing\r\n");

    is_enc_finishing    = 1;

    // больше не будем использовать буферы с несжатым звуком
    ReleaseTransportBufGroup(vcAEncode_trd, eACAPTURE_SRC);
    
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
    /* Make sure the other threads aren't waiting for us */
    if(envp->hRendezvousAllEncFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllEncFinished);
    }

    if(hBufTab != NULL)
    {
        FreeTransportBufGroup(eAENCODER_SRC); // больше не будем заполнять буферы сжатыми аудиочанками
    }

    /* Clean up the thread before exiting */
    if(hBufTab != NULL)
    {
        ret = BufTab_delete(hBufTab);
        if(ret < 0)
        {
            ERR("Failed to delete audio encoder buffers %i\r\n", ret);
        }
    }
    if(hAe1) 
    {
        ret = Aenc1_delete(hAe1);
        if(ret < 0)
        {
            ERR("Failed to delete audio encoder %i\r\n", ret);
        }
    }
    if(hEngine) 
    {
        Engine_close(hEngine);
    }
    // if(hDstBuf)
    // {
    //     Buffer_delete(hDstBuf);
    // }
    debug("Audio coder thread finished\r\n");
    return status;
}
