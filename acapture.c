/*
 * acapture.c
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "logging.h"
#include "transport_buffers.h"

#include "acapture.h"

#define PCM  				// !!! switch on for gained sound (K51, not for VT20)
#define PERIOD_DUR 	64


int enableAudio()
{
	/*
    * Audio capture channels AUXL and AUXR are by default disabled for
    * AM/DM37x,OMAP35x ASoC driver
    */

    csys("/usr/bin/amixer cset name='Analog Left AUXL Capture Switch' 1");
    csys("/usr/bin/amixer cset name='Analog Right AUXR Capture Switch' 1");

    return SUCCESS;
}


void *aCaptureThrFxn(void *arg)
{
    //debug("ACapture thread started\r\n");

	snd_mixer_elem_t 	   *elem;
	snd_mixer_selem_id_t   *sid 					= NULL;
	snd_mixer_t 		   *handle;
    void                   *status              	= THREAD_SUCCESS;
    BufTab_Handle           hBufTab             	= NULL;
    Buffer_Attrs            bAttrs              	= Buffer_Attrs_DEFAULT;
    ACaptureEnv            *envp                	= (ACaptureEnv *) arg;
    Buffer_Handle           hCapBuf;
    u16                    *hCapBufPtr;
    int                     ret;
    int                     chunks              	= 0;
    int                     iters;
    int                     inBufSize;
    int                     first_time_measure  	= 0;
    int            			analog_mic_gain1_prev 	= -1;
	int            			analog_mic_gain2_prev 	= -1;
    struct timespec         cond_time;
    struct timeval          first_aframe_time;
    u64      				timestamp 				= 0;

#ifndef PCM // вариант захвата звука через функции DMAI
    /* Create the sound device */
    
    Sound_Attrs  sAttrs = Sound_Attrs_MONO_DEFAULT;
    Sound_Handle hSound = NULL;

    sAttrs.sampleRate   = SAMPLING_FREQUENCY;
    sAttrs.channels     = audio_channels;		// количество каналов
    sAttrs.soundInput   = Sound_Input_MIC;
    sAttrs.mode         = Sound_Mode_INPUT;
    sAttrs.soundStd     = Sound_Std_ALSA;
    sAttrs.bufSize      = 256;

    hSound = Sound_create(&sAttrs);
    if (hSound == NULL) 
    {
        ERR("Failed to create audio device\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }
#else // вариант захвата звука через функции PCM
    snd_pcm_t 			   *capture_handle;
    snd_pcm_hw_params_t    *hw_params;
    snd_pcm_uframes_t 		frames;
    unsigned int            exact_rate;
    int                     temp_sample;
    int                     readSamples;
    int                     _32SampleBuf[SAMPLES_PER_CHUNK];   // use it only when analog_mic_enable == 0!!!
    int 					err;
    int                     numSamples          = 0;
    int 					dir 				= 0;
    unsigned int            dur                 = PERIOD_DUR;
    u8                     *_32SampleBufPtr     = NULL;
    // snd_pcm_uframes_t    frames_temp;   

	/* Open PCM. The last parameter of this function is the mode */
    err = snd_pcm_open (&capture_handle, "default" /*device*/, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) 
    {
		fprintf (stderr, "Cannot open audio device (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	/* Allocate the snd_pcm_hw_params_t structure on the stack */
    err = snd_pcm_hw_params_malloc (&hw_params);
	if (err < 0) 
    {
		ERR("Cannot allocate hardware parameter structure (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	/* Init hwparams with full configuration space */
    err = snd_pcm_hw_params_any (capture_handle, hw_params);
	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
	    ERR("Сannot initialize hardware parameter structure (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	/* Set sample rate. If the exact rate is not supported by the hardware, use nearest possible rate */
  	exact_rate  = SAMPLING_FREQUENCY;
    err         = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &exact_rate, &dir); 
    // функция возвращает частоту, которую смогла установить
  	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
	    ERR("Сannot set sample rate (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}

  	if (exact_rate != SAMPLING_FREQUENCY) 
    {
    	ERR("The rate %d Hz is not supported by your hardware.\r\n ==> Using %u Hz instead.\r\n", 
            SAMPLING_FREQUENCY, exact_rate);
  	}

  	/* Set sample format */
  	if(analog_mic_enable == 0)
  	{
  		// для цифрового микрофона
    	err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S32_LE);
    }
	else
	{
		// для аналогового микрофона
    	err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    }
  	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
	    ERR("Сannot set sample format (%s)\r\n", snd_strerror (err)); 
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}

	/* Set access type */
    err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
    	ERR("Сannot set access type (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}

  	/* Set number of channels */
    err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, audio_channels);
  	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
	    ERR("Сannot set channel count (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}

  	/* Set period size of frames */
    frames = SAMPLES_PER_CHUNK;
    snd_pcm_hw_params_set_period_size_near(capture_handle, hw_params, &frames, &dir);
    snd_pcm_hw_params_set_periods_near(capture_handle, hw_params, &dur, &dir);

  	/* Apply HW parameter settings to PCM device and prepare device */
    err = snd_pcm_hw_params(capture_handle, hw_params);
  	if (err < 0) 
    {
        snd_pcm_hw_params_free(hw_params);
	    ERR("Сannot set parameters (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}


    //====================

    // snd_pcm_hw_params_get_buffer_size(hw_params, &frames_temp);
    // debug("buffer size = %ld\r\n", frames_temp);

    // snd_pcm_hw_params_get_period_time(hw_params, &dur, &dir);
    // debug("period time = %d us\r\n", dur);

    // snd_pcm_hw_params_get_periods(hw_params, &dur, &dir);
    // debug("periods = %d\r\n", dur);

    //=====================


  	snd_pcm_hw_params_free(hw_params);

    err = snd_pcm_prepare (capture_handle);
  	if (err < 0) 
    {
	    ERR("Сannot prepare audio interface for use (%s)\r\n", snd_strerror (err));
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
  	}
#endif

  	// Закончили с настройкой аудиоустройства, подготавливаем буферы для хранения захваченного звука

    inBufSize   = ((SAMPLES_PER_CHUNK << 1) << ((audio_channels == 2) ? 1 : 0)); 

    hBufTab     = BufTab_create(TBUF_NUMBER, inBufSize, &bAttrs);
    if (hBufTab == NULL) 
    {
        ERR("Failed to allocate contiguous audio buffers for capture\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        is_rec_failed       = 1;
        // is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    //зарегистрировать все буферы как группу, которая будет хранить захваченный звук
    RegisterTransportBufGroup(eACAPTURE_SRC, hBufTab);

    /* Signal that initialization is done and wait for other threads */
    if(envp->hRendezvousInit != NULL)
    {
    	// ожидаем, что группа нитей, отвечающих за захват как звука, так и видео, - проинициализирована
        Rendezvous_meet(envp->hRendezvousInit);
    }

    iters = 0;

    if((sound_only == 0) && (envp->hRendezvousCapture != NULL))
    {

    	// для уменьшения расхождения во времени захвата звука и захвата видео
        Rendezvous_meet(envp->hRendezvousCapture);
    }

    while (!gblGetQuit(REC_QID | STRM_QID | RFTX_QID)) 
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt

       	do
       	{
       		// set volume for analog mic in dBs*2 if changed
	        if((analog_mic_enable == 1) 
	        	&& ((analog_mic_gain1 != analog_mic_gain1_prev) || (analog_mic_gain2 != analog_mic_gain2_prev)))
	        {
	        	ret = snd_mixer_open(&handle, 0);
	        	if(ret < 0)
	        	{
	        		WARN("Failed to open mixer on device (%s)\r\n", snd_strerror (ret));
	        		break;
	        	}

	        	ret = snd_mixer_attach(handle, "default");
	        	if(ret < 0)
	        	{
	        		WARN("Failed to attach mixer on device (%s)\r\n", snd_strerror (ret));
	        		break;
	        	}

	        	ret = snd_mixer_selem_register(handle, NULL, NULL);
	        	if(ret < 0)
	        	{
	        		WARN("Failed to register mixer on device (%s)\r\n", snd_strerror (ret));
	        		break;
	        	}

	        	ret = snd_mixer_load(handle);
	        	if(ret < 0)
	        	{
	        		WARN("Failed to load mixer on device (%s)\r\n", snd_strerror (ret));
	        		break;
	        	}

	        	snd_mixer_selem_id_alloca(&sid);
	        	if(sid == NULL)
	        	{
	        		WARN("Failed to allocate memory for mixer simple element (%s)\r\n", snd_strerror (ret));
	        		break;
	        	}

	        	snd_mixer_selem_id_set_index(sid, 0);
	        	snd_mixer_selem_id_set_name(sid, "PGA Level");
	        	elem = snd_mixer_find_selem(handle, sid);

                debug("SET VOLUME: left - %i; right - %i\r\n", analog_mic_gain1 << 1, analog_mic_gain2 << 1);

                err = snd_mixer_selem_set_capture_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, analog_mic_gain2 << 1);
                err = snd_mixer_selem_set_capture_volume(elem, SND_MIXER_SCHN_FRONT_RIGHT, analog_mic_gain1 << 1);

	        	snd_mixer_close(handle);
				
				analog_mic_gain1_prev = analog_mic_gain1;
				analog_mic_gain2_prev = analog_mic_gain2;

	        }
	    }while(0);

        while(1)
        {
            if(gblGetQuit(REC_QID | STRM_QID | RFTX_QID))
            {
            	// если есть флаги останова нитей, то идём на выход
                is_cap_finishing    = 1;
                goto cleanup;
            }
            ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

            GetBufferToWrite(eACAPTURE_SRC, &hCapBuf, 0);
            if(hCapBuf != NULL)
            {
            	// если получили буффер - идем дальше
                break;
            }
            else
            {
            	// если не получили буффер, то ожидаем, когда он освободится и придет сигнал
                makewaittime(&cond_time, 0, 500000000); // 500 ms ждём, дальше выходим с ошибкой
                pthread_mutex_lock(&wcond_mutex[eACAPTURE_SRC]);
                ret = pthread_cond_timedwait(&wbuf_cond[eACAPTURE_SRC], &wcond_mutex[eACAPTURE_SRC], &cond_time);
                if(ret != 0)
                {
                    if(ret == ETIMEDOUT) 
                    {
                        ERR("AudioCapture failed to get a buffer to write\r\n");
                    }
                    else if(ret != ETIMEDOUT)
                    {
                        ERR("Exit pthread_cond_timedwait with code %i\r\n", ret);
                    }
                    pthread_mutex_unlock(&wcond_mutex[eACAPTURE_SRC]);
                    logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                    is_rec_failed       = 1;
                    // is_rftx_failed      = 1;
                    is_stream_failed    = 1;
                    is_cap_finishing    = 1;
                    cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
                }
                pthread_mutex_unlock(&wcond_mutex[eACAPTURE_SRC]);
            }
        }

#ifdef PCM
        // получаем указатель на буффер в памяти, т.к. будем с ним работать, как с массивом байтов
        hCapBufPtr      = (u16 *)Buffer_getUserPtr(hCapBuf); 
        readSamples     = SAMPLES_PER_CHUNK;
        if(analog_mic_enable == 0)
        {
        	_32SampleBufPtr = (u8 *)&(_32SampleBuf[0]);
        }

        if(gblGetQuit(REC_QID | STRM_QID | RFTX_QID))
        {
            is_cap_finishing    = 1;
            cleanup(THREAD_SUCCESS, REC_QID | STRM_QID | RFTX_QID);
        }
        log_threads("enter snd_pcm_readi\r\n");

		if(analog_mic_enable == 0)
		{
            if(_32SampleBufPtr != NULL)
            {
        	   numSamples = snd_pcm_readi(capture_handle, _32SampleBufPtr, readSamples);
            }
            else
            {
                ERR("Function snd_pcm_readi failed\r\n");
                logEvent(log_REC_APL_REC_RUNTIME_ERROR);
                is_rec_failed       = 1;
                // is_rftx_failed      = 1;
                is_stream_failed    = 1;
            }
        }
		else 
		{
        	numSamples = snd_pcm_readi(capture_handle, hCapBufPtr, readSamples);
        }
        
        log_threads("exit snd_pcm_readi\r\n");

        if(first_time_measure == 0)  // снятие временной метки в самом начале, дальше прибавляем ровные интервалы
        {
            gettimeofday(&first_aframe_time, NULL);
            timestamp = (u64)first_aframe_time.tv_sec * (u64)MS_IN_SEC + (u64)first_aframe_time.tv_usec / (u64)MS_IN_SEC;
            debug("ACapture: first chunck time %llu\r\n", timestamp);
            first_time_measure = 1;
        }
        else
        {
            timestamp += 64;
        }

        if (numSamples == -EAGAIN)
        {
            continue;
        }
        else if(numSamples == -EBADFD)
        {
            WARN("-EBADFD\r\n");
        }
        else if(numSamples == -EPIPE)
        {
            WARN("-EPIPE\r\n");
            // if (snd_pcm_prepare(capture_handle) < 0)
            // {
            //     ERR("Failed to recover from over or underrun\r\n");
            // }
        }
        else if(numSamples == -ESTRPIPE)
        {
            WARN("-ESTRPIPE\r\n");
        }
        
        if (numSamples != readSamples)
        {
            ERR("Failed to read from PCM\r\n");
            logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }

        readSamples        -= numSamples;

		if(analog_mic_enable == 0)
		{
	        // convert 32bit sample to 16bit sample if digital mic connected

	        while (readSamples < SAMPLES_PER_CHUNK) 
	        {
	            if(gblGetQuit(REC_QID | STRM_QID | RFTX_QID))
	            {
	                is_cap_finishing    = 1;
	                cleanup(THREAD_SUCCESS, REC_QID | STRM_QID | RFTX_QID);
	            }

	            temp_sample = _32SampleBuf[readSamples] >> (16 - digital_mic_gain);
	            if(temp_sample < 0)
	            {
	                if((temp_sample & 0xFFFF0000) != 0xFFFF0000)
	                {
	                    temp_sample = 0xFFFF8000;
	                }
	                else
	                {
	                    temp_sample = temp_sample | 0x00008000;
	                }
	            }
	            else
	            {
	                if((temp_sample & 0xFFFF0000) != 0x00000000)
	                {
	                    temp_sample = 0x00007FFF;
	                }
	                else
	                {
	                    temp_sample = temp_sample & 0xFFFF7FFF;
	                }
	            }
	            *hCapBufPtr = (u16)temp_sample;
	            readSamples++;
	            hCapBufPtr++;
	        }
	    }

    	Buffer_setNumBytesUsed(hCapBuf, Buffer_getSize(hCapBuf));
#else
        /* Read samples from the Sound device */
        if (Sound_read(hSound, hCapBuf) < 0) 
        {
            ERR("Failed to read audio buffer\r\n");
            logEvent(log_REC_APL_REC_RUNTIME_ERROR);
            is_rec_failed       = 1;
            // is_rftx_failed      = 1;
            is_stream_failed    = 1;
            is_cap_finishing    = 1;
            cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
        }
#endif
        // если закончили работать с буфером - отпускаем его
        SetBufferReady(eACAPTURE_SRC, hCapBuf, timestamp, noKey);

        /* Incremement statistics */
        iters++; 	// счетчик итераций нити за 1 час
        chunks++; 	// счетчик кусков звука захваченных
        // if(iters % 20 == 0)
        // {
        //    debug("aud_cap: %i\r\n", iters);
        // }


        if(iters == AUDIO_CHUNKS_PER_HOUR)
        {
            iters = 0; // через 1 час обнуляем - начинаем новый файл
        }

    }

cleanup: // завершаем нить

    debug("ACapture thread finishing...\r\n");

    is_cap_finishing    = 1; // флаг процесса останова группы нитей

    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
    if(envp->hRendezvousCapture != NULL)
    {
        Rendezvous_force(envp->hRendezvousCapture);
    }

    /* Make sure the other threads aren't waiting for us */
    if(envp->hRendezvousAllCapFinished != NULL)
    {
        Rendezvous_meet(envp->hRendezvousAllCapFinished);
    }
  
    if(hBufTab != NULL)
    {
        FreeTransportBufGroup(eACAPTURE_SRC);
    }
    
    if(hBufTab != NULL)
    {
        ret = BufTab_delete(hBufTab);
        if(ret < 0)
        {
            ERR("Failed to delete audio capture buffers %i\r\n", ret);
        }
    }

#ifdef PCM
    if(capture_handle)
    {
        snd_pcm_close(capture_handle);
    }

    snd_config_update_free_global();
#else
    /* Clean up the thread before exiting */
    if(hSound) 
    {
        Sound_delete(hSound);
    }
#endif
    debug("Audio capture thread finished\r\n");
    return status;
}
