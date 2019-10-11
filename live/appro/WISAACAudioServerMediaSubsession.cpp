// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
 * Copyright (C) 2017 ASK-Lab
 */
// A "ServerMediaSubsession" subclass for on-demand unicast streaming
// of AAC audio from a DM37x DSP based device.
// Implementation

#include "WISAACAudioServerMediaSubsession.hh"
#include <liveMedia.hh>

extern unsigned audioSamplingFrequency;
extern unsigned audioNumChannels;

WISAACAudioServerMediaSubsession* WISAACAudioServerMediaSubsession
::createNew(UsageEnvironment& env, APPROInput& Input, int audioBitrate) 
{
  return new WISAACAudioServerMediaSubsession(env, Input, audioBitrate);
}

WISAACAudioServerMediaSubsession
::WISAACAudioServerMediaSubsession(UsageEnvironment& env, APPROInput& Input, int audioBitrate)
  : WISServerMediaSubsession(env, Input, audioBitrate) {
  	StartHWCallback = NULL;
  	StopHWCallback = NULL;
}

WISAACAudioServerMediaSubsession::~WISAACAudioServerMediaSubsession() {
}

FramedSource* WISAACAudioServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned& estBitrate) 
{
  estBitrate = fEstimatedKbps;
  return fWISInput.audioSource();
}

RTPSink* WISAACAudioServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* /*inputSource*/) 
{
	int objectType = 2;//AAC-LC
	unsigned freq_table[] = {96000, 84200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
	int freq_index = -1;
	unsigned char config_buf[2];
	char encoderConfigStr[5];

	for(unsigned i=0; i<(sizeof(freq_table)/sizeof(int)); i++)
	{
		if(freq_table[i] == audioSamplingFrequency)
		{
			freq_index = i;
			break;
		}
	}
	if(freq_index < 0)
		return NULL;

	config_buf[0] = (objectType << 3) | (freq_index >> 1);

	config_buf[1] = ((freq_index << 7) & 0xFF) | (audioNumChannels << 3);// | 0(3 bits)

	sprintf(encoderConfigStr, "%.2x%.2x", config_buf[0], config_buf[1]);

	//==  
	if(StartHWCallback)
		StartHWCallback();
	//==
  	setVideoRTPSinkBufferSize();

  	return MPEG4GenericRTPSink::createNew( envir(), rtpGroupsock,
									       rtpPayloadTypeIfDynamic,
									       audioSamplingFrequency,
									       "audio", "AAC-hbr",
									       encoderConfigStr, audioNumChannels);

/*	if( (audioSamplingFrequency == 16000) && (audioNumChannels == 1))
	{
		char const* encoderConfigStr = "1408";//"1408";// (2<<3)|(8>>1) = 0x14 ; ((8<<7)&0xFF)|(1<<3)=0x08;
		return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
						       rtpPayloadTypeIfDynamic,
						       audioSamplingFrequency,
						       "audio", "AAC-hbr",
						       encoderConfigStr, audioNumChannels);
	}
	else
		if( (audioSamplingFrequency == 16000) && (audioNumChannels == 2))
		{
			char const* encoderConfigStr = "1410";// (2<<3)|(8>>1) = 0x14 ; ((8<<7)&0xFF)|(2<<3)=0x10;
			return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
							       rtpPayloadTypeIfDynamic,
							       audioSamplingFrequency,
							       "audio", "AAC-hbr",
							       encoderConfigStr, audioNumChannels);
		}
		else
			if( (audioSamplingFrequency == 32000) && (audioNumChannels == 1))
			{
				char const* encoderConfigStr = "1288";// (2<<3)|(5>>1) = 0x12 ; ((5<<7)&0xFF)|(1<<3)=0x08;
				return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
							       rtpPayloadTypeIfDynamic,
							       audioSamplingFrequency,
							       "audio", "AAC-hbr",
							       encoderConfigStr, audioNumChannels);
			}			
			else
				if( (audioSamplingFrequency == 32000) && (audioNumChannels == 2))
				{
					char const* encoderConfigStr = "1290";// (2<<3)|(5>>1) = 0x12 ; ((5<<7)&0xFF)|(2<<3)=0x90;
					return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
								       rtpPayloadTypeIfDynamic,
								       audioSamplingFrequency,
								       "audio", "AAC-hbr",
								       encoderConfigStr, audioNumChannels);
				}
				else
				{
					return NULL;//==
				}*/
}

//==
void WISAACAudioServerMediaSubsession
::deleteStream(unsigned clientSessionId, void*& streamToken)
{
	if(StopHWCallback)
		StopHWCallback();

	WISServerMediaSubsession::deleteStream(clientSessionId, streamToken);
}