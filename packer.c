/*
 * packer.c
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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "mp4ff.h"
#include "logging.h"
#include "transport_buffers.h"
#include "common.h"

#include "packer.h"


void *packerThrFxn(void *arg)
{
    //debug("Packer thread started\r\n");

    u8                  kf              = 0;
    u8                  trdFlags        = 0;
    int                 aframes         = 0;    
    int                 aframes_old     = 0;
    int                 vframes         = 0;
    int                 vframes_old     = 0;
    int                 fails_counter   = 0;
    int                 rec_done        = 0;
    int                 skipped_frames  = 0;
    void               *status          = THREAD_SUCCESS;
    PackerEnv          *envp            = (PackerEnv *) arg;  
    // FILE               *videoOutFile    = NULL;
    FILE               *audioOutFile    = NULL;
    FILE               *mp4OutFile      = NULL;

    Buffer_Handle       hVideoOutBuf;
    Buffer_Handle       hAudioOutBuf;
    int                 new_file;
    int                 fp              = 0;
    int                 ret;
    // char                videoFileName[256];
    char                outAudFileName[256];
    t_moov_params       mp4MoovAtomData;
    time_t              seconds;
    time_t              firstTime       = time(NULL);
    time_t              secondTime;
    struct tm           timeinfo;
    struct timeval      start_thread_time;
    struct timeval      end_thread_time;
    u64                 a_timestamp     = 0;
    u64                 k_timestamp     = 0;
    u64                 v_timestamp     = 0;
    struct  timespec    cond_time;

    /* Signal that initialization is done */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    PlugToTransportBufGroup(vcAVRec_trd, trdFlags, eVENCODER_SRC);
#ifdef SOUND_EN
    PlugToTransportBufGroup(vcAVRec_trd, trdFlags, eAENCODER_SRC);
