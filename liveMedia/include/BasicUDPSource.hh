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
// A simple UDP source, where every UDP payload is a complete frame
// C++ header

#ifndef _BASIC_UDP_SOURCE_HH
#define _BASIC_UDP_SOURCE_HH

#ifndef _FRAMED_SOURCE_HH
#include "FramedSource.hh"
#endif
#ifndef _GROUPSOCK_HH
#include "Groupsock.hh"
#endif

class BasicUDPSource: public FramedSource {
public:
  static BasicUDPSource* createNew(UsageEnvironment& env, Groupsock* inputGS);

  virtual ~BasicUDPSource();

private:
  BasicUDPSource(UsageEnvironment& env, Groupsock* inputGS);
      // called only by createNew()

  static void incomingPacketHandler(BasicUDPSource* source, int mask);
  void incomingPacketHandler1();

private: // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  Groupsock* fInputGS;
};

#endif
