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
// Abstract class for parsing a byte stream
// C++ header

#ifndef _STREAM_PARSER_HH
#define _STREAM_PARSER_HH

#ifndef _FRAMED_SOURCE_HH 
#include "FramedSource.hh"
#endif

// A structure for holding input data to be parsed.
// (Eventually replace with iostream stuff) ##### 
#define BANK_SIZE 100000

class StreamParser {
protected: // we're a virtual base class
  typedef void (clientContinueFunc)(void* clientData,
				    unsigned char* ptr, unsigned size);
  StreamParser(FramedSource* inputSource,
	       FramedSource::onCloseFunc* onInputCloseFunc,
	       void* onInputCloseClientData,
	       clientContinueFunc* clientContinueFunc,
	       void* clientContinueClientData);
  virtual ~StreamParser();

  void saveParserState();
  virtual void restoreSavedParserState();

  unsigned get4Bytes() { // byte-aligned; returned in big-endian order
    unsigned result = test4Bytes();
    fCurParserIndex += 4;
    fRemainingUnparsedBits = 0;

    return result;
  }
  unsigned test4Bytes() { // as above, but doesn't advance ptr
    ensureValidBytes(4);

    unsigned char const* ptr = nextToParse();
    return (ptr[0]<<24)|(ptr[1]<<16)|(ptr[2]<<8)|ptr[3];
  }

  unsigned short get2Bytes() {
    ensureValidBytes(2);

    unsigned char const* ptr = nextToParse();
    unsigned short result = (ptr[0]<<8)|ptr[1];

    fCurParserIndex += 2;
    fRemainingUnparsedBits = 0;

    return result;
  }

  unsigned char get1Byte() { // byte-aligned
    ensureValidBytes(1);
    fRemainingUnparsedBits = 0;
    return curBank()[fCurParserIndex++];
  }

  void getBytes(unsigned char* to, unsigned numBytes) {
    ensureValidBytes(numBytes);
    memmove(to, nextToParse(), numBytes);
    fCurParserIndex += numBytes;
    fRemainingUnparsedBits = 0;
  }
  void skipBytes(unsigned numBytes) {
    ensureValidBytes(numBytes);
    fCurParserIndex += numBytes;
  }

  void skipBits(unsigned numBits);
  unsigned getBits(unsigned numBits);
      // numBits <= 32; returns data into low-order bits of result

  unsigned curOffset() const { return fCurParserIndex; }

  unsigned& totNumValidBytes() { return fTotNumValidBytes; }

private:
  unsigned char* curBank() { return fCurBank; }
  unsigned char* nextToParse() { return &curBank()[fCurParserIndex]; }
  unsigned char* lastParsed() { return &curBank()[fCurParserIndex-1]; }

  // makes sure that at least "numBytes" valid bytes remain:
  void ensureValidBytes(unsigned numBytesNeeded) {
    // common case: inlined:
    if (fCurParserIndex + numBytesNeeded <= fTotNumValidBytes) return;

    ensureValidBytes1(numBytesNeeded);
  }
  void ensureValidBytes1(unsigned numBytesNeeded);

  static void afterGettingBytes(void* clientData, unsigned numBytesRead,
				struct timeval presentationTime);

private:
  FramedSource* fInputSource; // should be a byte-stream source??
  FramedSource::onCloseFunc* fOnInputCloseFunc;
  void* fOnInputCloseClientData;
  clientContinueFunc* fClientContinueFunc;
  void* fClientContinueClientData;

  // Use a pair of 'banks', and swap between them as they fill up:
  unsigned char fBank[2][BANK_SIZE];
  unsigned char fCurBankNum;
  unsigned char* fCurBank;

  // The most recent 'saved' parse position:
  unsigned fSavedParserIndex; // <= fCurParserIndex
  unsigned char fSavedRemainingUnparsedBits;

  // The current position of the parser within the current bank:
  unsigned fCurParserIndex; // <= fTotNumValidBytes
  unsigned char fRemainingUnparsedBits; // in previous byte: [0,7]

  // The total number of valid bytes stored in the current bank:
  unsigned fTotNumValidBytes; // <= BANK_SIZE
};

#endif
