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
// Copyright (c) 1996-2008 Live Networks, Inc.  All rights reserved.
// MP3 HTTP Sources
// C++ header

#ifndef _MP3_HTTP_SOURCE_HH
#define _MP3_HTTP_SOURCE_HH

#ifndef _MP3_FILE_SOURCE_HH
#include "MP3FileSource.hh"
#endif
#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif

class MP3HTTPSource: public MP3FileSource {
public:
  static MP3HTTPSource* createNew(UsageEnvironment& env,
				  NetAddress const& address, Port port,
				  char const* remoteHostName,
				  char const* fileName);

  virtual ~MP3HTTPSource();

private:
  MP3HTTPSource(UsageEnvironment& env, FILE* fid);
      // called only by createNew()

  void writeGetCmd(char const* hostName, unsigned portNum,
		   char const* fileName);
};

#endif
