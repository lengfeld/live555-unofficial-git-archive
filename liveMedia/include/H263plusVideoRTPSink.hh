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
// Copyright (c) 1996-2002 Live Networks, Inc.  All rights reserved.
// RTP sink for H.263+ video (RFC 2429)
// C++ header

#ifndef _H263_PLUS_VIDEO_RTP_SINK_HH
#define _H263_PLUS_VIDEO_RTP_SINK_HH

#ifndef _MULTI_FRAMED_RTP_SINK_HH
#include "MultiFramedRTPSink.hh"
#endif

class H263plusVideoRTPSink: public MultiFramedRTPSink {
public:
  static H263plusVideoRTPSink* createNew(UsageEnvironment& env, Groupsock* RTPgs,
				    unsigned char rtpPayloadFormat);
  
protected:
  H263plusVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
		  unsigned char rtpPayloadFormat);
	// called only by createNew()

  virtual ~H263plusVideoRTPSink();

private: // redefined virtual functions:
  virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
                                      unsigned char* frameStart,
                                      unsigned numBytesInFrame,
                                      struct timeval frameTimestamp,
                                      unsigned numRemainingBytes);
  virtual
  Boolean frameCanAppearAfterPacketStart(unsigned char const* frameStart,
					 unsigned numBytesInFrame) const;
  virtual unsigned specialHeaderSize() const;

  virtual char const* sdpMediaType() const;

private:
  Boolean fAreInFragmentedFrame;
};

#endif
