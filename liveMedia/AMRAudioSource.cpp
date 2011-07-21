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
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2011 Live Networks, Inc.  All rights reserved.
// A source object for AMR audio sources
// Implementation

#include "AMRAudioSource.hh"

AMRAudioSource::AMRAudioSource(UsageEnvironment& env,
			       Boolean isWideband, unsigned numChannels)
  : FramedSource(env),
    fIsWideband(isWideband), fNumChannels(numChannels), fLastFrameHeader(0) {
}

AMRAudioSource::~AMRAudioSource() {
}

char const* AMRAudioSource::MIMEtype() const {
  return "audio/AMR";
}

Boolean AMRAudioSource::isAMRAudioSource() const {
  return True;
}