#endif
    new_file = 1;

    while (!gblGetQuit(REC_QID)) 
    {
        gettimeofday(&start_thread_time, NULL); 
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 
        if(gblGetQuit(REC_QID) 
            || is_sdcard_off_status
            )
        {
            is_rec_finishing    = 1;
            goto cleanup;
        }

        if((new_file == 1) && (sound_only != 1))
        {
            seconds  = time(NULL);
            timeinfo = *localtime(&seconds);

            while(1)
            {
                if(is_sd_mounted == 1)
                {
                    // начинаем запись только когда sd-карта примонтирована, иначе начнется запись в файловую систему
                    break;
                }
                else
                {
                    makewaittime(&cond_time, 0, 500000000); // 500 ms
                    pthread_mutex_lock(&sd_mount_mutex);
                    ret = pthread_cond_timedwait(&sd_mount_cond, &sd_mount_mutex, &cond_time);
                    if(ret != 0) 
                    {
                        if(ret == ETIMEDOUT)
                        {
                            ERR("SD is not mounted!\r\n");
                        }
                        else if(ret != ETIMEDOUT)
                        {
                            ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                        }
                        pthread_mutex_unlock(&sd_mount_mutex);
                        is_rec_failed       = 1;
                        is_rec_finishing    = 1;
                        cleanup(THREAD_FAILURE, REC_QID);
                    }
                    pthread_mutex_unlock(&sd_mount_mutex);
                }
            }

            mp4OutFile = NewMP4File(&mp4MoovAtomData, &timeinfo);
            if (mp4OutFile == NULL) 
            {
                ERR("Failed to create new MP4 file\r\n");
                is_rec_failed       = 1;
                is_rec_finishing    = 1;
                cleanup(THREAD_FAILURE, REC_QID);
            }

            firstTime = time(NULL);

            new_file = 0;
            logEvent(log_REC_APL_NEW_FRAG_BEGAN_VIDEO_ON);
        }

        if((new_file == 1) && (sound_only == 1))
        {
            // if (videoOutFile) {
            //     fclose(videoOutFile);
            //     videoOutFile = NULL;
            // }

            if (audioOutFile) 
            {
                fclose(audioOutFile);
                audioOutFile = NULL;
            }

            seconds  = time(NULL);
            timeinfo = *localtime(&seconds);

            while(1)
            {
                if(is_sd_mounted == 1)
                {
                    break;
                }
                else
                {
                    makewaittime(&cond_time, 0, 500000000); // 500 ms
                    pthread_mutex_lock(&sd_mount_mutex);
                    ret = pthread_cond_timedwait(&sd_mount_cond, &sd_mount_mutex, &cond_time);
                    if(ret != 0) 
                    {
                        if(ret == ETIMEDOUT)
                        {
                            ERR("SD is not mounted!\r\n");
                        }
                        else if(ret != ETIMEDOUT)
                        {
                            ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                        }
                        pthread_mutex_unlock(&sd_mount_mutex);
                        ERR("SD is not mounted!\r\n");
                        is_rec_failed       = 1;
                        is_rec_finishing    = 1;
                        cleanup(THREAD_FAILURE, REC_QID);
                    }
                    pthread_mutex_unlock(&sd_mount_mutex);
                }
            }
            sprintf(outAudFileName, "/media/card/audio_%.4i_%.2i_%.2i_%.2i-%.2i-%.2i.aac" , 
                
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

            // /* Open the output video file for writing */
            // videoOutFile = fopen64(videoFileName, "w");

            // if (videoOutFile == NULL) {
            //     ERR("Failed to open %s for writing\r\n", videoFileName);
            //     is_rec_failed       = 1;
            //     is_rec_finishing    = 1;
            //     cleanup(THREAD_FAILURE, REC_QID);
            // }

            /* Open the output audio file for writing */
            audioOutFile = fopen64(outAudFileName, "wb");
            if (audioOutFile == NULL) 
            {
                ERR("Failed to open %s for writing\r\n", outAudFileName);
                is_rec_failed       = 1;
                is_rec_finishing    = 1;
                cleanup(THREAD_FAILURE, REC_QID);
            }

            new_file = 0;

            logEvent(log_REC_APL_NEW_FRAG_BEGAN_VIDEO_OFF);
        }

        if((sound_only != 1) && (vframes < frames_per_hour))
        {
            GetBufferToRead(vcAVRec_trd, eVENCODER_SRC, &hVideoOutBuf, NULL, &v_timestamp, NULL, &kf);
            if(hVideoOutBuf != NULL)
            {
                vframes++;

                /* Store the encoded frame to disk */
                ret = Buffer_getNumBytesUsed(hVideoOutBuf);
                if (ret <= 0) 
                {
                    WARN("Рacker received 0 byte encoded frame\r\n");
                }
                else
                {
                    // if (fwrite(Buffer_getUserPtr(hVideoOutBuf),
                    //            Buffer_getNumBytesUsed(hVideoOutBuf), 1, videoOutFile) != 1) {
                    //     ERR("Error writing the encoded data to video file\r\n");
                    //     is_rec_failed       = 1;
                    //     is_rec_finishing    = 1;
                    //     cleanup(THREAD_FAILURE, REC_QID);
                    // }

                    if(vframes == 1)
                    {
                        if(kf == 1)
                        {
                            if(GetAvcCFromH264(&mp4MoovAtomData, hVideoOutBuf) < 0) 
                            {
                                ERR("Failed to get AvcC from H264\r\n");  
                                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                                is_rec_failed       = 1;
                                is_rec_finishing    = 1;
                                cleanup(THREAD_FAILURE, REC_QID);
                            }
                            else
                            {
                                k_timestamp = v_timestamp;
                            }
                        }
                        else
                        {
                            BufferReadComplete(vcAVRec_trd, eVENCODER_SRC, hVideoOutBuf);
                            if(skipped_frames > 35)
                            {
                                ERR("Failed to get AvcC from H264\r\n");  
                                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                                is_rec_failed       = 1;
                                is_rec_finishing    = 1;
                                cleanup(THREAD_FAILURE, REC_QID);
                            }
                            else
                            {
                                vframes--;
                                skipped_frames++;
                                continue; // while() on 100 line
                            }
                        }
                    }

                    log_threads("enter rec v_buf\r\n");

                    if(AddVideoFrame(&mp4MoovAtomData, hVideoOutBuf, mp4OutFile) < 0)
                    {
                        ERR("Failed to add video frame #%i\r\n", vframes);
                        is_rec_failed       = 1;
                        is_rec_finishing    = 1;
                        cleanup(THREAD_FAILURE, REC_QID);
                    }

                    rec_done            = 1;
                    is_file_not_empty   = 1;
                    internal_error      = 0;
                    log_threads("exit rec v_buf\r\n");
                } 

                BufferReadComplete(vcAVRec_trd, eVENCODER_SRC, hVideoOutBuf);

                // периодически очищаем кэш от записанных кадров
                if((vframes % 25 == 0) && (vframes > vframes_old))
                {
                    vframes_old = vframes;
                    fp = fileno(mp4OutFile);
                    // debug("vid_pack: %i\r\n", vframes);
                    posix_fadvise(fp, 0, 0, POSIX_FADV_DONTNEED);
                }
            }
        }

        if((aframes < AUDIO_CHUNKS_PER_HOUR) && ((k_timestamp > 0) || (sound_only == 1)))
        {
#ifdef SOUND_EN
            GetBufferToRead(vcAVRec_trd, eAENCODER_SRC, &hAudioOutBuf, NULL, &a_timestamp, NULL, NULL);
            if(hAudioOutBuf != NULL)
            {
                if(a_timestamp >= k_timestamp)
                {
                    aframes++;

                    /* Write encoded buffer to the audio file */
                    ret = Buffer_getNumBytesUsed(hAudioOutBuf);
                    if (ret <= 0) 
                    {
                        WARN("Packer received 0 byte encoded audio\r\n");
                    }
                    else
                    {
                        log_threads("enter rec a_buf\r\n");
                        if(sound_only == 1)
                        {
                            if (fwrite(Buffer_getUserPtr(hAudioOutBuf),
                                       Buffer_getNumBytesUsed(hAudioOutBuf), 1, audioOutFile) != 1) 
                            {
                                ERR("Error writing the encoded data to audio file.\r\n");
                                is_rec_failed       = 1;
                                is_rec_finishing    = 1;
                                cleanup(THREAD_FAILURE, REC_QID);
                            }
                            fp = fileno(audioOutFile);
                        }
                        else
                        {
                            if(AddAudioChunk(&mp4MoovAtomData, hAudioOutBuf, mp4OutFile) < 0)
                            {
                                ERR("Failed to add audio frame #%i\r\n", aframes);
                                is_rec_failed       = 1;
                                is_rec_finishing    = 1;
                                cleanup(THREAD_FAILURE, REC_QID);
                            }

                            is_file_not_empty   = 1;
                        }
                        rec_done = 1;

                        if(sound_only == 1)
                        {
                            internal_error = 0;
                        }
                        
                        log_threads("exit rec a_buf\r\n");
                    }
                }

                BufferReadComplete(vcAVRec_trd, eAENCODER_SRC, hAudioOutBuf);
            
                // периодически очищаем кэш от записанных порций аудио
                if((aframes % 16 == 0) && (sound_only == 1) && (aframes > aframes_old))
                {
                    aframes_old = aframes;
                    if(fp)
                    {
                        posix_fadvise(fp, 0, 0, POSIX_FADV_DONTNEED);
                    }
                    debug("aud_pack: %i\r\n", aframes);
                }
            }

#endif //SOUND_EN
        }

        // every 5 secs save info for video/audio recovery
        if(sound_only != 1)
        {
            secondTime = time(NULL);
            if((secondTime - firstTime) >= 5)
            {
                SaveRecoveryData(&mp4MoovAtomData);
                firstTime = secondTime;
            }
        }

        
        // ================= cut the file to pieces of an hour duration


#ifdef SOUND_EN
        if(aframes == AUDIO_CHUNKS_PER_HOUR)
        {
#endif // SOUND_EN
            if((vframes == frames_per_hour) || (sound_only == 1))
            {
                new_file = 1;

                if(sound_only != 1)
                {
                    if(mp4OutFile != NULL)
                    {
                        if(is_file_not_empty == 1)
                        {
                            ret = SaveAndCloseMP4File(mp4OutFile, &mp4MoovAtomData); 
                            if(ret == 0)
                            {
                                fails_counter   = 0;
                                mp4OutFile      = NULL;
                            }
                            else
                            {
                                is_file_not_empty   = 1;
                                mp4OutFile          = NULL;
                                ERR("Failed to close the MP4 file successfully\r\n");
                                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                                fails_counter++;
                                if(fails_counter >=3)
                                {
                                    is_rec_failed       = 1;
                                    is_rec_finishing    = 1;
                                    cleanup(THREAD_FAILURE, REC_QID);
                                }
                            }
                        }
                        else
                        {
                            CloseMP4File(mp4OutFile, &mp4MoovAtomData); 
                        }
                        is_file_not_empty   = 0;
                    }
                    debug("old file saved\r\n");
                }

                vframes = 0;
                aframes = 0;
            }
            else
            {
                GetBufferToRead(vcAVRec_trd, eAENCODER_SRC, &hAudioOutBuf, NULL, &a_timestamp, NULL, NULL);
                if(hAudioOutBuf != NULL)
                {

                    BufferReadComplete(vcAVRec_trd, eAENCODER_SRC, hAudioOutBuf);
                    WARN("Audio frame skipped\r\n");
                }
            }
#ifdef SOUND_EN
        }
        else if(vframes == frames_per_hour)
        {
            GetBufferToRead(vcAVRec_trd, eVENCODER_SRC, &hVideoOutBuf, NULL, &v_timestamp, NULL, &kf);
            if(hVideoOutBuf != NULL)
            {
                BufferReadComplete(vcAVRec_trd, eVENCODER_SRC, hVideoOutBuf);
                WARN("Video frame skipped\r\n");
            }

        }
#endif // SOUND_EN

        if(rec_done != 0)
        {
            rec_done = 0;
        }
        else
        {
            log_threads("STALL packer 2\r\n");
            usleep(5000);
        }        

        gettimeofday(&end_thread_time, NULL); 
        sprintf((char *)fifoLogString, "Iter packer time(usec): %llu\r\n",
            ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));
        log_threads((char *)fifoLogString);
    }

