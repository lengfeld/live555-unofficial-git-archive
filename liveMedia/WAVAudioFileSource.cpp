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
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.
// A WAV audio file source
// Implementation

#if defined(__WIN32__) || defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

#include "WAVAudioFileSource.hh"
#include "GroupsockHelper.hh"

////////// WAVAudioFileSource //////////

WAVAudioFileSource*
WAVAudioFileSource::createNew(UsageEnvironment& env, char const* fileName) {
  do {
    FILE* fid;

    // Check for a special case file name: "stdin"
    if (strcmp(fileName, "stdin") == 0) {
      fid = stdin;
#if defined(__WIN32__) || defined(_WIN32)
      _setmode(_fileno(stdin), _O_BINARY); // convert to binary mode
#endif
    } else { 
      fid = fopen(fileName, "rb");
      if (fid == NULL) {
	env.setResultMsg("unable to open file \"",fileName, "\"");
	break;
      }
    }

    WAVAudioFileSource* newSource = new WAVAudioFileSource(env, fid);
    if (newSource != NULL && newSource->bitsPerSample() == 0) {
      // The WAV file header was apparently invalid.
      delete newSource; newSource = NULL;
    }
    return newSource;
  } while (0);

  return NULL;
}

#define nextc fgetc(fid)
#define ucEOF ((unsigned char)EOF)

static Boolean get4Bytes(FILE* fid, unsigned& result) { // little-endian
  unsigned char c0, c1, c2, c3;
  if ((c0 = nextc) == ucEOF || (c1 = nextc) == ucEOF ||
      (c2 = nextc) == ucEOF || (c3 = nextc) == ucEOF) return False;
  result = (c3<<24)|(c2<<16)|(c1<<8)|c0;
  return True;
}

static Boolean get2Bytes(FILE* fid, unsigned short& result) {//little-endian
  unsigned char c0, c1;
  if ((c0 = nextc) == ucEOF || (c1 = nextc) == ucEOF) return False;
  result = (c1<<8)|c0;
  return True;
}

static Boolean skipBytes(FILE* fid, int num) {
  while (num-- > 0) {
    if (nextc == ucEOF) return False;
  }
  return True;
}

WAVAudioFileSource::WAVAudioFileSource(UsageEnvironment& env, FILE* fid)
  : AudioInputDevice(env, 0, 0, 0, 0)/* set the real parameters later */,
    fFid(fid), fLastPlayTime(0) {
  // Check the WAV file header for validity.
  // Note: The following web pages contain info about the WAV format:
  // http://www.technology.niagarac.on.ca/courses/comp630/WavFileFormat.html
  // http://ccrma-www.stanford.edu/CCRMA/Courses/422/projects/WaveFormat/
  // http://www.ringthis.com/dev/wave_format.htm
  // http://www.lightlink.com/tjweber/StripWav/Canon.html
  // http://www.borg.com/~jglatt/tech/wave.htm
  // http://www.wotsit.org/download.asp?f=wavecomp

  Boolean success = False; // until we learn otherwise
  do {
    // RIFF Chunk:
    if (nextc != 'R' || nextc != 'I' || nextc != 'F' || nextc != 'F') break;
    if (!skipBytes(fid, 4)) break;
    if (nextc != 'W' || nextc != 'A' || nextc != 'V' || nextc != 'E') break;

    // FORMAT Chunk:
    if (nextc != 'f' || nextc != 'm' || nextc != 't' || nextc != ' ') break;
    unsigned formatLength;
    if (!get4Bytes(fid, formatLength)) break;
    unsigned short audioFormat;
    if (!get2Bytes(fid, audioFormat)) break;
    if (audioFormat != 1) { // not PCM - we can't handle this
      env.setResultMsg("Audio format is not PCM");
      break;
    }
    unsigned short numChannels;
    if (!get2Bytes(fid, numChannels)) break;
    fNumChannels = (unsigned char)numChannels;
    if (fNumChannels < 1 || fNumChannels > 2) { // invalid # channels
      char errMsg[100];
      sprintf(errMsg, "Bad # channels: %d", fNumChannels);
      env.setResultMsg(errMsg);
      break;
    }
    if (!get4Bytes(fid, fSamplingFrequency)) break;
    if (fSamplingFrequency == 0) {
      env.setResultMsg("Bad sampling frequency: 0");
      break;
    }
    if (!skipBytes(fid, 6)) break;
    unsigned short bitsPerSample;
    if (!get2Bytes(fid, bitsPerSample)) break;
    fBitsPerSample = (unsigned char)bitsPerSample;
    if (fBitsPerSample == 0) {
      env.setResultMsg("Bad bits-per-sample: 0");
      break;
    }
    if (!skipBytes(fid, formatLength - 16)) break;

    // FACT chunk (optional):
    unsigned char c = nextc;
    if (c == 'f') {
      if (nextc != 'a' || nextc != 'c' || nextc != 't') break;
      unsigned factLength;
      if (!get4Bytes(fid, factLength)) break;
      if (!skipBytes(fid, factLength)) break;
      c = nextc;
    }

    // DATA Chunk:
    if (c != 'd' || nextc != 'a' || nextc != 't' || nextc != 'a') break;
    if (!skipBytes(fid, 4)) break;

    // The header is good; the remaining data are the sample bytes.
    success = True;
  } while (0);
  
  if (!success) {
    env.setResultMsg("Bad WAV file format");
    // Set "fBitsPerSample" to zero, to indicate failure:
    fBitsPerSample = 0;
    return;
  }

  fPlayTimePerSample = 1e6/(double)fSamplingFrequency;

  // Although PCM is a sample-based format, we group samples into
  // 'frames' for efficient delivery to clients.  Set up our preferred
  // frame size to be close to 20 ms, if possible, but always no greater
  // than 1400 bytes (to ensure that it will fit in a single RTP packet)
  unsigned maxSamplesPerFrame = (1400*8)/(fNumChannels*fBitsPerSample);
  unsigned desiredSamplesPerFrame = (unsigned)(0.02*fSamplingFrequency);
  unsigned samplesPerFrame = desiredSamplesPerFrame < maxSamplesPerFrame
    ? desiredSamplesPerFrame : maxSamplesPerFrame;
  fPreferredFrameSize = (samplesPerFrame*fNumChannels*fBitsPerSample)/8;
  fPlayTimePerFrame = samplesPerFrame/(double)fSamplingFrequency;
}

