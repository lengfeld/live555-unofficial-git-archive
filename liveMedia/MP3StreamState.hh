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
// A class encapsulating the state of a MP3 stream
// C++ header

#ifndef _MP3_STREAM_STATE_HH
#define _MP3_STREAM_STATE_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif
#ifndef _MP3_INTERNALS_HH
#include "MP3Internals.hh"
#endif
#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif

#include <stdio.h>

class MP3StreamState {
public:
  MP3StreamState();
  virtual ~MP3StreamState();

  void assignStream(FILE* fid, unsigned fileSize);

  unsigned findNextHeader(struct timeval& presentationTime);
  Boolean readFrame(unsigned char* outBuf, unsigned outBufSize,
		    unsigned& resultFrameSize,
		    unsigned& resultDurationInMicroseconds);
      // called after findNextHeader()

  void getAttributes(char* buffer, unsigned bufferSize) const;

  void writeGetCmd(char const* hostName, unsigned short portNum,
		   char const* fileName);

  void checkForXingHeader(); // hack for Xing VBR files

private:
  MP3FrameParams& fr() {return fCurrentFrame;}
  MP3FrameParams const& fr() const {return fCurrentFrame;}

  struct timeval currentFramePlayTime() const;
  unsigned filePlayTime() const; // in seconds

  Boolean findNextFrame();
  unsigned readFromStream(unsigned char* buf, unsigned numChars);

private:
  FILE* fFid;
  Boolean fFidIsReallyASocket;
  unsigned fFileSize;
  unsigned fNumFramesInFile;
  Boolean fIsVBR;

  MP3FrameParams fCurrentFrame;
  struct timeval fNextFramePresentationTime;
};

#endif
