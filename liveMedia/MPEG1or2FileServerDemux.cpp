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
// A server demultiplexer for a MPEG 1 or 2 Program Stream
// Implementation

#include "MPEG1or2FileServerDemux.hh"
#include "MPEG1or2DemuxedServerMediaSubsession.hh"
#include "ByteStreamFileSource.hh"

MPEG1or2FileServerDemux*
MPEG1or2FileServerDemux::createNew(UsageEnvironment& env, char const* fileName,
				   Boolean reuseFirstSource) {
  return new MPEG1or2FileServerDemux(env, fileName, reuseFirstSource);
}

MPEG1or2FileServerDemux
::MPEG1or2FileServerDemux(UsageEnvironment& env, char const* fileName,
			  Boolean reuseFirstSource)
  : Medium(env),
    fReuseFirstSource(reuseFirstSource),
    fSession0Demux(NULL), fLastCreatedDemux(NULL), fLastClientSessionId(~0) {
  fFileName = strDup(fileName);
  fFileDuration = MPEG1or2ProgramStreamFileDuration(env, fileName);
}

MPEG1or2FileServerDemux::~MPEG1or2FileServerDemux() {
  Medium::close(fSession0Demux);
  delete[] (char*)fFileName;
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newAudioServerMediaSubsession() {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xC0, fReuseFirstSource);
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newVideoServerMediaSubsession(Boolean iFramesOnly,
						       double vshPeriod) {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xE0, fReuseFirstSource,
							 iFramesOnly, vshPeriod);
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newAC3AudioServerMediaSubsession() {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xBD, fReuseFirstSource);
  // because, in a VOB file, the AC3 audio has stream id 0xBD
}

MPEG1or2DemuxedElementaryStream*
MPEG1or2FileServerDemux::newElementaryStream(unsigned clientSessionId,
					     u_int8_t streamIdTag) {
  MPEG1or2Demux* demuxToUse;
  if (clientSessionId == 0) {
    // 'Session 0' is treated especially, because its audio & video streams
    // are created and destroyed one-at-a-time, rather than both streams being
    // created, and then (later) both streams being destroyed (as is the case
    // for other ('real') session ids).  Because of this, a separate demux is
    // used for session 0, and its deletion is managed by us, rather than
    // happening automatically.
    if (fSession0Demux == NULL) {
      // Open our input file as a 'byte-stream file source':
      ByteStreamFileSource* fileSource
	= ByteStreamFileSource::createNew(envir(), fFileName);
      if (fileSource == NULL) return NULL;
      fSession0Demux = MPEG1or2Demux::createNew(envir(), fileSource, False/*note!*/);
    }
    demuxToUse = fSession0Demux;
  } else {
    // First, check whether this is a new client session.  If so, create a new
    // demux for it:
    if (clientSessionId != fLastClientSessionId) {
      // Open our input file as a 'byte-stream file source':
      ByteStreamFileSource* fileSource
	= ByteStreamFileSource::createNew(envir(), fFileName);
      if (fileSource == NULL) return NULL;

      fLastCreatedDemux = MPEG1or2Demux::createNew(envir(), fileSource, True);
      // Note: We tell the demux to delete itself when its last
      // elementary stream is deleted.
      fLastClientSessionId = clientSessionId;
      // Note: This code relies upon the fact that the creation of streams for
      // different client sessions do not overlap - so one "MPEG1or2Demux" is used
      // at a time.
    }
    demuxToUse = fLastCreatedDemux;
  }

  if (demuxToUse == NULL) return NULL; // shouldn't happen

  return demuxToUse->newElementaryStream(streamIdTag);
}


static Boolean getMPEG1or2TimeCode(FramedSource* dataSource,
				   Boolean returnFirstSeenCode,
				   float& timeCode); // forward

float MPEG1or2ProgramStreamFileDuration(UsageEnvironment& env, char const* fileName) {
  //  fprintf(stderr, "#####@@@@@MPEG1or2ProgramStreamFileDuration(%s)\n", fileName);
  FramedSource* dataSource = NULL;
  float duration = 0.0; // until we learn otherwise

  do {
    // Open the input file as a 'byte-stream file source':
    ByteStreamFileSource* fileSource = ByteStreamFileSource::createNew(env, fileName);
    if (fileSource == NULL) break;
    dataSource = fileSource;

    unsigned fileSize = fileSource->fileSize();
    if (fileSize == 0) break;

    // Create a MPEG demultiplexor that reads from that source.
    MPEG1or2Demux* baseDemux = MPEG1or2Demux::createNew(env, dataSource);
    if (baseDemux == NULL) break;

    // Create, from this, a source that returns raw PES packets:
    dataSource = baseDemux->newRawPESStream();

    // Read the first time code from the file:
    float firstTimeCode;
    if (!getMPEG1or2TimeCode(dataSource, True, firstTimeCode)) break;

    
    // Then, read the last time code from the file.
    // (Before doing this, seek towards the end of the file, for efficiency.)
    unsigned const startByteFromEnd = 100000;
    unsigned newFilePosition
      = fileSize < startByteFromEnd ? 0 : fileSize - startByteFromEnd;
    if (newFilePosition > 0) fileSource->seekToByteAbsolute(newFilePosition);

    float lastTimeCode;
    if (!getMPEG1or2TimeCode(dataSource, False, lastTimeCode)) break;

    // Take the difference between these time codes as being the file duration:
    float timeCodeDiff = lastTimeCode - firstTimeCode;
    if (timeCodeDiff < 0) break;
    duration = timeCodeDiff;
  } while (0);

  Medium::close(dataSource);
  return duration;
}

static Boolean getMPEG1or2TimeCode(FramedSource* /*dataSource*/,
				   Boolean /*returnFirstSeenCode*/,
				   float& timeCode) {
  timeCode = 0.0; return True;//#####@@@@@
}

