/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2004 Live Networks, Inc.  All rights reserved.
// A filter that breaks up an AC3 audio elementary stream into frames
// C++ header

#ifndef _AC3_AUDIO_STREAM_FRAMER_HH
#define _AC3_AUDIO_STREAM_FRAMER_HH

#ifndef _FRAMED_FILTER_HH
#include "FramedFilter.hh"
#endif

class AC3AudioStreamFramer: public FramedFilter {
public:
  static AC3AudioStreamFramer*
  createNew(UsageEnvironment& env, FramedSource* inputSource,
	    unsigned char streamCode = 0x80);

  unsigned samplingRate();

private:
  AC3AudioStreamFramer(UsageEnvironment& env, FramedSource* inputSource,
		       unsigned char streamCode);
      // called only by createNew()
  virtual ~AC3AudioStreamFramer();

  static void handleNewData(void* clientData,
			    unsigned char* ptr, unsigned size,
			    struct timeval presentationTime);
  void handleNewData(unsigned char* ptr, unsigned size);

  void parseNextFrame();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  struct timeval currentFramePlayTime() const;

private:
  struct timeval fNextFramePresentationTime;

private: // parsing state
  class AC3AudioStreamParser* fParser;
  unsigned char fOurStreamCode;
  friend class AC3AudioStreamParser; // hack
};

#endif
