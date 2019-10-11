/*
 * resize.c
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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/watchdog.h>
#include <linux/videodev2.h>

#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "logging.h"
#include "transport_buffers.h"
#include "common.h"

#include "resize.h"

#define FIRST_FRAME_TO_RSZ 0

void print_buf_content(char *buf, int len)
{
    int i;
    for(i = 0; i < len; i++)
    {   
        printf("0x%.2x ", buf[i]);
    }
    printf("\n");
}

static int stream_on(int fd, int type) 
{
    int err = ioctl(fd, VIDIOC_STREAMON, &type);
    if (err < 0) 
    {        
        ERR("Resizer streaming on error %i\r\n", errno);
        return -1;
    }
    return 0;
}

static int stream_off(int fd, int type) 
{
    int err = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (err < 0) 
    {
        ERR("Resizer streaming on error %i\r\n", errno);
        ERR("Stream off failed(%i)\r\n", err);
        return -1;
    }
    
    return 0;
}

void *resizeThrFxn(void *arg)
{
    //debug("Resize thread started\r\n");

    u8                          trdFlags          = 0;
    const char                 *device_in         = "/dev/video5";
    const char                 *device_out        = "/dev/video6";
    void                       *status            = THREAD_SUCCESS;
    BufferGfx_Attrs             gfxAttrs          = BufferGfx_Attrs_DEFAULT;
    ResizeEnv                  *envp              = (ResizeEnv *) arg;
    BufTab_Handle               hBufTab           = NULL;
    Buffer_Handle               hCaptureBuf       = NULL;
    Buffer_Handle               hDstBuf;
    Command                     currentCommand;
    int                         iters;
    int                         fd_in;
    int                         fd_out            = 0;
    int                         err;
    int                         bufSize;
    u8                         *hCaptureBufPtr;
    u8                         *hDstBufPtr;
    u64                         diff_t;
    struct timespec             cond_time;
    struct timeval              first_frame_time;
    struct timeval              frame_time;
    struct timeval              start_thread_time;
    struct timeval              end_thread_time;
    struct v4l2_buffer          buf;
    struct v4l2_capability      cap;
    struct v4l2_format          fmt;
    struct v4l2_requestbuffers  rb;
    struct v4l2_buffer          buf_in;
    struct v4l2_buffer          buf_out;
    struct tm                   timeinfo;
    time_t                      seconds;
    char                        set_sys_cmd[512];  
    int                         bpp                = 16;

    system("/usr/bin/media-ctl -l '\"OMAP3 ISP resizer input\":0->\"OMAP3 ISP resizer\":0[1], \
        \"OMAP3 ISP resizer\":1->\"OMAP3 ISP resizer output\":0[1]'");

    sprintf(set_sys_cmd, "/usr/bin/media-ctl -f '\"OMAP3 ISP resizer\":0 [UYVY %ix%i]'", actual_frame_width, frame_height);
    system(set_sys_cmd);

    sprintf(set_sys_cmd, "/usr/bin/media-ctl -f '\"OMAP3 ISP resizer\":1 [UYVY %ix%i]'", rsz_width, rsz_height);
    system(set_sys_cmd);

    seconds  = time(NULL);
    timeinfo = *localtime(&seconds);

    /* resizer input setup */
    fd_in = open(device_in, O_RDWR);
    if (fd_in < 0) 
    {
        ERR("Failed to open %s\r\n", device_in);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    err = ioctl(fd_in, VIDIOC_QUERYCAP, &cap);
    if (err < 0) 
    {
        ERR("Failed to query device capabilities\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT && cap.capabilities & V4L2_CAP_STREAMING)) 
    {
        ERR("Missing v4l2 capabilities (%08x)\r\n", cap.capabilities);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    /* resizer output setup */
    fd_out = open(device_out, O_RDWR);
    if (fd_out < 0) 
    {
        ERR("Failed to open %s\r\n", device_out);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    err = ioctl(fd_out, VIDIOC_QUERYCAP, &cap);
    if (err < 0) 
    {
        ERR("Failed to query camera capabilities\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE && cap.capabilities & V4L2_CAP_STREAMING)) 
    {
        ERR("Missing v4l2 capabilities (%08x)\r\n", cap.capabilities);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type                    = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width           = actual_frame_width;
    fmt.fmt.pix.height          = frame_height;
    fmt.fmt.pix.pixelformat     = V4L2_PIX_FMT_UYVY;
    fmt.fmt.pix.bytesperline    = 0;
    fmt.fmt.pix.field           = V4L2_FIELD_ANY;

    err = ioctl(fd_in, VIDIOC_S_FMT, &fmt);
    if (err < 0) 
    {
        ERR("Failed to set format\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type                    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width           = rsz_width;
    fmt.fmt.pix.height          = rsz_height;
    fmt.fmt.pix.pixelformat     = V4L2_PIX_FMT_UYVY;
    fmt.fmt.pix.bytesperline    = 0;
    fmt.fmt.pix.field           = V4L2_FIELD_ANY;

    err = ioctl(fd_out, VIDIOC_S_FMT, &fmt);
    if (err < 0) 
    {
        ERR("Failed to set format\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    gfxAttrs.colorSpace     = ColorSpace_UYVY;
    gfxAttrs.dim.x          = 0;
    gfxAttrs.dim.y          = 0;
    gfxAttrs.dim.width      = rsz_width;
    gfxAttrs.dim.height     = rsz_height;
    gfxAttrs.dim.lineLength = rsz_width * bpp / 8;

    bufSize = gfxAttrs.dim.lineLength * gfxAttrs.dim.height;
    //bufSize = rsz_width * rsz_height * 2;
    bufSize = (bufSize + 4096) & (~0xFFF);

    if (bufSize < 0) 
    {
        ERR("Failed to calculate size for resized video buffers\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    hBufTab = BufTab_create(TBUF_NUMBER, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
    if (hBufTab == NULL) 
    {
        ERR("Failed to allocate contiguous buffers\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    RegisterTransportBufGroup(eVRESIZER_SRC, hBufTab);

    /* Signal that initialization is done and wait for other threads */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    PlugToTransportBufGroup(vcResize_trd, trdFlags, eVCAPTURE_SRC);

    /* request input buffers */
    memset(&rb, 0, sizeof(struct v4l2_requestbuffers));
    rb.count    = 1;
    rb.type     = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb.memory   = V4L2_MEMORY_USERPTR;

    err = ioctl(fd_in, VIDIOC_REQBUFS, &rb);
    if (err < 0) 
    {
        ERR("Failed to request buffers\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    /* request output buffers */
    memset(&rb, 0, sizeof(struct v4l2_requestbuffers));
    rb.count    = 1;
    rb.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory   = V4L2_MEMORY_USERPTR;

    err = ioctl(fd_out, VIDIOC_REQBUFS, &rb);
    if (err < 0) 
    {
        ERR("Failed to request buffers\r\n");
        is_rftx_failed      = 1;
        is_rftx_finishing   = 1;
        cleanup(THREAD_FAILURE, RFTX_QID);
    }

    iters   		= 0;
    is_rsz_started 	= 1;

    while(1) 
    {
        gettimeofday(&start_thread_time, NULL); 
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt

        while(1)
        {
            if(gblGetQuit(RFTX_QID))
            {
                is_rftx_finishing   = 1;
                goto cleanup;
            }
            currentCommand = gblGetCmd();
            if(currentCommand == STOP_RFTX)
            {
                is_rftx_finishing   = 1;
                cleanup(THREAD_SUCCESS, RFTX_QID);
            }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

            GetBufferToRead(vcResize_trd, eVCAPTURE_SRC, &hCaptureBuf, NULL, NULL, NULL, NULL);
            if(hCaptureBuf != NULL)
            {
                break;
            }
            else
            {
                makewaittime(&cond_time, 0, 500000000); // 500 ms
                pthread_mutex_lock(&rcond_mutex[eVCAPTURE_SRC]);
                err = pthread_cond_timedwait(&rbuf_cond[eVCAPTURE_SRC], &rcond_mutex[eVCAPTURE_SRC], &cond_time);
                if(err != 0)
                {
                    if(err == ETIMEDOUT)
                    {
                        ERR("Resizer failed to get a buffer to read\r\n");
                    }
                    else if(err != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                    }
                    pthread_mutex_unlock(&rcond_mutex[eVCAPTURE_SRC]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    is_rftx_failed      = 1;
                    is_rftx_finishing   = 1;
                    cleanup(THREAD_FAILURE, RFTX_QID);
                }
                pthread_mutex_unlock(&rcond_mutex[eVCAPTURE_SRC]);
            }
        }

        gettimeofday(&end_thread_time, NULL); 
        sprintf((char *)fifoLogString,"1 wait buf: %ld\r\n",
            (end_thread_time.tv_sec * US_IN_SEC + end_thread_time.tv_usec) 
                - (start_thread_time.tv_sec * US_IN_SEC + start_thread_time.tv_usec));
        
        /* Make sure the whole buffer is used for input */
        BufferGfx_resetDimensions(hCaptureBuf);
        hCaptureBufPtr  = (u8 *)Buffer_getUserPtr(hCaptureBuf);

        // if(iters >= FIRST_FRAME_TO_RSZ)
        if(iters%5 == 0) // на данный момент радиотрансляция 5 кадров в секунду
        {
            while(1)
            {
            	if(gblGetQuit(RFTX_QID))
	        	{
                    is_rftx_finishing   = 1;
	        		goto cleanup;
	        	}
	        	currentCommand = gblGetCmd();
		        if(currentCommand == STOP_RFTX)
		        {
                    is_rftx_finishing   = 1;
		            cleanup(THREAD_SUCCESS, RFTX_QID);
		        }

                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

                GetBufferToWrite(eVRESIZER_SRC, &hDstBuf, 0);
                if(hDstBuf != NULL)
                {
                    break;
                }
                else
                {
                    makewaittime(&cond_time, 0, 500000000); // 500 ms
                    pthread_mutex_lock(&wcond_mutex[eVRESIZER_SRC]);
                    err = pthread_cond_timedwait(&wbuf_cond[eVRESIZER_SRC], &wcond_mutex[eVRESIZER_SRC], &cond_time);
                    if(err != 0)
                    {
                        if(err == ETIMEDOUT)
                        {
                            ERR("Resizer failed to get a buffer to write\r\n");
                        }
                        else if(err != ETIMEDOUT)
                        {
                            ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                        }
                        pthread_mutex_unlock(&wcond_mutex[eVRESIZER_SRC]);
                        logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                        is_rftx_failed      = 1;
                        is_rftx_finishing   = 1;
                        cleanup(THREAD_FAILURE, RFTX_QID);
                    }
                    pthread_mutex_unlock(&wcond_mutex[eVRESIZER_SRC]);
                }
            }
            hDstBufPtr      = (u8 *)Buffer_getUserPtr(hDstBuf);

            log_threads("enter v_rsz\r\n");

            gettimeofday(&end_thread_time, NULL); 
            sprintf((char *)fifoLogString,"2 get ptr: %llu\r\n",
                ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                    - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));

            /////////////
            // RESIZE
            /////////////

             /* map the output buffers */
            memset(&buf_out, 0, sizeof(struct v4l2_buffer));
            buf_out.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf_out.memory      = V4L2_MEMORY_USERPTR;
            buf_out.m.userptr   = (u32)hDstBufPtr;
            buf_out.length      = rsz_height * rsz_width * 2;

            // queue output buffer
            err = ioctl(fd_out, VIDIOC_QBUF, &buf_out);
            if (err < 0) 
            {
                ERR("Queuing buffer failed\r\n");
                is_rftx_failed      = 1;
                is_rftx_finishing   = 1;
                cleanup(THREAD_FAILURE, RFTX_QID);
            }

            gettimeofday(&end_thread_time, NULL); 
            sprintf((char *)fifoLogString,"3 q buf_out: %llu\r\n",
                ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                    - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));


            /* map the input buffers */
            memset(&buf_in, 0, sizeof(struct v4l2_buffer));
            buf_in.index        = 0;
            buf_in.type         = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            buf_in.memory       = V4L2_MEMORY_USERPTR;
            buf_in.m.userptr    = (u32)hCaptureBufPtr;
            buf_in.length       = actual_frame_width * frame_height * 2;

            err = ioctl(fd_in, VIDIOC_QBUF, &buf_in);
            if (err < 0) 
            {
                ERR("Queuing buffer failed\r\n");
                is_rftx_failed      = 1;
                is_rftx_finishing   = 1;
                cleanup(THREAD_FAILURE, RFTX_QID);
            }

            gettimeofday(&end_thread_time, NULL); 
            sprintf((char *)fifoLogString,"4 q buf_in: %llu\r\n",
                ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                    - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));

            if(iters == FIRST_FRAME_TO_RSZ)
            {
                if(stream_on(fd_in, V4L2_BUF_TYPE_VIDEO_OUTPUT) < 0)
                {
                    ERR("Input stream on failed\r\n");
                    is_rftx_failed      = 1;
                    is_rftx_finishing   = 1;
                    cleanup(THREAD_FAILURE, RFTX_QID);
                }
                if(stream_on(fd_out, V4L2_BUF_TYPE_VIDEO_CAPTURE) < 0)
                {
                    ERR("Output stream on failed\r\n");
                    is_rftx_failed      = 1;
                    is_rftx_finishing   = 1;
                    cleanup(THREAD_FAILURE, RFTX_QID);
                }
            }

             /* dequeue input buffer */
            memset(&buf, 0, sizeof(struct v4l2_buffer));
            buf.type    = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            buf.memory  = V4L2_MEMORY_USERPTR;

            err = ioctl(fd_in, VIDIOC_DQBUF, &buf);
            if (err < 0) 
            {
                ERR("Buffer dequeue failed\r\n");
                is_rftx_failed      = 1;
                is_rftx_finishing   = 1;
                cleanup(THREAD_FAILURE, RFTX_QID);
            }

            gettimeofday(&end_thread_time, NULL); 
            sprintf((char *)fifoLogString,"5: dq buf_in %llu\r\n",
                ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                    - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));

            /* dequeue output buffer */
            memset(&buf, 0, sizeof(struct v4l2_buffer));
            buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory  = V4L2_MEMORY_USERPTR;

            err = ioctl(fd_out, VIDIOC_DQBUF, &buf);
            if (err < 0) 
            {
                ERR("Buffer dequeue failed\r\n");
                is_rftx_failed      = 1;
                is_rftx_finishing   = 1;
                cleanup(THREAD_FAILURE, RFTX_QID);
            }

            /////////////
            // RESIZE
            /////////////

            if(iters == FIRST_FRAME_TO_RSZ)
            {
                gettimeofday(&first_frame_time, NULL); 
            }

            gettimeofday(&frame_time, NULL); 

            diff_t = ((u64)frame_time.tv_sec * (u64)US_IN_SEC + (u64)frame_time.tv_usec) 
                     - ((u64)first_frame_time.tv_sec * (u64)US_IN_SEC + (u64)first_frame_time.tv_usec);

            Buffer_setNumBytesUsed(hDstBuf, buf_out.length);
            SetBufferReady(eVRESIZER_SRC, hDstBuf, diff_t, noKey);
        }

        BufferReadComplete(vcResize_trd, eVCAPTURE_SRC, hCaptureBuf);
        
        iters++;
        if(iters % 10 == 0)
        {
            //debug("vid_rsz: %i\r\n", iters);
        }

        // if(iters == frames_per_hour)
        // {
        //     iters = 0;
        // }
        gettimeofday(&end_thread_time, NULL); 
        sprintf((char *)fifoLogString,"Iter v_resizer time(usec): %llu\r\n",
            ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));
        log_threads((char *)fifoLogString);
    }

cleanup:

    is_rsz_started      = 0;
    is_rftx_finishing   = 1;

    ReleaseTransportBufGroup(vcResize_trd, eVCAPTURE_SRC);

    /* Make sure the other threads aren't waiting for us */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
    if(envp->hRendezvousAllRTFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllRTFinished);
    }

    if(hBufTab != NULL)
    {
        FreeTransportBufGroup(eVRESIZER_SRC);
    }

    if(hBufTab != NULL)
    {
        err = BufTab_delete(hBufTab);
        if(err < 0)
        {
            ERR("Failed to delete resizer buffers %i\r\n", err);
        }
    }

    if(fd_in != -1)
    {
        stream_off(fd_in, V4L2_BUF_TYPE_VIDEO_OUTPUT);
        close(fd_in);
    }
    if(fd_out != -1)
    {
        stream_off(fd_out, V4L2_BUF_TYPE_VIDEO_CAPTURE);
        close(fd_out);
    }

    debug("Resizer thread finished\r\n");

    return status;
}