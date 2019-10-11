// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
 * Copyright (C) 2005-2006 WIS Technologies International Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and the associated README documentation file (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
// An application that streams audio/video captured by a WIS GO7007,
// using a built-in RTSP server.
// main program

#include <signal.h>
#include <BasicUsageEnvironment.hh>
#include <getopt.h>
#include <liveMedia.hh>
#include "Err.hh"
#include "WISH264VideoServerMediaSubsession.hh"
#include "WISAACAudioServerMediaSubsession.hh"
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

unsigned 	audioSamplingFrequency;
unsigned 	audioNumChannels;
char 		wis_watchVariable 		= 0;
int 		streaming_stoped 		= 0;

extern "C" int wis_hw_started 		= 0;
extern "C" int wis_audio_enable 	= 0;
extern "C" int wis_video_enable 	= 0;
extern "C" int wis_video_hw_started = 0;
extern "C" int wis_audio_hw_started = 0;

extern "C" int wis_start_streaming(int aFreq, int channels, int aBtrt, int vBtrt);
extern "C" int wis_stop_streaming();

void StartVideoHW();
void StopVideoHW();
void StartAudioHW();
void StopAudioHW();

int wis_start_streaming(int aFreq, int channels, int aBtrt, int vBtrt)
{	
	int H264VideoBitrate = vBtrt;
	int audioOutputBitrate = aBtrt;

	portNumBits rtspServerPortNum = 8557;
	char const* H264StreamName = "stream";
	char const* streamDescription = "RTSP/RTP stream from VT20";

	audioSamplingFrequency 	= aFreq;
	audioNumChannels 		= channels;

	setpriority(PRIO_PROCESS, 0, 0);
		
	// Begin by setting up our usage environment:
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);
	APPROInput* H264InputDevice = NULL;

	*env << "Initializing...\n";

	H264InputDevice = APPROInput::createNew(*env);
	if (H264InputDevice == NULL) {
		err(*env) << "Failed to create H264 input device\n";
		// exit(1);
		delete scheduler;
		return -1;
	}

	// Create the RTSP server:
	RTSPServer* rtspServer = NULL;
	// Normal case: Streaming from a built-in RTSP server:
	rtspServer = RTSPServer::createNew(*env, rtspServerPortNum, NULL);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
		if( H264InputDevice != NULL )
		{
			Medium::close(H264InputDevice);
		}
		// exit(1);
		delete scheduler;
		return -1;
	}

	*env << "...done initializing\n";

    ServerMediaSession* sms;
    sms = ServerMediaSession::createNew(*env, H264StreamName, H264StreamName, streamDescription);

    if(wis_video_enable)
    {
    	WISH264VideoServerMediaSubsession*h264ss;
    	h264ss = WISH264VideoServerMediaSubsession::createNew(sms->envir(), *H264InputDevice, H264VideoBitrate);
    	if(h264ss)
    	{
    		h264ss->StartHWCallback = StartVideoHW;
			h264ss->StopHWCallback = StopVideoHW;
		}
    	sms->addSubsession(h264ss);
    }
    if(wis_audio_enable)
    {
    	WISAACAudioServerMediaSubsession*aacss;
    	aacss = WISAACAudioServerMediaSubsession::createNew(sms->envir(), *H264InputDevice, audioOutputBitrate);
    	if(aacss)
    	{
    		aacss->StartHWCallback = StartAudioHW;
    		aacss->StopHWCallback = StopAudioHW;
    	}
    	sms->addSubsession(aacss);
    }

    rtspServer->addServerMediaSession(sms);

    char *url = rtspServer->rtspURL(sms);
	*env << "Play this stream using the URL:\n\t" << url << "\n";	
	delete[] url;

	wis_watchVariable = 0;

	// Begin the LIVE555 event loop:
	env->taskScheduler().doEventLoop(&wis_watchVariable); // does not return

	Medium::close(rtspServer); // will also reclaim "sms" and its "ServerMediaSubsession"s

	if( H264InputDevice != NULL )
	{
		Medium::close(H264InputDevice);
	}

	env->reclaim();

	delete scheduler;

	streaming_stoped = 1;

	return 0; // only to prevent compiler warning
}

int wis_stop_streaming()
{
	while(1)
	{
		if(wis_watchVariable == 0)
		{
			wis_watchVariable = 1;

			while(1)
			{
				if(streaming_stoped == 1)
				{
					return 0;
				}
				usleep(100000);
			}
		}
		usleep(10000);
	}

	return 0;
}

void StartVideoHW()
{
	//printf("%s\n", __func__);	
	
	if(wis_video_hw_started)
		return;

	wis_video_hw_started = 1;

	while(wis_hw_started == 0)
	{
		usleep(1000);
	}
}

void StopVideoHW()
{
	//printf("%s\n", __func__);	
	if(!wis_video_hw_started)
		return;

	wis_video_hw_started = 0;
}

void StartAudioHW()
{
	//printf("%s\n", __func__);
	if(wis_audio_hw_started)
		return;

	wis_audio_hw_started = 1;
	{
		usleep(1000);
	}
}

void StopAudioHW()
{
	//printf("%s\n", __func__);

	if(!wis_audio_hw_started)
		return;
	
	wis_audio_hw_started = 0;
}