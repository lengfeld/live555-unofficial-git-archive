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
// "mTunnel" multicast access service
// Copyright (c) 1996-2001 Live Networks, Inc.  All rights reserved.
// Helper routines to implement 'group sockets'
// C++ header

#ifndef _GROUPSOCK_HELPER_HH
#define _GROUPSOCK_HELPER_HH

#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif

#ifndef _USAGE_ENVIRONMENT_HH
#include "UsageEnvironment.hh"
#endif

#if defined(__WIN32__) || defined(_WIN32)
#else
#include <sys/time.h>
#endif

#ifndef SOCKLEN_T
#define SOCKLEN_T int
#endif

int setupDatagramSocket(UsageEnvironment& env,
			Port port, Boolean setLoopback = True);
int setupStreamSocket(UsageEnvironment& env,
		      Port port, Boolean makeNonBlocking = True);

int readSocket(UsageEnvironment& env,
	       int socket, unsigned char* buffer, unsigned bufferSize,
	       struct sockaddr_in& fromAddress,
	       struct timeval* timeout = NULL);

Boolean writeSocket(UsageEnvironment& env,
		    int socket, struct in_addr address, Port port,
		    unsigned char ttlArg,
		    unsigned char* buffer, unsigned bufferSize);

unsigned increaseSendBufferTo(UsageEnvironment& env,
			      int socket, unsigned requestedSize);
unsigned increaseReceiveBufferTo(UsageEnvironment& env,
				 int socket, unsigned requestedSize);

Boolean socketJoinGroup(UsageEnvironment& env, int socket, unsigned groupAddress);
Boolean socketLeaveGroup(UsageEnvironment&, int socket, unsigned groupAddress);

// source-specific multicast join/leave
Boolean socketJoinGroupSSM(UsageEnvironment& env, int socket,
			   unsigned groupAddress, unsigned sourceFilterAddr);
Boolean socketLeaveGroupSSM(UsageEnvironment&, int socket,
			    unsigned groupAddress, unsigned sourceFilterAddr);

Boolean getSourcePort(UsageEnvironment& env, int socket, Port& port);

unsigned ourSourceAddressForMulticast(UsageEnvironment& env); // in network order

// IP addresses of our sending and receiving interfaces.  (By default, these
// are INADDR_ANY (i.e., 0), specifying the default interface.)
extern unsigned SendingInterfaceAddr;
extern unsigned ReceivingInterfaceAddr;

// Returns a simple "hh:mm:ss" string, for use in debugging output (e.g.)
char const* timestampString();

#if defined(__WIN32__) || defined(_WIN32)
// For Windoze, we need to implement our own gettimeofday()
extern int gettimeofday(struct timeval*, int*);
#endif

// The following are implemented in inet.c:
extern "C" unsigned long our_inet_addr(char*);
extern "C" char* our_inet_ntoa(struct in_addr);
extern "C" struct hostent* our_gethostbyname(char* name);
extern "C" void our_bcopy(/*#####const*/ void*, void*, size_t);
extern "C" void our_srandom(int x);
extern "C" long our_random();

#endif
