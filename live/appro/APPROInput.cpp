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
// An interface to the WIS GO7007 capture device.
// Implementation
    
#include "APPROInput.hh"
//#include "Options.hh"
#include "Err.hh"
#include "Base64.hh"
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/soundcard.h>

extern "C" {
	#include "../../writer_data.h"
}

// #define STREAM_DETAIL_OUTPUT

extern unsigned audioSamplingFrequency;
extern unsigned audioNumChannels;
extern int wis_audio_enable;

int ts_flag = 0;

#define RET_SUCCESS				0
#define RET_NO_VALID_DATA		1


////////// OpenFileSource definition //////////

// A common "FramedSource" subclass, used for reading from an open file:

class OpenFileSource: public FramedSource {
public:
  int  uSecsToDelay;
  int  uSecsToDelayMax;  
  int i;
protected:
  OpenFileSource(UsageEnvironment& env, APPROInput& input);
  virtual ~OpenFileSource();

  virtual int readFromFile() = 0;

private: // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void incomingDataHandler(OpenFileSource* source);
  void incomingDataHandler1();
  
protected:
  APPROInput& fInput;
};


////////// VideoOpenFileSource definition //////////

class VideoOpenFileSource: public OpenFileSource {
public:
  VideoOpenFileSource(UsageEnvironment& env, APPROInput& input);
  virtual ~VideoOpenFileSource();

protected: // redefined virtual functions:
  virtual int readFromFile();
  
  unsigned int SerialBook;
  unsigned int SerialLock;
  int StreamFlag;
};

#define STREAM_GET_VOL    0x0001
#define STREAM_NEW_GOP    0x0002

////////// AudioOpenFileSource definition //////////

class AudioOpenFileSource: public OpenFileSource {
public:
  AudioOpenFileSource(UsageEnvironment& env, APPROInput& input);
  virtual ~AudioOpenFileSource();

protected: // redefined virtual functions:
  virtual int readFromFile();
  int getAudioData();

  unsigned int AudioBook;
  unsigned int AudioLock;
};

long timevaldiff(struct timeval *starttime, struct timeval *finishtime)
{
  long msec;
  msec=(finishtime->tv_sec-starttime->tv_sec)*1000;
  msec+=(finishtime->tv_usec-starttime->tv_usec)/1000;
  return msec;
}

void printErr(UsageEnvironment& env, char const* str = NULL) {
  if (str != NULL) err(env) << str;
  env << ": " << strerror(env.getErrno()) << "\n";
}

////////// APPROInput implementation //////////

APPROInput* APPROInput::createNew(UsageEnvironment& env) {
  return new APPROInput(env);
}

FramedSource* APPROInput::videoSource() {

  if (fOurVideoSource == NULL) {
    fOurVideoSource = new VideoOpenFileSource(envir(), *this);
  }

  return fOurVideoSource;
}

FramedSource* APPROInput::audioSource() {

  if (fOurAudioSource == NULL) {
    fOurAudioSource = new AudioOpenFileSource(envir(), *this);
  }

  return fOurAudioSource;
}

APPROInput::APPROInput(UsageEnvironment& env)
  : Medium(env), fOurVideoSource(NULL), fOurAudioSource(NULL) {
}

APPROInput::~APPROInput() 
{
	if( fOurVideoSource )
	{
		delete (VideoOpenFileSource *)fOurVideoSource;
		fOurVideoSource = NULL;
	}

	if( fOurAudioSource )
	{
		delete (AudioOpenFileSource *)fOurAudioSource;
		fOurAudioSource = NULL;
	}
}

#include <stdio.h>
#include <stdlib.h>

////////// OpenFileSource implementation //////////

OpenFileSource
::OpenFileSource(UsageEnvironment& env, APPROInput& input)
  : FramedSource(env),
    fInput(input) {
}

OpenFileSource::~OpenFileSource() {
}

void OpenFileSource::doGetNextFrame() {
	// printf("WHOWHOWHO %i\r\n", i);
	incomingDataHandler(this);
}

void OpenFileSource
::incomingDataHandler(OpenFileSource* source) {
  source->incomingDataHandler1();
}

void OpenFileSource::incomingDataHandler1() {
	int ret;

	if (!isCurrentlyAwaitingData()) return; // we're not ready for the data yet

	ret = readFromFile();
	if (ret < 0) {
		handleClosure(this);
		fprintf(stderr,"In Grab Image, the source stops being readable!!!!\n");
	}
	else 
	{
		if (ret == 0)
		{
			if( uSecsToDelay >= uSecsToDelayMax )
			{
				uSecsToDelay = uSecsToDelayMax;
			}
			else
			{			
				uSecsToDelay *= 2;
			}
			nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)incomingDataHandler, this);
		}
		else
		{
			nextTask() = envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)afterGetting, this);
		}
	}
}

////////// VideoOpenFileSource implementation //////////

