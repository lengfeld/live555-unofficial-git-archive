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
#include "HandlerSet.hh"
#include <stdio.h>
#if defined(_QNX4)
#include <sys/select.h>
#include <unix.h>
#endif

////////// BasicTaskScheduler //////////

BasicTaskScheduler* BasicTaskScheduler::createNew() {
	return new BasicTaskScheduler();
}

BasicTaskScheduler::BasicTaskScheduler()
  : fMaxNumSockets(0) {
  FD_ZERO(&fReadSet);
}

BasicTaskScheduler::~BasicTaskScheduler() {
}

#ifndef MILLION
#define MILLION 1000000
#endif

void BasicTaskScheduler::SingleStep(unsigned maxDelayTime) {
  fd_set readSet = fReadSet; // make a copy for this select() call
  
  DelayInterval const& timeToDelay = fDelayQueue.timeToNextAlarm();
  struct timeval tv_timeToDelay;
  tv_timeToDelay.tv_sec = timeToDelay.seconds();
  tv_timeToDelay.tv_usec = timeToDelay.useconds();
  // Very large "tv_sec" values cause select() to fail.
  // Don't make it any larger than 1 million seconds (11.5 days)
  const long MAX_TV_SEC = MILLION;
  if (tv_timeToDelay.tv_sec > MAX_TV_SEC) {
    tv_timeToDelay.tv_sec = MAX_TV_SEC;
  }
  // Also check our "maxDelayTime" parameter:
  if (maxDelayTime > 0 &&
      (tv_timeToDelay.tv_sec*MILLION+tv_timeToDelay.tv_usec) > (long)maxDelayTime) {
    tv_timeToDelay.tv_sec = maxDelayTime/MILLION;
    tv_timeToDelay.tv_usec = maxDelayTime%MILLION;
  }
  
  int selectResult = select(fMaxNumSockets, &readSet, NULL, NULL,
			    &tv_timeToDelay);
  if (selectResult < 0) {
#if defined(__WIN32__) || defined(_WIN32)
    int err = WSAGetLastError();
    // For some unknown reason, select() in Windoze sometimes fails with WSAEINVAL if
    // it was called with no entries set in "readSet".  If this happens, ignore it:
    if (err == WSAEINVAL && readSet.fd_count == 0) {
      err = 0;
	  // To stop this from happening again, create a dummy readable socket:
	  int dummySocketNum = socket(AF_INET, SOCK_DGRAM, 0);
	  FD_SET((unsigned)dummySocketNum, &fReadSet);
    }
    if (err != 0)
#endif
      {
	// Unexpected error - treat this as fatal:
	perror("BasicTaskScheduler::SingleStep(): select() fails");
	exit(0);
      }
  }
  
  // Handle any delayed event that may have come due:
  fDelayQueue.handleAlarm();
  
  // Call the handler function for each readable socket:
  HandlerIterator iter(*fReadHandlers);
  HandlerDescriptor* handler;
  while ((handler = iter.next()) != NULL) {
    if (FD_ISSET(handler->socketNum, &readSet) &&
	FD_ISSET(handler->socketNum, &fReadSet) /* sanity check */ &&
	handler->handlerProc != NULL) {
      (*handler->handlerProc)(handler->clientData, SOCKET_READABLE);
    }
  }
}

void BasicTaskScheduler::turnOnBackgroundReadHandling(int socketNum,
				BackgroundHandlerProc* handlerProc,
				void* clientData) {
  if (socketNum < 0) return;
  FD_SET((unsigned)socketNum, &fReadSet);
  fReadHandlers->assignHandler(socketNum, handlerProc, clientData);

  if (socketNum+1 > fMaxNumSockets) {
    fMaxNumSockets = socketNum+1;
  }
}

void BasicTaskScheduler::turnOffBackgroundReadHandling(int socketNum) {
  if (socketNum < 0) return;
  FD_CLR((unsigned)socketNum, &fReadSet);
  fReadHandlers->removeHandler(socketNum);

  if (socketNum+1 == fMaxNumSockets) {
    --fMaxNumSockets;
  }
}
#endif