cleanup:

    debug("Packer thread finishing...\r\n");

    is_rec_finishing    = 1;
    
    if((mp4OutFile != NULL) && (sound_only != 1))
    {
        if(is_file_not_empty == 1)
        {
            ret = SaveAndCloseMP4File(mp4OutFile, &mp4MoovAtomData);
            if(ret == 0)
            {
                mp4OutFile      = NULL;
            }
            else
            {
                mp4OutFile      = NULL;
                ERR("Failed to close the MP4 file successfully\r\n");
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            }
        }
        else
        {
            CloseMP4File(mp4OutFile, &mp4MoovAtomData); 
        }
        is_file_not_empty   = 0;
    }

    ReleaseTransportBufGroup(vcAVRec_trd, eVENCODER_SRC);
#ifdef SOUND_EN
    ReleaseTransportBufGroup(vcAVRec_trd, eAENCODER_SRC);
#endif

    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
    if(envp->hRendezvousAllVRFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllVRFinished);
    }

    // if (videoOutFile) 
    // {
    //     fclose(videoOutFile);
    //     videoOutFile = NULL;
    // }

    if ((sound_only == 1) && (audioOutFile))
    {
        fclose(audioOutFile);
        audioOutFile = NULL;
    }

    system("/bin/echo 1 > /proc/sys/vm/drop_caches");
    sync();
    debug("Packer thread finished\r\n");
    return status;
}