VideoOpenFileSource
::VideoOpenFileSource(UsageEnvironment& env, APPROInput& input)
  : OpenFileSource(env, input), SerialBook(0), SerialLock(0), StreamFlag(STREAM_GET_VOL) {

 i = 1;
 uSecsToDelay = 1000;
 uSecsToDelayMax = 1666;//33000/4; 
}

VideoOpenFileSource::~VideoOpenFileSource()
{
  fInput.fOurVideoSource = NULL;
  SerialLock = 0;
}

int WaitVideoStart(int sleep_unit )
{
	AV_DATA av_data;
	int cnt = 0;
	int serialnum = -1;

	while(1)
	{
		av_data.serial = -1;
		GetMP4_Serial(&av_data);
		
		if( (int)av_data.serial < 0 )
		{
			printf("Stream is not avaliable~~~~~~~~\n");
			//sigterm(0);
			//exit(1);
			//break;
			return -1;
		}

		if( av_data.flags != AV_FLAGS_MP4_I_FRAME )
		{
			usleep(sleep_unit);
		}
		else
		{
			serialnum = av_data.serial ;
			printf("%s: found key frame(%i)\n", __func__, serialnum);
			break;
		}
		cnt++;
		if( cnt > 10000 )
			break;
	}

	return serialnum;
}

int GetVideoSerial( )
{
	AV_DATA av_data;

	GetMP4_Serial(&av_data);

	return av_data.serial;
}

#ifdef STREAM_DETAIL_OUTPUT
#define	RUN_FRAME_NUM	(1000)
void PrintStreamDetails(void)
{
	static int strmfrmCnt = 0;
	static int strmfrmSkip = 0;
	static struct timeval startTime, endTime, passTime;
	double calcTime;

	if (strmfrmSkip < 150) {
		strmfrmSkip++;
	} else {
		if (strmfrmCnt == 0)
			gettimeofday(&startTime, NULL);
		if (strmfrmCnt == RUN_FRAME_NUM) {
			gettimeofday(&endTime, NULL);
			printf("\n==================== STREAMING DETAILS ====================\n");
			printf("Start Time : %ldsec %06ldusec\n", (long)startTime.tv_sec, (long)startTime.tv_usec);
			printf("End Time   : %ldsec %06ldusec\n", (long)endTime.tv_sec, (long)endTime.tv_usec);
			timersub(&endTime, &startTime, &passTime);
			calcTime = (double)passTime.tv_sec*1000.0 + (double)passTime.tv_usec/1000.0;
			printf("Total Time to stream %d frames: %.3f msec\n", RUN_FRAME_NUM , calcTime);
			printf("Time per frame: %3.4f msec\n", calcTime/RUN_FRAME_NUM);
			printf("Streaming Performance in FPS: %3.4f\n", RUN_FRAME_NUM/(calcTime/1000));
			printf("===========================================================\n");
			strmfrmCnt 	= 0;
			strmfrmSkip = 0;
		} else {
			strmfrmCnt++;
		}
	}
}
#endif

int VideoOpenFileSource::readFromFile()
{
	AV_DATA av_data;
	int ret;
	int start = 0;
	
	if (StreamFlag & STREAM_GET_VOL) 
	{
		//printf("mp4 vol searching\n");

		if(WaitVideoStart(10000) < 0)
		{
			return -1;
		}

		ret = GetMP4_I_Frame(&av_data);
		SerialBook = av_data.serial;
		StreamFlag &= ~(STREAM_GET_VOL|STREAM_NEW_GOP);
	}
	else if (StreamFlag & STREAM_NEW_GOP) {

		printf("New gop searching\n");
		if(WaitVideoStart(5000) < 0)
		{
			return -1;
		}

		ret = GetMP4_I_Frame(&av_data);
		SerialBook = av_data.serial;
		StreamFlag &= ~STREAM_NEW_GOP;
	}
	else {
		ret = GetMP4_Frame(SerialBook, &av_data);
	}

	if (ret == RET_SUCCESS)
	{
		static int IscheckKey = 1;
		if( av_data.flags == AV_FLAGS_MP4_I_FRAME && IscheckKey == 1 )
		{
			int serial_now;
			serial_now = GetVideoSerial();
			IscheckKey = 0;
			//printf("serial_now = %d SerialBook = %d \n",serial_now,SerialBook);
			if( (serial_now - SerialBook) > 30 )
			{
				StreamFlag |= STREAM_NEW_GOP;
				printf("New gop\n");
				return 0;
			}
		}else{
			IscheckKey = 1;
		}
	}

	if (ret == RET_SUCCESS) 
	{
		start = 4;
		
		fFrameSize = av_data.size - start;

		if (fFrameSize > fMaxSize) 
		{
			printf("Frame Truncated\n");
			fNumTruncatedBytes = fFrameSize - fMaxSize;
			fFrameSize = fMaxSize;
		}
		else {
			fNumTruncatedBytes = 0;
		}
		memcpy(fTo, av_data.ptr+start, fFrameSize);
		FreeMP4_Frame(av_data.serial);

#ifdef STREAM_DETAIL_OUTPUT
		PrintStreamDetails();
#endif			
		SerialLock = SerialBook;

		// Note the timestamp and size:
		fPresentationTime.tv_sec = av_data.timestamp/1000;
		fPresentationTime.tv_usec = (av_data.timestamp%1000)*1000;
	
		fDurationInMicroseconds = 40000;//if fps is 25

		SerialBook++;
		return 1;
	}
	else if (ret == RET_NO_VALID_DATA) {
		//usleep(5000);
		return 0;
	}
	else {
		StreamFlag |= STREAM_NEW_GOP;
		printf("New gop 1\n");
		return 0;
	}
 	
 	return 0;
}


