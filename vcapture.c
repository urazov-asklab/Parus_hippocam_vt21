/*
 * capture.c
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

#include <xdc/std.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <linux/watchdog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/Capture.h>
#include <ti/sdo/dmai/Resize.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "logging.h"
#include "gpio_func.h"
#include "transport_buffers.h"

#include "vcapture.h"

// 4-ре функции для регулировки настроек аналоговой камеры

int tvp5150_set_brightness(Capture_Handle hCapture, int val)
{
    int                 ret;
    struct v4l2_control control;

    control.id      = V4L2_CID_BRIGHTNESS;
    control.value   = val;

    ret = ioctl(hCapture->capture.chip_fd, VIDIOC_SUBDEV_S_CTRL, &control);
    if(ret < 0) {
        ERR("(%s:%i)failed to set control on tvp5150\n", __FILE__, __LINE__);
    }
    return ret;
}

int tvp5150_set_contrast(Capture_Handle hCapture, int val)
{
    int                 ret;
    struct v4l2_control control;

    control.id      = V4L2_CID_CONTRAST;
    control.value   = val;

    ret = ioctl(hCapture->capture.chip_fd, VIDIOC_SUBDEV_S_CTRL, &control);
    if(ret < 0) {
        ERR("(%s:%i)failed to set control on tvp5150\n", __FILE__, __LINE__);
    }
    return ret;
}

int tvp5150_set_saturation(Capture_Handle hCapture, int val)
{
    int                 ret;
    struct v4l2_control control;

    control.id      = V4L2_CID_SATURATION;
    control.value   = val;

    ret = ioctl(hCapture->capture.chip_fd, VIDIOC_SUBDEV_S_CTRL, &control);
    if(ret < 0) {
        ERR("(%s:%i)failed to set control on tvp5150\n", __FILE__, __LINE__);
    }
    return ret;
}

int tvp5150_set_hue(Capture_Handle hCapture, int val)
{
    int                 ret;
    struct v4l2_control control;

    control.id      = V4L2_CID_HUE;
    control.value   = val;

    ret = ioctl(hCapture->capture.chip_fd, VIDIOC_SUBDEV_S_CTRL, &control);
    if(ret < 0) {
        ERR("(%s:%i)failed to set control on tvp5150\n", __FILE__, __LINE__);
    }
    return ret;
}


void *vCaptureThrFxn(void *arg)
{
    // debug("VCapture thread started\r\n");

    void                   *status          = THREAD_SUCCESS;
    VCaptureEnv            *envp            = (VCaptureEnv *) arg;
    Capture_Attrs           cAttrs          = Capture_Attrs_OMAP3530_DEFAULT;
    BufferGfx_Attrs         gfxAttrs        = BufferGfx_Attrs_DEFAULT;
    Capture_Handle          hCapture        = NULL;
    BufTab_Handle           hBufTab         = NULL;
    BufTab_Handle           hBufTab2        = NULL;
    Buffer_Handle           hDstBuf         = NULL;
    Buffer_Handle           hCapBuf         = NULL;
    // BufferGfx_Dimensions    srcDim;
    int                     bufSize;
    int                     frames          = 0;
    int                     iters;
    int                     bufIdx;
    int                     ret;
    int                     n;
    int                     i               = 0;
    struct timeval          first_vframe_time;
    struct timeval          vframe_time;
    u64                     diff_t;
    // int                     frames_skipped;
    struct v4l2_pix_format  c_fmt;
    struct v4l2_pix_format  o_fmt;
    u64                     frames_done     = 0;
    u64                     timestamp       = 0;
    u64                     a               = 0;
    int                     cap_buf_count   = 0;
    int                     nowait          = 0;

   
    char *captDevNames[]        = {"tvp5150 2-005c", "adv761x 2-004c", "ds90ub91xa_ar0134 2-0060"};
    cAttrs.captureDeviceName    = captDevNames[video_source_num];

    // переключение каналов аналоговой камеры
    if(video_source_num == 0)
    {
        if(cam_channel_num == 0)
        {
            system("/bin/echo 0 > /sys/devices/platform/omap/omap_i2c.2/i2c-2/2-005c/analog_channel_number");
        }
        else if (cam_channel_num == 1)
        {
            system("/bin/echo 1 > /sys/devices/platform/omap/omap_i2c.2/i2c-2/2-005c/analog_channel_number");
        }
    }

    /* Create capture device driver instance */
    cAttrs.numBufs = NUM_CAP_VIDEO_BUFS;
    hCapture = Capture_create(&cAttrs);
    if (hCapture == NULL)
    {
        ERR("Failed to create video capture device\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    debug("Detecting video signal..\r\n");
    while (1)
    {
        i++;
        ret = Capture_detectVideoStd(hCapture, &c_fmt);
        if((ret < 0)||(ret == 2))
        {
            ERR("Detect video std failed!\r\n");
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }
        if(ret == 0)
        {
            if(c_fmt.width == 1280)
            {
                if((c_fmt.height != 720) && (c_fmt.height != 730))
                {
                    ERR("video std not supported!\n");
                    is_rec_failed       = 1;
                    // is_rftx_failed      = 1;
                    is_stream_failed    = 1;
                    is_cap_finishing    = 1;
                    cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
                }
            }
            else if((c_fmt.width != 720) || (!((c_fmt.height == 576) || (c_fmt.height == 480))))
            {
                ERR("video std not supported!\r\n");
                is_rec_failed       = 1;
                // is_rftx_failed      = 1;
                is_stream_failed    = 1;
                is_cap_finishing    = 1;
                cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
            }

            if(c_fmt.height == 576)
            {
                debug("frame_height (v_cap): 576\r\n");
                frame_height        = 576;
                frames_per_hour     = 90000;
                framerate           = 25000 >> half_vrate;
                frames_per_sec      = (framerate / 1000);
                envp->videoStd      = VideoStd_D1_PAL;
            }
            else if(c_fmt.height == 480)
            {
                debug("frame_height (v_cap): 480\r\n");
                frame_height        = 480;
                frames_per_hour     = 108000;
                framerate           = 30000 >> half_vrate;
                frames_per_sec      = (framerate / 1000);
                envp->videoStd      = VideoStd_D1_NTSC;
            }
            else if((c_fmt.height == 720) || (c_fmt.height == 730))
            {
                debug("frame_height (v_cap): %u\r\n", c_fmt.height);
                frame_height        = 720;
                frames_per_hour     = 90000;
                framerate           = 25000 >> half_vrate;
                frames_per_sec      = (framerate / 1000);
                envp->videoStd      = VideoStd_720PR_30;
            }
            o_fmt = c_fmt;
            is_cam_failed = 0;
            break;
        }
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
        usleep(20000);
        if((gblGetQuit(REC_QID | STRM_QID | RFTX_QID)) || i > 50)
        {
            is_cam_failed       = 1;
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_SUCCESS, REC_QID | STRM_QID | RFTX_QID);
        } 
    }

    // если требуется согласовать характеристики кадра с нитью сжатия видео
    if(envp->hRendezvousFrameFormat != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFrameFormat);
    }

    /* Calculate the dimensions of a video standard given a color space */
    if (BufferGfx_calcDimensions(envp->videoStd, ColorSpace_UYVY, &gfxAttrs.dim) < 0) 
    {
        ERR("Failed to calculate Buffer dimensions\n");
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    /* Calculate necessary buffer size of a video standard given a color space */
    bufSize = BufferGfx_calcSize(envp->videoStd, ColorSpace_UYVY);
    bufSize = (bufSize + 4096) & (~0xFFF);
    if (bufSize < 0) 
    {
        ERR("Failed to calculate size for video capture driver buffers\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    /* Create a table of buffers to use with the device drivers */
    gfxAttrs.colorSpace     = ColorSpace_UYVY;
    gfxAttrs.dim.width      = enc_width;
    gfxAttrs.dim.height     = enc_height;
    hBufTab = BufTab_create(NUM_CAP_VIDEO_BUFS, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
    if (hBufTab == NULL) 
    {
        ERR("Failed to allocate contiguous buffers for video capture\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    RegisterTransportBufGroup(eVCAPTURE_SRC, hBufTab);

    hBufTab2 = BufTab_create(TBUF_NUMBER - NUM_CAP_VIDEO_BUFS, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
    if (hBufTab2 == NULL) 
    {
        ERR("Failed to allocate second part of contiguous buffers for video capture\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    n = BufTab_getNumBufs(hBufTab);

    for (bufIdx = 0; bufIdx < n; bufIdx++) 
    {
        GetBufferToWrite(eVCAPTURE_SRC, &hDstBuf, 0);
        cap_buf_count++;
    }
    
    AddBuffers(eVCAPTURE_SRC, hBufTab2);

    /* Signal that initialization is done and wait for other threads */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    cAttrs.cropWidth  = (video_source_num == 2)? 1280 : actual_frame_width;
#if (FRAME_CROP_WIDTH == 704)
    cAttrs.cropX      = (video_source_num == 2)? 0 : 8;
#else
    cAttrs.cropX      = 0;
#endif
    cAttrs.cropY      = 0;
    cAttrs.cropHeight = (video_source_num == 2)? 730 : frame_height;

    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

    ret = Capture_finish_create(hCapture, hBufTab, &cAttrs, &c_fmt);
    if(ret < 0)
    {
        ERR("Failed to finish creating capture device\n");
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    /* Only process the part of the image which is going to be encoded*/
    // for (bufIdx = 0; bufIdx < NUM_CAP_VIDEO_BUFS; bufIdx++) 
    // {
    //     hCapBuf = BufTab_getBuf(hBufTab, bufIdx);
    //     BufferGfx_getDimensions(hCapBuf, &srcDim);
    //     BufferGfx_setDimensions(hCapBuf, &srcDim);
    // }

    iters    = 0;
#ifdef SOUND_EN
    // синхронизация с нитью звука
    if(envp->hRendezvousCapture != NULL)
    {
        Rendezvous_meet(envp->hRendezvousCapture);
    }
#endif

    if(video_source_num == 0)
    {
        tvp5150_set_brightness(hCapture, cam_brightness + 78);
        tvp5150_set_contrast(hCapture, cam_contrast + 78);
        tvp5150_set_saturation(hCapture, (int)(cam_saturation*25) + 78);
    }

    while (!gblGetQuit(REC_QID | STRM_QID | RFTX_QID)) 
    {        
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
        log_threads("waiting cap_buf\r\n");

        /* Get a buffer from the capture driver to encode */
        if (Capture_get(hCapture, &hCapBuf) < 0) 
        {
            ERR("Failed to get video capture buffer\r\n");
            logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }
        cap_buf_count--;

        log_threads("got cap_buf\r\n");

        if(iters == 0) // первый раз сохраняем текущее время во временную метку
        {
            gettimeofday(&first_vframe_time, NULL); 
            a = (u64)first_vframe_time.tv_sec * (u64)MS_IN_SEC + (u64)first_vframe_time.tv_usec / (u64)MS_IN_SEC;
            timestamp = a;
        }
        else            // в следующие разы прибавляем по 40 ms
        {
            timestamp += 40;
        }

        gettimeofday(&vframe_time, NULL); 
        diff_t = ((u64)vframe_time.tv_sec * (u64)US_IN_SEC + (u64)vframe_time.tv_usec) - a * (u64)1000;

        frames_done = diff_t / 40000;

        if((frames%2000 == 0)/*&&((frames_done - frames) > 2)*/)
        {
            debug("v frames %llu(p) = %i(f)\r\n", frames_done, frames);
        }

        is_video_captured = 1;

        iters++;
        if(((hdmi_active == 1) || (half_vrate == 1)) && (iters%2 == 0))
        {
            if (Capture_put(hCapture, hCapBuf) < 0) 
            {
                ERR("Failed to put video capture buffer\r\n");
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                is_rec_failed       = 1;
                // is_rftx_failed      = 1;
                is_stream_failed    = 1;
                is_cap_finishing    = 1;
                cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
            }
        }
        else
        {
            Buffer_setNumBytesUsed(hCapBuf, frame_height * actual_frame_width * 2);
            SetBufferReady(eVCAPTURE_SRC, hCapBuf, timestamp, noKey);

            log_threads("waiting to return cap_buf\r\n");

            nowait = (cap_buf_count < 3) ? 1 : 0;

            GetBufferToWrite(eVCAPTURE_SRC, &hDstBuf, nowait);

            if(hDstBuf != NULL)
            {
                /* Return a buffer to the capture driver */
                if (Capture_put(hCapture, hDstBuf) < 0) 
                {
                    ERR("Failed to put video capture buffer\r\n");
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    is_rec_failed       = 1;
                    // is_rftx_failed      = 1;
                    is_stream_failed    = 1;
                    is_cap_finishing    = 1;
                    cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
                }
                else
                {
                    cap_buf_count++;
                }
            }
            log_threads("returned cap_buf\r\n");

            frames++;
        }

        ret = Capture_detectVideoStd(hCapture, &c_fmt);

        if(ret > 0)
        {
            ERR("Video signal status changed!\n");
            is_cam_failed       = 1;
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }
        if(ret < 0)
        {
            ERR("Detect video std failed!\n");
            is_cam_failed       = 1;
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }
        if(!ret)
        {
            if((o_fmt.width != c_fmt.width)||(o_fmt.height != c_fmt.height))
            {
                ERR("video std changed!\n");
                is_cam_failed       = 1;
                is_rec_failed       = 1;
                // is_rftx_failed      = 1;
                is_stream_failed    = 1;
                is_cap_finishing    = 1;
                cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
            }
        }

        // }
        // else
        // {
        //     debug("CROPPING DONE\r\n");
        //     /* Return a buffer to the capture driver */
        //     if (Capture_put(hCapture, hCapBuf) < 0) 
        //     {
        //         ERR("Failed to put video capture buffer\r\n");
        //         logEvent(log_REC_APL_REC_RUNTIME_ERROR);
        //         is_rec_failed       = 1;
        //         is_rftx_failed      = 1;
        //         is_stream_failed    = 1;
        //         is_cap_finishing    = 1;
        //         cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        //     }

        //     frames_skipped++;
        // }

        // if(frames % 20 == 0)
        // {
        //     debug("vid_cap: %i\r\n", frames);
        // }


        if(frames == frames_per_hour)
        {
            frames = 0;
        }
    }

cleanup:
    debug("VCapture thread finishing...\r\n");

    is_video_captured   = 0;
    is_cap_finishing    = 1;

    /* Make sure the other threads aren't waiting for us */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }

    if(envp->hRendezvousFrameFormat != NULL)
    {
        Rendezvous_force(envp->hRendezvousFrameFormat);
    }
    if(envp->hRendezvousCapture != NULL)
    {
        Rendezvous_force(envp->hRendezvousCapture);
    }

    if(envp->hRendezvousAllCapFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllCapFinished);
    }

    if(hBufTab != NULL)
    {
        FreeTransportBufGroup(eVCAPTURE_SRC);
    }

    if(hCapture)
    {
        Capture_delete(hCapture);
    }

    if(hBufTab != NULL)
    {
        ret = BufTab_delete(hBufTab);
        if(ret < 0)
        {
            ERR("Failed to delete video capture buffers1 %i\r\n", ret);
        }
    }
    if(hBufTab2)
    {
        ret = BufTab_delete(hBufTab2);
        if(ret < 0)
        {
            ERR("Failed to delete video capture buffers2 %i\r\n", ret);
        }
    }  
    debug("Video capture thread finished\r\n");
    return status;
}

