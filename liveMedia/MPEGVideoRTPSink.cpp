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
// RTP sink for MPEG video (RFC 2250)
// Implementation

#include "MPEGVideoRTPSink.hh"
#include "MPEGVideoStreamFramer.hh" // hack #####

MPEGVideoRTPSink::MPEGVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs)
  : MultiFramedRTPSink(env, RTPgs, 32, 90000, "MPV") {
  fPictureState.temporal_reference = 0;
  fPictureState.picture_coding_type = fPictureState.vector_code_bits = 0;
}

MPEGVideoRTPSink::~MPEGVideoRTPSink() {
}

MPEGVideoRTPSink*
MPEGVideoRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs) {
  return new MPEGVideoRTPSink(env, RTPgs);
}

Boolean MPEGVideoRTPSink
::frameCanAppearAfterPacketStart(unsigned char const* frameStart,
				 unsigned numBytesInFrame) const {
  // A frame can appear at other than the first position in a packet
  // only if the previous frame was not a slice.
  return !fPreviousFrameWasSlice;
}

#define VIDEO_SEQUENCE_HEADER_START_CODE 0x000001B3
#define PICTURE_START_CODE               0x00000100

void MPEGVideoRTPSink
::doSpecialFrameHandling(unsigned fragmentationOffset,
			 unsigned char* frameStart,
			 unsigned numBytesInFrame,
			 struct timeval frameTimestamp,
			 unsigned numRemainingBytes) {
  Boolean thisFrameIsASlice = False; // until we learn otherwise
  if (isFirstFrameInPacket()) {
    fSequenceHeaderPresent = fPacketBeginsSlice = fPacketEndsSlice = False; 
  }

  // Begin by inspecting the 4-byte code at the start of the frame:
  if (numBytesInFrame < 4) return; // shouldn't happen
  unsigned startCode = (frameStart[0]<<24) | (frameStart[1]<<16)
                     | (frameStart[2]<<8) | frameStart[3];

  if (startCode == VIDEO_SEQUENCE_HEADER_START_CODE) {
    // This is a video sequence header
    fSequenceHeaderPresent = True;
  } else if (startCode == PICTURE_START_CODE) {
    // This is a picture header

    // Record the parameters of this picture:
    if (numBytesInFrame < 8) return; // shouldn't happen
    unsigned next4Bytes = (frameStart[4]<<24) | (frameStart[5]<<16)
                        | (frameStart[6]<<8) | frameStart[7];
    unsigned char byte8 = numBytesInFrame == 8 ? 0 : frameStart[8];

    fPictureState.temporal_reference = (next4Bytes&0xFFC00000)>>(32-10);
    fPictureState.picture_coding_type = (next4Bytes&0x00380000)>>(32-(10+3)); 

    unsigned char FBV, BFC, FFV, FFC;
    FBV = BFC = FFV = FFC = 0;
    switch (fPictureState.picture_coding_type) {
    case 3:
      FBV = (byte8&0x40)>>6;
      BFC = (byte8&0x38)>>3;
      // fall through to:
    case 2:
      FFV = (next4Bytes&0x00000004)>>2;
      FFC = ((next4Bytes&0x00000003)<<1) | ((byte8&0x80)>>7);
    }

    fPictureState.vector_code_bits = (FBV<<7) | (BFC<<4) | (FFV<<3) | FFC;
  } else if ((startCode&0xFFFFFF00) == 0x00000100) {
    unsigned char lastCodeByte = startCode&0xFF;

    if (lastCodeByte <= 0xAF) {
      // This is (the start of) a slice
      thisFrameIsASlice = True;

      if (fragmentationOffset > 0) { // sanity check
	fprintf(stderr, "Warning: MPEGVideoRTPSink::doSpecialFrameHandling saw slice start code 0x%08x, but also fragmentationOffset %d\n", startCode, fragmentationOffset);
      }
    } else {
      // This is probably a GOP header; we don't do anything with this
    }
  } else {
    // The first 4 bytes aren't a code that we recognize.  This should
    // happen only if we're a fragment (other than the first) of a slice.
    thisFrameIsASlice = True;

    if (fragmentationOffset == 0) { // sanity check
      fprintf(stderr, "Warning: MPEGVideoRTPSink::doSpecialFrameHandling saw strange first 4 bytes 0x%08x, but we're not a fragment\n", startCode);
    }
  }

  if (thisFrameIsASlice) {
    // This packet begins a slice iff there's no fragmentation offset:
    fPacketBeginsSlice = (fragmentationOffset == 0);

    // This packet also ends a slice iff there are no fragments remaining:
    fPacketEndsSlice = (numRemainingBytes == 0);
  }

  // Set the video-specific header based on the parameters that we've seen.
  // Note that this may get done more than once, if several frames appear
  // in the packet.  That's OK, because this situation happens infrequently,
  // and we want the video-specific header to reflect the most up-to-date
  // information (in particular, from a Picture Header) anyway.
  unsigned videoSpecificHeader = 
    // T == 0
    (fPictureState.temporal_reference<<16) |
    // AN == N == 0
    (fSequenceHeaderPresent<<13) |
    (fPacketBeginsSlice<<12) |
    (fPacketEndsSlice<<11) |
    (fPictureState.picture_coding_type<<8) |
    fPictureState.vector_code_bits;
  setSpecialHeaderWord(videoSpecificHeader);

  // Also set the RTP timestamp.  (As above, we do this for each frame
  // in the packet.)
  setTimestamp(frameTimestamp);

  // Set the RTP 'M' (marker) bit iff this frame ends (i.e., is the last
  // slice of) a picture.  This relies on the source being a
  // "MPEGVideoStreamFramer", and so is a hack.  Eventually, we need a
  // more general mechanism for accessing per-frame meta-data like this.
  MPEGVideoStreamFramer* framerSource = (MPEGVideoStreamFramer*)fSource;
  if (framerSource != NULL && framerSource->fPictureEndMarker) {
    setMarkerBit();
    framerSource->fPictureEndMarker = False;
  }

  fPreviousFrameWasSlice = thisFrameIsASlice;
}


unsigned MPEGVideoRTPSink::specialHeaderSize() const {
  // There's a 4 byte special audio header:
  return 4;
}

char const* MPEGVideoRTPSink::sdpMediaType() const {
  return "video";
}
