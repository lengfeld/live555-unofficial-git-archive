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
// Inclusion of header files representing the interface
// for the entire library
//
// Programs that use the library can include this header file,
// instead of each of the individual media header files

#ifndef _LIVEMEDIA_HH
#define _LIVEMEDIA_HH

#include "MPEGAudioRTPSink.hh"
#include "MP3ADURTPSink.hh"
#include "MPEGVideoRTPSink.hh"
#include "FileSink.hh"
#include "MPEGVideoHTTPSink.hh"
#include "GSMAudioRTPSink.hh"
#include "H263plusVideoRTPSink.hh"
#include "SimpleRTPSink.hh"
#include "ByteStreamMultiFileSource.hh"
#include "BasicUDPSource.hh"
#include "SimpleRTPSource.hh"
#include "MPEGAudioRTPSource.hh"
#include "MPEG4LATMAudioRTPSource.hh"
#include "MPEG4ESVideoRTPSource.hh"
#include "MPEG4GenericRTPSource.hh"
#include "MP3ADURTPSource.hh"
#include "QCELPAudioRTPSource.hh"
#include "JPEGVideoRTPSource.hh"
#include "MPEGVideoRTPSource.hh"
#include "H261VideoRTPSource.hh"
#include "H263plusVideoRTPSource.hh"
#include "MP3HTTPSource.hh"
#include "MP3ADU.hh"
#include "MP3ADUinterleaving.hh"
#include "MP3Transcoder.hh"
#include "MPEGDemuxedElementaryStream.hh"
#include "MPEGAudioStreamFramer.hh"
#include "AC3AudioStreamFramer.hh"
#include "AC3AudioRTPSource.hh"
#include "AC3AudioRTPSink.hh"
#include "MPEGVideoStreamFramer.hh"
#include "DeviceSource.hh"
#include "PrioritizedRTPStreamSelector.hh"
#include "RTSPServer.hh"
#include "RTSPClient.hh"
#include "SIPClient.hh"
#include "QuickTimeFileSink.hh"
#include "QuickTimeGenericRTPSource.hh"
#include "PassiveServerMediaSession.hh"

#endif