WAVAudioFileSource::~WAVAudioFileSource() {
  fclose(fFid);
}

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

void WAVAudioFileSource::doGetNextFrame() {
  if (feof(fFid) || ferror(fFid)) {
    handleClosure(this);
    return;
  }

  // Try to read as many bytes as will fit in the buffer provided
  // (or "fPreferredFrameSize" if less)
  if (fPreferredFrameSize < fMaxSize) {
    fMaxSize = fPreferredFrameSize;
  }
  unsigned const bytesPerSample = (fNumChannels*fBitsPerSample)/8;
  unsigned bytesToRead = fMaxSize - fMaxSize%bytesPerSample;
  fFrameSize = fread(fTo, 1, bytesToRead, fFid);

  // Set the 'presentation time':
  if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
    // This is the first frame, so use the current time:
    gettimeofday(&fPresentationTime, &Idunno);
  } else {
    // Increment by the play time of the previous data:
    unsigned uSeconds	= fPresentationTime.tv_usec + fLastPlayTime;
    fPresentationTime.tv_sec += uSeconds/1000000;
    fPresentationTime.tv_usec = uSeconds%1000000;
  }

  // Remember the play time of this data:
  fLastPlayTime
    = (unsigned)((fPlayTimePerSample*fFrameSize)/bytesPerSample);

  // Switch to another task, and inform the reader that he has data:
#if defined(__WIN32__) || defined(_WIN32)
  // HACK: One of our applications that uses this source uses an
  // implementation of scheduleDelayedTask() that performs very badly
  // (chewing up lots of CPU time, apparently polling) on Windows.
  // Until this is fixed, we just call our "afterGetting()" function
  // directly.  This avoids infinite recursion, as long as our sink
  // is discontinuous, which is the case for the RTP sink that
  // this application uses. #####
  afterGetting(this);
#else
  nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
			(TaskFunc*)FramedSource::afterGetting, this);
#endif
}

float WAVAudioFileSource::getPlayTime(unsigned numFrames) const {
  return (float)(numFrames*fPlayTimePerFrame);
}

Boolean WAVAudioFileSource::setInputPort(int /*portIndex*/) {
  return True;
}

double WAVAudioFileSource::getAverageLevel() const {
  return 0.0;//##### fix this later
}
