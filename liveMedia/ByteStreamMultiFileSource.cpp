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
// A source that consists of multiple byte-stream files, read sequentially
// Implementation

#include "ByteStreamMultiFileSource.hh"
#include <string.h>

////////// ByteStreamFileSource //////////

ByteStreamMultiFileSource
::ByteStreamMultiFileSource(UsageEnvironment& env,
			    char const** fileNameArray)
  : FramedSource(env),
    fCurrentlyReadSourceNumber(0) {
    // Begin by counting the number of sources:
    for (fNumSources = 0; ; ++fNumSources) {
      if (fileNameArray[fNumSources] == NULL) break;
    }

    // Next, copy the source file names into our own array:
    fFileNameArray = new (char const*)[fNumSources];
    if (fFileNameArray == NULL) return;
    unsigned i;
    for (i = 0; i < fNumSources; ++i) {
      fFileNameArray[i] = strdup(fileNameArray[i]);
    }

    // Next, set up our array of component ByteStreamFileSources
    // Don't actually create these yet; instead, do this on demand
    fSourceArray = new (ByteStreamFileSource*)[fNumSources];
    if (fSourceArray == NULL) return;
    for (i = 0; i < fNumSources; ++i) {
      fSourceArray[i] = NULL;
    }
}

ByteStreamMultiFileSource::~ByteStreamMultiFileSource() {
  unsigned i;
  for (i = 0; i < fNumSources; ++i) {
    Medium::close(fSourceArray[i]);
  }
  delete fSourceArray;

  for (i = 0; i < fNumSources; ++i) {
    delete fFileNameArray[i];
  }
  delete fFileNameArray;
}

ByteStreamMultiFileSource* ByteStreamMultiFileSource
::createNew(UsageEnvironment& env, char const** fileNameArray) {
  ByteStreamMultiFileSource* newSource
    = new ByteStreamMultiFileSource(env, fileNameArray);

  return newSource;
}

void ByteStreamMultiFileSource::doGetNextFrame() {
  do {
    // First, check whether we've run out of sources:
    if (fCurrentlyReadSourceNumber >= fNumSources) break;

    ByteStreamFileSource*& source
      = fSourceArray[fCurrentlyReadSourceNumber];
    if (source == NULL) {
      // The current source hasn't been created yet.  Do this now:
      source = ByteStreamFileSource::createNew(envir(),
			     fFileNameArray[fCurrentlyReadSourceNumber]);
      if (source == NULL) break;
    }      

    // (Attempt to) read from the current source.
    source->getNextFrame(fTo, fMaxSize,
			       afterGettingFrame, this,
			       onSourceClosure, this);
  } while (0);

  // An error occurred; consider ourselves closed:
  handleClosure(this);
}

void ByteStreamMultiFileSource
  ::afterGettingFrame(void* clientData,
		      unsigned frameSize, struct timeval presentationTime) {
  ByteStreamMultiFileSource* source
    = (ByteStreamMultiFileSource*)clientData;
  source->fFrameSize = frameSize;
  source->fPresentationTime = presentationTime;
  FramedSource::afterGetting(source);
}

void ByteStreamMultiFileSource::onSourceClosure(void* clientData) {
  ByteStreamMultiFileSource* source
    = (ByteStreamMultiFileSource*)clientData;
  source->onSourceClosure1();
}

void ByteStreamMultiFileSource::onSourceClosure1() {
  // This routine was called because the currently-read source was closed
  // (probably due to EOF).  Close this source down, and move to the
  // next one:
  ByteStreamFileSource*& source
    = fSourceArray[fCurrentlyReadSourceNumber++];
  Medium::close(source);
  source = NULL;

  // Try reading again:
  doGetNextFrame();
}

float ByteStreamMultiFileSource::getPlayTime(unsigned numFrames) const {
  // Because the constituent sources are not created up-front, we can't
  // just add up their individual play times.
  // So, instead, just return 0.0
  return 0.0;
}

