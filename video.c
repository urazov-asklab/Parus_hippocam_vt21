/*
 * video.c
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
#include <string.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <sys/time.h>
#include <xdc/std.h>
#include <linux/videodev2.h>

#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/VideoStd.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/dmai/ce/Venc1.h>

#include "I2p.h"
#include "common.h"
#include "logging.h"
#include "mp4ff.h"
#include "transport_buffers.h"

#include "video.h"

#define NUM_DEI_VIDEO_BUFS      1
#define RSZ_BITRATE             100000

void getm4vol(u8 *src, int src_size, u8 *dst, int *size)
{
    int pos = 0;

    //SPS
    if((src[pos] != 0) || (src[pos + 1] != 0) || (src[pos + 2] != 0) || (src[pos + 3] != 1))
    {
        return;
    }
    if((src[pos + 4] & 0x1F) != 7)
    {
        return;
    }
    pos += 4;
    do
    {
        if((src[pos] == 0) && (src[pos + 1] == 0) && (src[pos + 2] == 0) && (src[pos + 3] == 1))
        {
            break;
        }
        pos++;
    }
    while((pos < src_size) && (pos < 512));

    if((pos == src_size) || (pos == 512))
    {
        return;
    }
    if((src[pos + 4] & 0x1F) != 8)
    {
        return;
    }
    pos += 4;

    do
    {
        if((src[pos] == 0) && (src[pos + 1] == 0) && (src[pos + 2] == 0) && (src[pos + 3] == 1))
        {
            break;
        }
        pos++;
    }
    while((pos < src_size) && (pos < 512));

    if((pos == src_size) || (pos == 512))
    {
        return;
    }
    memcpy(dst, src, pos);

    *size = pos;

    pthread_mutex_lock(&mp4_vol_mutex);
    pthread_cond_broadcast(&mp4_vol_cond);
    pthread_mutex_unlock(&mp4_vol_mutex);
}


void *videoThrFxn(void *arg)
{
    debug("Video thread started\r\n");

    VideoEnv               *envp                = (VideoEnv *) arg;
    void                   *status              = THREAD_SUCCESS;
    Buffer_Attrs            bAttrs              = Buffer_Attrs_DEFAULT;
    VIDENC1_Params          defaultParams       = Venc1_Params_DEFAULT;
    VIDENC1_DynamicParams   defaultDynParams    = Venc1_DynamicParams_DEFAULT;
    BufferGfx_Attrs         gfxAttrs            = BufferGfx_Attrs_DEFAULT;
    Venc1_Handle            hVe1                = NULL;
    Engine_Handle           hEngine             = NULL;
    Buffer_Handle           hCaptureBuf         = NULL;
    Buffer_Handle           hDstBuf             = NULL;
    BufTab_Handle           hBufTab             = NULL;
    //FILE                   *filePtr           = NULL;

    int                     vid_enc_num         = 1;
    u8                  	trdFlags        	= 0;
    u64                     timestamp_venc;
    u64                     diff_t;
    // char                    fileName[256];
    int                     frames              = 0;
    int                     iters;
    int                     isKeyFlag;
    // int                     i;
    Int32                   width;
    Int32                   height;
    int 					ret;
    eBufferGroups           sink_bgroup_num 	= eNO_SRC;
    VIDENC1_Params         *params;
    VIDENC1_DynamicParams  *dynParams;    
    struct timeval          first_frame_time;
    struct timeval          frame_time;
    struct timespec         cond_time;
    // time_t                  seconds;
    // struct tm               timeinfo;    
    Command                 currentCommand;
    I2P_Params              i2pParams;
    UNIVERSAL_DynamicParams dynDeILParams;
    I2p_Handle              hDeILcr             = NULL;
//    Buffer_Handle           hCapPrevBuf         = NULL;
    Buffer_Handle           hDeILBuf            = NULL;
    int                     bufSizeDeIL         = 0;
    int                     data_size           = 0;
    //int                     is_rftx_fail_val    = 0;
    int                     is_rec_fail_val     = 0;
    int                     is_stream_fail_val  = 0;
    u8                     *hDstBufPtr          = NULL;
    int                     thread_qid 			= 0;
    u64                     iters_done     		= 0;
    u64                     a 					= 0;

    // эта нить может иметь 2 экземпляра: для сжатия основного видеопотока ...
    if(envp->src_bgroup_num == eVCAPTURE_SRC)
    {
        thread_qid          = (REC_QID | STRM_QID); 
        is_rec_fail_val     = 1;
        is_stream_fail_val  = 1;       
    }
    // ... и для сжатия потока с уменьшенным размером кадров для трансляции по радиоканалу
    else if(envp->src_bgroup_num == eVRESIZER_SRC)
    {
        thread_qid          = RFTX_QID;
        //is_rftx_fail_val    = 1;
    }

    pthread_mutex_lock(envp->encEngineLock);

    /* Open the codec engine */
    hEngine = Engine_open(envp->engineName, NULL, NULL);
    if (hEngine == NULL) 
    {
        ERR("Failed to open codec engine %s\r\n", envp->engineName);
        logEvent(log_REC_APL_INIT_FAILED);
        pthread_mutex_unlock(envp->encEngineLock);
        cleanup(THREAD_FAILURE, thread_qid);
    }

    pthread_mutex_unlock(envp->encEngineLock);

    /* Deinterlacer initialisation if enabled */
    if (deinterlace_on == 1) 
    {
        /* Use supplied params if any, otherwise use defaults */
        i2pParams.size     = sizeof(i2pParams);
        dynDeILParams.size = sizeof(dynDeILParams);

        /* Create the Deinterlacer */
        hDeILcr = I2p_create(hEngine, "i2p", &i2pParams, &dynDeILParams);
        if (hDeILcr == NULL) 
        {
            ERR("Failed to create DeInterlacer\r\n");
            logEvent(log_REC_APL_INIT_FAILED);
            cleanup(THREAD_FAILURE, thread_qid);
        }
    }

    if(video_source_num == 2)
    {
        /* Get the resolution of capture device*/
        if(VideoStd_getResolution(envp->videoStd, &width, &height) < 0) 
        {
            ERR("Failed to calculate resolution of video standard\r\n");
            logEvent(log_REC_APL_INIT_FAILED);
            cleanup(THREAD_FAILURE, thread_qid);
        }
    }
    else
    {
        width       = enc_width;
        height      = enc_height;
    }

    if(envp->src_bgroup_num == eVRESIZER_SRC)
    {
        width       = width / 4;
        height      = height / 4;

        rsz_width   = width;
        rsz_height  = height;
    }


    /* Use supplied params if any, otherwise use defaults */
    params                              = &defaultParams;
    dynParams                           = &defaultDynParams;

    /* Set up codec parameters depending on bit rate */
    /* Constant bit rate */
    params->rateControlPreset           = IVIDEO_STORAGE;

    if(envp->src_bgroup_num == eVCAPTURE_SRC)
    {
        params->maxBitRate              = video_bitrate;
        dynParams->intraFrameInterval   = (frames_per_sec != 12) ? frames_per_sec : 25;
    }
    else if(envp->src_bgroup_num == eVRESIZER_SRC)
    {
        params->maxBitRate              = RSZ_BITRATE;
        dynParams->intraFrameInterval   = frames_per_sec_rsz;
    }

    params->inputChromaFormat           = XDM_YUV_422ILE;
    dynParams->targetBitRate            = params->maxBitRate;

    // если требуется согласовать характеристики кадра с нитью захвата видео
    if(envp->hRendezvousFrameFormat != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFrameFormat);
    }

    /* Set up codec parameters */
    params->maxWidth                    = width;
    params->maxHeight                   = height;
    params->reconChromaFormat           = XDM_CHROMA_NA;

    dynParams->inputWidth               = params->maxWidth;
    dynParams->inputHeight              = params->maxHeight;

    params->maxInterFrameInterval       = KEY_FRAME_INTERVAL;
    params->maxFrameRate                = framerate;
    dynParams->refFrameRate             = framerate;
    dynParams->targetFrameRate          = framerate;

    /* Create the video encoder */
    hVe1 = Venc1_create(hEngine, envp->videoEncoder, params, dynParams);
    if (hVe1 == NULL) 
    {
        ERR("Failed to create video encoder: %s\r\n", envp->videoEncoder);
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, thread_qid);
    }

    /* Store the output buffer size in the environment */
    envp->outBufSize = Venc1_getOutBufSize(hVe1);

    if(envp->src_bgroup_num == eVCAPTURE_SRC)
    {
        sink_bgroup_num   = eVENCODER_SRC;
    }
    else if(envp->src_bgroup_num == eVRESIZER_SRC)
    {
        vid_enc_num         = 2;
        sink_bgroup_num   = eVENCODERRF_SRC;
    }

    if (deinterlace_on == 1) 
    {
        gfxAttrs.colorSpace     = ColorSpace_UYVY;
        gfxAttrs.dim.width      = width;
        gfxAttrs.dim.height     = height;
        gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(gfxAttrs.dim.width, gfxAttrs.colorSpace);

        bufSizeDeIL = BufferGfx_calcSize(envp->videoStd, ColorSpace_UYVY);
        bufSizeDeIL = (bufSizeDeIL + 4096) & (~0xFFF);
        if (bufSizeDeIL < 0) 
        {
            ERR("Failed to calculate size for capture driver buffers\r\n");
            logEvent(log_REC_APL_INIT_FAILED);            
            cleanup(THREAD_FAILURE, thread_qid);
        }

        hDeILBuf = Buffer_create(bufSizeDeIL, BufferGfx_getBufferAttrs(&gfxAttrs));
        if (hDeILBuf == NULL) 
        {
            ERR("Failed to allocate contiguous buffer for deinterlacing\r\n");
            logEvent(log_REC_APL_INIT_FAILED);
            cleanup(THREAD_FAILURE, thread_qid);
        }
        blackFill(hDeILBuf);
    }

    if (envp->outBufSize < 0) 
    {
        ERR("Failed to calculate size for encoded video buffers\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, thread_qid);
    }

    hBufTab = BufTab_create(TBUF_NUMBER, envp->outBufSize, &bAttrs);
    if (hBufTab == NULL) 
    {
        ERR("Failed to allocate contiguous buffers for video encoding\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, thread_qid);
    }

    RegisterTransportBufGroup(sink_bgroup_num, hBufTab);

    /* Signal that initialization is done and wait for other threads */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    if(PlugToTransportBufGroup(vcEncode_trd, trdFlags, envp->src_bgroup_num) < 0)
    {
        ERR("PlugToTransportBufGroup(vcEncode_trd) failed\n");
    }

    iters   = 0;

//     while(1)
//     {
//         if(is_sd_mounted == 1)
//         {
//             break
//         }
//         else
//         {
//             makewaittime(&cond_time, 0, 500000000); // 500 мс
//             pthread_mutex_lock(&sd_mount_mutex);
//             ret = pthread_cond_timedwait(&sd_mount_cond, &sd_mount_mutex, &cond_time);
//             if(ret != 0) 
//             {
//                 if(ret != ETIMEDOUT)
//                 {
//                      ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
//                 }
//                 else
//                 {
//                      ERR("SD is not mounted!\r\n");
//                 }
//                 pthread_mutex_unlock(&sd_mount_mutex);
//                 return FAILURE;
//             }
//             pthread_mutex_unlock(&sd_mount_mutex);
//         }
//     }
//     seconds  = time(NULL);
//     timeinfo = *localtime(&seconds);
//     sprintf(fileName, "/media/card/video_%.4i_%.2i_%.2i_%.2i-%.2i-%.2i.h264", 
//     //sprintf(fileName, "/media/mmcblk0/video_%.4i_%.2i_%.2i_%.2i-%.2i-%.2i.h264", 
//     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
//     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

//     /* Open the output video file for writing */
//     filePtr = fopen(fileName, "w");

//     if (filePtr == NULL) 
//     {
//         ERR("Failed to open %s for writing\r\n", fileName);
//         cleanup(THREAD_FAILURE, thread_qid);
//     }

    while(!gblGetQuit(thread_qid))
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
        
        while(1)
        {
            if(gblGetQuit(thread_qid))
            {
                is_enc_finishing    = is_rec_fail_val;
                //is_rftx_finishing   = is_rftx_fail_val;
                goto cleanup;
            }

            if(envp->src_bgroup_num == eVRESIZER_SRC)
            {
                currentCommand = gblGetCmd();
                if(currentCommand == STOP_RFTX)
                {
                    is_enc_finishing    = is_rec_fail_val;
                    //is_rftx_finishing   = is_rftx_fail_val;
                    cleanup(THREAD_SUCCESS, thread_qid);
                }
            }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

            // Get a buffer to encode from the capture thread
            GetBufferToRead(vcEncode_trd, envp->src_bgroup_num, &hCaptureBuf, NULL, &timestamp_venc, NULL, NULL);
            if(hCaptureBuf != NULL)
            {
                break;
            }
            else
            {
                makewaittime(&cond_time, 0, 500000000); // 500 ms
                pthread_mutex_lock(&rcond_mutex[envp->src_bgroup_num]);
                ret = pthread_cond_timedwait(&rbuf_cond[envp->src_bgroup_num], &rcond_mutex[envp->src_bgroup_num], 
                    &cond_time);
                if(ret != 0)
                {
                    if(ret == ETIMEDOUT)
                    {
                        ERR("VideoEncoder failed to get a buffer to read\r\n");
                    }
                    else if(ret != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                    }
                    pthread_mutex_unlock(&rcond_mutex[envp->src_bgroup_num]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    cleanup(THREAD_FAILURE, thread_qid);
                }
                pthread_mutex_unlock(&rcond_mutex[envp->src_bgroup_num]);
            }
        }

        while(1)
        {
        	if(gblGetQuit(thread_qid))
	        {
                is_enc_finishing    = is_rec_fail_val;
                //is_rftx_finishing   = is_rftx_fail_val;
	            goto cleanup;
	        }

	        if(envp->src_bgroup_num == eVRESIZER_SRC)
	        {
	            currentCommand = gblGetCmd();
	            if(currentCommand == STOP_RFTX)
	            {
                    //is_rftx_finishing   = is_rftx_fail_val;
	                cleanup(THREAD_SUCCESS, thread_qid);
	            }
	        }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

            GetBufferToWrite(sink_bgroup_num, &hDstBuf, 0);
            if(hDstBuf != NULL)
            {
                break;
            }
            else
            {
                makewaittime(&cond_time, 0, 500000000); // 500 ms
                pthread_mutex_lock(&rcond_mutex[sink_bgroup_num]);
                ret = pthread_cond_timedwait(&wbuf_cond[sink_bgroup_num], &wcond_mutex[ sink_bgroup_num], &cond_time);
                if(ret != 0) 
                {
                    if(ret == ETIMEDOUT)
                    {
                        ERR("VideoEncoder failed to get a buffer to write\r\n");
                    }
                    else if(ret != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                    }
                    pthread_mutex_unlock(&wcond_mutex[sink_bgroup_num]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    cleanup(THREAD_FAILURE, thread_qid);
                }
                pthread_mutex_unlock(&wcond_mutex[sink_bgroup_num]);
            }
        }

        if((iters % 100) == 0)
        {
            // to adjust bitrate
            dynParams->targetBitRate = video_bitrate;
            if(Venc1_control(hVe1, dynParams) < 0)
            {
                WARN("Bitrate %lu is not set!\r\n", video_bitrate);
            }
        }

        /* Make sure the whole buffer is used for input */
        BufferGfx_resetDimensions(hCaptureBuf);

        if(deinterlace_on == 1)
        {
            /* Deinterlacing the Capture frame */
            if(I2p_process(hDeILcr, hCaptureBuf, NULL, hDeILBuf) < 0) //hCapPrevBuf
            {
                ERR("Failed to Deinterlace video Buffer(iters: %i)\r\n", iters);
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                cleanup(THREAD_FAILURE, thread_qid);
            }

            // Decode the video buffer 
            if (Venc1_process(hVe1, hDeILBuf, hDstBuf) < 0)
            {
                ERR("Failed to encode video buffer\r\n");
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                cleanup(THREAD_FAILURE, thread_qid);
            }
        }
        else
        {
            log_threads("enter v_enc\r\n");
            /* Encode the video buffer */
            if (Venc1_process(hVe1, hCaptureBuf, hDstBuf) < 0) 
            {
                ERR("Failed to encode video buffer\r\n");
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                cleanup(THREAD_FAILURE, thread_qid);
            }
        }

        data_size   = Buffer_getNumBytesUsed(hDstBuf);
        hDstBufPtr  = (u8 *)Buffer_getUserPtr(hDstBuf);

        if(iters == 0) // (!!!) Is it right if we broadcast on radio?
        {
            getm4vol(hDstBufPtr, data_size, (u8 *) &mp4_vol[0], (int *) &mp4_vol_size);
        }
        if(frames == 0)
        {
            gettimeofday(&first_frame_time, NULL); 
            a = (u64)first_frame_time.tv_sec * (u64)US_IN_SEC + (u64)first_frame_time.tv_usec;
        }

        gettimeofday(&frame_time, NULL); 

        log_threads("exit v_enc\r\n");

        diff_t = ((u64)frame_time.tv_sec * (u64)US_IN_SEC + (u64)frame_time.tv_usec) - a;


        iters_done = diff_t / 40000;

        if(iters%2000 == 0)
        {
            debug("ve frames %llu(p) = %i(f)\r\n", iters_done, iters);
            sync(); // save info for video/audio recovery to card
        }

        // PRINT SPS&PPS
        // GetAvcCFromH264(NULL, hDstBuf);

        isKeyFlag = IsVideoFrameKey(hDstBuf);
        if(isKeyFlag < 0)
        {
            WARN("Failed to define video key frame \r\n");
        }
        else if(isKeyFlag == 1)
        {
            if(envp->src_bgroup_num == eVRESIZER_SRC)
            {
                got_key_frame = 1;
            }
        }

        // if (fwrite(Buffer_getUserPtr(hDstBuf),
        //            Buffer_getNumBytesUsed(hDstBuf), 1, filePtr) != 1) {
        //     ERR("Error writing the encoded data to video file\r\n");
        // }

        // if(deinterlace_on == 1)
        // {
        //     if(hCapPrevBuf != NULL)
        //     {
        //         BufferReadComplete(vcEncode_trd, envp->src_bgroup_num, hCapPrevBuf);
        //     }
        //     hCapPrevBuf = hCaptureBuf;
        // }
        // else
        // {
        BufferReadComplete(vcEncode_trd, envp->src_bgroup_num, hCaptureBuf);
        // }

        SetBufferReady(sink_bgroup_num, hDstBuf, timestamp_venc, isKeyFlag);

        frames++;
        iters++;
        // if(iters % 20 == 0)
        // {
        //    debug("vid_enc%i: %i\r\n", vid_enc_num, iters);
        // }

        if((envp->src_bgroup_num == eVCAPTURE_SRC) && (iters == frames_per_hour))
        {
            if (hVe1) 
            {
                Venc1_delete(hVe1);
            }

            if (hEngine) 
            {
                Engine_close(hEngine);
            }

            hEngine = Engine_open(envp->engineName, NULL, NULL);
            if (hEngine == NULL) 
            {
                ERR("Failed to open codec engine %s\r\n", envp->engineName);
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                cleanup(THREAD_FAILURE, thread_qid);
            }

            /* Create the video encoder */
            hVe1 = Venc1_create(hEngine, envp->videoEncoder, params, dynParams);
            if (hVe1 == NULL) 
            {
                ERR("Failed to create video encoder: %s\r\n", envp->videoEncoder);
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                cleanup(THREAD_FAILURE, thread_qid);
            }

            iters = 0;
        }
        log_threads((char *)fifoLogString);
    }

cleanup:
    debug("Video thread finishing...\r\n");

    if(status == THREAD_FAILURE)
    {
        is_rec_failed       = is_rec_fail_val;
        //is_rftx_failed      = is_rftx_fail_val;
        is_stream_failed    = is_stream_fail_val;
    }

    is_enc_finishing    = is_rec_fail_val;
    //is_rftx_finishing   = is_rftx_fail_val;
    
    ReleaseTransportBufGroup(vcEncode_trd, envp->src_bgroup_num);

    /* Make sure the other threads aren't waiting for us */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
    if(envp->hRendezvousFrameFormat != NULL)
    {
        Rendezvous_force(envp->hRendezvousFrameFormat);
    }

    if(envp->hRendezvousAllEncFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllEncFinished);
    }

    if(hBufTab != NULL)
    {
        FreeTransportBufGroup(sink_bgroup_num);
    }

    if(hBufTab != NULL)
    {
        ret = BufTab_delete(hBufTab);
        if(ret < 0)
        {
            ERR("Failed to delete video encoder buffers %i\r\n", ret);
        }
    }

    /* Clean up the thread before exiting */
    // if (filePtr) 
    // {
    //     fclose(filePtr);
    //     filePtr = NULL;
    // }

    if(hVe1) 
    {
        ret = Venc1_delete(hVe1);
        if(ret < 0)
        {
            ERR("Failed to delete video encoder %i\r\n", ret);
        }
    }
    if(hDeILcr)
    {
        I2p_delete(hDeILcr);
    }
    if(hEngine) 
    {
        Engine_close(hEngine);
    }
    if(hDeILBuf)
    {
        ret = Buffer_delete(hDeILBuf);
        if(ret < 0)
        {
            ERR("Failed to delete buffer for deinterlacer %i\r\n", ret);
        }
    }    

    debug("Video coder thread finished\r\n");
    return status;
}