////////// AudioOpenFileSource implementation //////////

AudioOpenFileSource
::AudioOpenFileSource(UsageEnvironment& env, APPROInput& input)
  : OpenFileSource(env, input), AudioBook(0), AudioLock(0)
{
  i = 2;
  uSecsToDelay = 5000;
  uSecsToDelayMax = 125000;  
}

AudioOpenFileSource::~AudioOpenFileSource() 
{
	fInput.fOurAudioSource = NULL;
}

int AudioOpenFileSource::getAudioData()
{
	AV_DATA av_data;
	int ret;

	if (AudioBook == 0) {
		GetAACSerial(&av_data);
		if (av_data.serial <= AudioLock) {
			printf("av_data.serial <= audio_lock!!!\n");
			return 0;
		}
		AudioBook = av_data.serial;
	}

	ret = GetAACFrame( AudioBook, &av_data );
	if (ret == RET_SUCCESS)
	{
		if (av_data.size > fMaxSize)
			av_data.size = fMaxSize;
		memcpy(fTo, av_data.ptr, av_data.size);
		FreeAACFrame(av_data.serial);

		AudioLock = av_data.serial;
		AudioBook = av_data.serial + 1;

		fPresentationTime.tv_sec = av_data.timestamp/1000;
		fPresentationTime.tv_usec = (av_data.timestamp%1000)*1000;
		
 		return av_data.size;
	}
	else
		if (ret == RET_NO_VALID_DATA)
		{
			return 0;
		}
		else
		{
			AudioBook = 0;
			return -1;
		}
}

int AudioOpenFileSource::readFromFile()
{
	if (!wis_audio_enable)
		return 0;

	// Read available audio data:
	int ret = getAudioData();

	if (ret <= 0) 
		return 0;

	fFrameSize = (unsigned)ret;
	fNumTruncatedBytes = 0;
	fDurationInMicroseconds = 1024*1000/audioSamplingFrequency;
	
	return 1;
}

int GetVolInfo(void *pBuff, int bufflen)
{	
	AV_DATA vol_data;

	if(GetMP4Vol(&vol_data) != RET_SUCCESS)
	{
		printf("Error on Get Vol data\n");
		return 0;
	}

	memcpy(pBuff, vol_data.ptr, vol_data.size);
	
	return vol_data.size;
}

int GetSprop(void *pBuff)
{
	static char tempBuff[512];
	int ret = 0;
	int cnt = 0;
	int IsSPS = 0;
	int IsPPS = 0;
	int SPS_LEN = 0;
	int PPS_LEN = 4;
	char *pSPS=tempBuff;//0x7
	char *pPPS=tempBuff;//0x8
	char *pSPSEncode=NULL;
	char *pPPSEncode=NULL;

	ret = GetVolInfo(tempBuff,sizeof(tempBuff));

	if(ret <=0 )
	{
		return 0;
	}

	for(;;)
	{
		if(pSPS[0]==0&&pSPS[1]==0&&pSPS[2]==0&&pSPS[3]==1)
		{
			if( (pSPS[4]& 0x1F) == 7 )
			{
				IsSPS = 1;
				break;
			}
		}
		pSPS++;
		cnt++;
		if( (cnt+4)>ret )
			break;
	}
	if(IsSPS)
		pSPS += 4;

	cnt = 0;
	for(;;)
	{
		if(pPPS[0]==0&&pPPS[1]==0&&pPPS[2]==0&&pPPS[3]==1)
		{
			if( (pPPS[4]& 0x1F) == 8 )
			{
				IsPPS = 1;
				break;
			}
		}
		pPPS++;
		cnt++;
		if( (cnt+4)>ret )
			break;
	}

	SPS_LEN = (unsigned long)pPPS - (unsigned long)pSPS;

	if(IsPPS)
		pPPS += 4;

	PPS_LEN = ret - (unsigned long)(pPPS - tempBuff);

	pSPSEncode = base64Encode(pSPS,SPS_LEN);
	pPPSEncode = base64Encode(pPPS,PPS_LEN);

	sprintf((char *)pBuff,"%s,%s",(char *)pSPSEncode,(char *)pPPSEncode);
	//printf("pBuff = %s \n",(char *)pBuff);

	delete[] pSPSEncode;
	delete[] pPPSEncode;

	return 1;
}

