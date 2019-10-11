/*
 * Copyright (C) 2017 ASK Lab (c)
 */
// A "ServerMediaSubsession" subclass for on-demand unicast streaming
// of AAC audio from a DM37x based device.
// C++ header

#ifndef _WIS_AAC_AUDIO_SERVER_MEDIA_SUBSESSION_HH
#define _WIS_AAC_AUDIO_SERVER_MEDIA_SUBSESSION_HH

#include "WISServerMediaSubsession.hh"

class WISAACAudioServerMediaSubsession: public WISServerMediaSubsession {
public:
  static WISAACAudioServerMediaSubsession*
  createNew(UsageEnvironment& env, APPROInput& Input, int audioBitrate);

private:
  WISAACAudioServerMediaSubsession(UsageEnvironment& env, APPROInput& Input, int audioBitrate);
      // called only by createNew()
  virtual ~WISAACAudioServerMediaSubsession();

private: // redefined virtual functions
  virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate);
  virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, 
                                    unsigned char rtpPayloadTypeIfDynamic,
				                            FramedSource* inputSource);
  //==
  virtual void deleteStream(unsigned clientSessionId, void*& streamToken);

private:
  unsigned fSamplingFrequency;
  unsigned fNumChannels;

public:
  void (*StartHWCallback)();
  void (*StopHWCallback)();
};

#endif
