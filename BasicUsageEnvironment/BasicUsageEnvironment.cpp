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
// Copyright (c) 1996-2000 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#ifndef IMN_PIM
#include "BasicUsageEnvironment.hh"
#include <iomanip.h>

////////// BasicUsageEnvironment //////////

#if defined(__WIN32__) || defined(_WIN32)
extern "C" int initializeWinsockIfNecessary();
#endif

BasicUsageEnvironment::BasicUsageEnvironment(TaskScheduler& taskScheduler)
: BasicUsageEnvironment0(taskScheduler) {
#if defined(__WIN32__) || defined(_WIN32)
  if (!initializeWinsockIfNecessary()) {
    setResultErrMsg("Failed to initialize 'winsock': ");
    reportBackgroundError();
    exit(1);
  }
#endif
}

BasicUsageEnvironment::~BasicUsageEnvironment() {
}

BasicUsageEnvironment*
BasicUsageEnvironment::createNew(TaskScheduler& taskScheduler) {
  return new BasicUsageEnvironment(taskScheduler);
}

int BasicUsageEnvironment::getErrno() const {
#if defined(__WIN32__) || defined(_WIN32)
  if (errno == 0) {
    errno = WSAGetLastError();
  }
#endif
  return errno;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(char const* str) {
	cerr << str;
	return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(int i) {
	cerr << i;
	return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(unsigned u) {
	cerr << u;
	return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(double d) {
	cerr << d;
	return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(void* p) {
	cerr << hex << p;
	return *this;
}
#endif

