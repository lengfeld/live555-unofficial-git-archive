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
// A generic RTSP client
// Implementation

#include "RTSPClient.hh"
#include "GroupsockHelper.hh"
#include "our_md5.h"

#if defined(__WIN32__) || defined(_WIN32)
#define _close closesocket
#define _strncasecmp strncmp
#define snprintf _snprintf
#else
#define _close close
#if defined(_QNX4)
#define _strncasecmp strncmp
#else
#define _strncasecmp strncasecmp
#endif
#endif

////////// RTSPClient //////////

RTSPClient* RTSPClient::createNew(UsageEnvironment& env,
				  int verbosityLevel,
				  char const* applicationName) {
  return new RTSPClient(env, verbosityLevel, applicationName);
}

Boolean RTSPClient::lookupByName(UsageEnvironment& env,
				 char const* instanceName,
				 RTSPClient*& resultClient) {
  resultClient = NULL; // unless we succeed

  Medium* medium;
  if (!Medium::lookupByName(env, instanceName, medium)) return False;

  if (!medium->isRTSPClient()) {
    env.setResultMsg(instanceName, " is not a RTSP client");
    return False;
  }

  resultClient = (RTSPClient*)medium;
  return True;
}

RTSPClient::RTSPClient(UsageEnvironment& env,
		       int verbosityLevel, char const* applicationName)
  : Medium(env),
    fVerbosityLevel(verbosityLevel), fSocketNum(-1), fServerAddress(0),
    fCSeq(0), fBaseURL(NULL), fCurrentAuthenticator(NULL),
    fTCPStreamIdCount(0), fLastSessionId(NULL) {
  // Set the "User-Agent:" header to use in each request:
  char const* const libName = "LIVE.COM Streaming Media v";
  char const* const libVersionStr = LIVEMEDIA_LIBRARY_VERSION_STRING;
  char const* libPrefix; char const* libSuffix;
  if (applicationName == NULL || applicationName[0] == '\0') {
    applicationName = libPrefix = libSuffix = "";
  } else {
    libPrefix = " (";
    libSuffix = ")";
  }
  char const* const formatStr = "User-Agent: %s%s%s%s%s\r\n";
  unsigned headerSize
    = strlen(formatStr) + strlen(applicationName) + strlen(libPrefix)
    + strlen(libName) + strlen(libVersionStr) + strlen(libSuffix);
  fUserAgentHeaderStr = new char[headerSize];
  sprintf(fUserAgentHeaderStr, formatStr,
	  applicationName, libPrefix, libName, libVersionStr, libSuffix);
}

RTSPClient::~RTSPClient() {
  reset();
  delete fUserAgentHeaderStr;
}

Boolean RTSPClient::isRTSPClient() const {
  return True;
}

void RTSPClient::reset() {
  if (fSocketNum >= 0) {
    ::_close(fSocketNum);
  }
  fSocketNum = -1;
  fServerAddress = 0;

  delete fBaseURL; fBaseURL = NULL;

  resetCurrentAuthenticator();

  delete fLastSessionId; fLastSessionId = NULL;
}

static char* getLine(char* startOfLine) {
  // returns the start of the next line, or NULL if none
  for (char* ptr = startOfLine; *ptr != '\0'; ++ptr) {
    if (*ptr == '\r' || *ptr == '\n') {
      // We found the end of the line
      *ptr++ = '\0';
      if (*ptr == '\n') ++ptr;
      return ptr;
    }
  }

  return NULL;
}

char* RTSPClient::describeURL(char const* url, AuthRecord* authenticator) {
  do {  
    if (!openConnectionFromURL(url)) break;

    // Send the DESCRIBE command:

    // First, construct an authenticator string:
    resetCurrentAuthenticator();
    char* authenticatorStr
      = createAuthenticatorString(authenticator, "DESCRIBE", url);

    // (Later implement more, as specified in the RTSP spec, sec D.1 #####)
    char* const cmdFmt = "DESCRIBE %s RTSP/1.0\r\nCSeq: %d\r\nAccept: application/sdp\r\n%s%s\r\n";

    unsigned const writeBufSize = 1000;
    char writeBuf[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "url" can come from an external user
    sprintf(writeBuf,
	    cmdFmt, url, ++fCSeq, authenticatorStr, fUserAgentHeaderStr);
#else
    snprintf(writeBuf, writeBufSize,
	     cmdFmt, url, ++fCSeq, authenticatorStr, fUserAgentHeaderStr);
#endif
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("DESCRIBE send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 20000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received DESCRIBE response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code that
    // we can handle.
    Boolean wantRedirection = False;
    char* redirectionURL = NULL;
    char* firstLine = readBuf;
    char* nextLineStart = getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode == 301 || responseCode == 302) {
      wantRedirection = True;
      redirectionURL = new char[readBufSize]; // ensures enough space
    } else if (responseCode != 200) {
      if (responseCode == 401 && authenticator != NULL) {
	// We have an authentication failure, so fill in "authenticator"
	// using the contents of a following "WWW-Authenticate:" line.
	// (Once we compute a 'response' for "authenticator", it can be
	//  used in a subsequent request - that will hopefully succeed.)
	char* lineStart;
	while (1) {
	  lineStart = nextLineStart;
	  if (lineStart == NULL) break;

	  nextLineStart = getLine(lineStart);
	  if (lineStart[0] == '\0') break; // this is a blank line

	  char* realm = strdup(lineStart); char* nonce = strdup(lineStart);
	  if (sscanf(lineStart, "WWW-Authenticate: Digest realm=\"%[^\"]\", nonce=\"%[^\"]\"",
		     realm, nonce) == 2) {
	    authenticator->realm = realm;
	    authenticator->nonce = nonce;
	    break;
	  } else {
	    delete realm; delete nonce;
	  }
	} 
      }
      envir().setResultMsg("cannot handle DESCRIBE response: ", firstLine);
      break;
    }

    // Skip every subsequent header line, until we see a blank line
    // The remaining data is assumed to be the SDP descriptor that we want.
    // We should really do some checking on the headers here - e.g., to
    // check for "Content-type: application/sdp", "Content-base",
    // "Content-location", "CSeq", etc. #####
    int contentLength = -1;
    char* lineStart;
    while (1) {
      lineStart = nextLineStart;
      if (lineStart == NULL) break;

      nextLineStart = getLine(lineStart);
      if (lineStart[0] == '\0') break; // this is a blank line

      if (sscanf(lineStart, "Content-Length: %d", &contentLength) == 1
	  || sscanf(lineStart, "Content-length: %d", &contentLength) == 1) {
	if (contentLength < 0) {
	  envir().setResultMsg("Bad \"Content-length:\" header: \"",
			       lineStart, "\"");
	  break;
	}
      } else if (wantRedirection) { 
	if (sscanf(lineStart, "Location: %s", redirectionURL) == 1) {
	  // Try again with this URL
	  if (fVerbosityLevel >= 1) {
	    fprintf(stderr, "Redirecting to the new URL \"%s\"\n",
		    redirectionURL); fflush(stderr);
	  }
	  reset();
	  char* result = describeURL(redirectionURL);
	  delete redirectionURL;
	  return result;
	}
      }
    } 

    // We're now at the end of the response header lines
    if (wantRedirection) {
      envir().setResultMsg("Saw redirection response code, but not a \"Location:\" header");
      delete redirectionURL;
      break;
    }
    if (lineStart == NULL) {
      envir().setResultMsg("no content following header lines: ", readBuf);
      break;
    }

    // Use the remaining data as the SDP descr, but first, check
    // the "Content-length:" header (if any) that we saw.  We may need to
    // read more data, or we may have extraneous data in the buffer.
    char* bodyStart = nextLineStart;
    if (contentLength >= 0) {
      // We saw a "Content-length:" header
      unsigned numBodyBytes = &readBuf[bytesRead] - bodyStart;
      if (contentLength > (int)numBodyBytes) {
	// We need to read more data.  First, make sure we have enough
	// space for it:
	unsigned numExtraBytesNeeded = contentLength - numBodyBytes;
	unsigned remainingBufferSize = readBufSize - bytesRead;
	if (numExtraBytesNeeded > remainingBufferSize) {
	  char tmpBuf[200];
	  sprintf(tmpBuf, "Read buffer size (%d) is too small for \"Content-length:\" %d (need a buffer size of >= %d bytes\n",
		  readBufSize, contentLength,
		  readBufSize + numExtraBytesNeeded - remainingBufferSize);
	  envir().setResultMsg(tmpBuf);
	  break;
	}

	// Keep reading more data until we have enough:
	if (fVerbosityLevel >= 1) {
	  fprintf(stderr, "Need to read %d extra bytes\n",
		  numExtraBytesNeeded); fflush(stderr);
	}
	while (numExtraBytesNeeded > 0) {
	  struct sockaddr_in fromAddress;
	  char* ptr = &readBuf[bytesRead];
	  int bytesRead2 = readSocket(envir(), fSocketNum, (unsigned char*)ptr,
				      numExtraBytesNeeded, fromAddress);
	  if (bytesRead2 < 0) break;
	  ptr[bytesRead2] = '\0';
	  if (fVerbosityLevel >= 1) {
	    fprintf(stderr, "Read %d extra bytes: %s\n", bytesRead2, ptr); fflush(stderr);
	  }

	  bytesRead += bytesRead2;
	  numExtraBytesNeeded -= bytesRead2;
	}
	if (numExtraBytesNeeded > 0) break; // one of the reads failed
      }

      bodyStart[contentLength] = '\0'; // trims any extra data
    }

    return strdup(bodyStart);
  } while (0);

  return NULL;
}

char* RTSPClient
::describeWithPassword(char const* url,
		       char const* username, char const* password) {
  AuthRecord authenticator;
  authenticator.realm = authenticator.nonce = NULL;
  authenticator.username = username; authenticator.password = password;
  char* describeResult = describeURL(url, &authenticator);
  if (describeResult != NULL) {
    // We are already authorized
    return describeResult;
  }

  // The "realm" and "nonce" fields should have been filled in:
  if (authenticator.realm == NULL || authenticator.nonce == NULL) {
    // We haven't been given enough information to try again, so fail:
    return NULL;
  }

  // Try again:
  describeResult = describeURL(url, &authenticator);
  if (describeResult != NULL) {
    // The authenticator worked, so use it in future requests:
    useAuthenticator(&authenticator);
  }

  // The "realm" and "nonce" fields were dynamically
  // allocated; free them now:
  delete (char*)authenticator.realm;
  delete (char*)authenticator.nonce;

  return describeResult;
}

Boolean RTSPClient::sendOptionsCmd() {
  do {
    // Send the OPTIONS command:
    char* const cmdFmt = "OPTIONS * RTSP/1.0\r\nCSeq: %d\r\n%s\r\n";
    unsigned const writeBufSize = 100;
    char writeBuf[writeBufSize];
    sprintf(writeBuf, cmdFmt, ++fCSeq, fUserAgentHeaderStr);

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("OPTIONS send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received OPTIONS response: %s\n", readBuf); fflush(stderr);
    }

    // For now, don't bother looking at the response #####

    return True;
  } while (0);

  return False;
}

static Boolean isAbsoluteURL(char const* url) {
  // Assumption: "url" is absolute if it contains a ':', before any
  // occurrence of '/'
  while (*url != '\0' && *url != '/') {
    if (*url == ':') return True;
    ++url;
  }

  return False;
}

void RTSPClient::constructSubsessionURL(MediaSubsession const& subsession,
					char const*& prefix,
					char const*& separator,
					char const*& suffix) {
  // Figure out what the URL describing "subsession" will look like.
  // The URL is returned in three parts: prefix; separator; suffix
  prefix = fBaseURL;
  if (prefix == NULL) prefix = "";

  suffix = subsession.controlPath();
  if (suffix == NULL) suffix = "";

  if (isAbsoluteURL(suffix)) {
    prefix = separator = "";
  } else {
    separator = (suffix[0] == '/') ? "" : "/";
  }
}

Boolean RTSPClient::announceSDPDescription(char const* url,
					   char const* sdpDescription,
					   AuthRecord* authenticator) {
  do {
    if (!openConnectionFromURL(url)) break;

    // Send the ANNOUNCE command:

    // First, construct an authenticator string:
    resetCurrentAuthenticator();
    char* authenticatorStr
      = createAuthenticatorString(authenticator, "ANNOUNCE", url);

    char* const cmdFmt = "ANNOUNCE %s RTSP/1.0\r\nCSeq: %d\r\nContent-Type: application/sdp\r\n%sContent-length: %d\r\n\r\n%s";
	    // Note: QTSS hangs if an "ANNOUNCE" contains a "User-Agent:" field (go figure), so don't include one here
    unsigned sdpSize = strlen(sdpDescription);
    unsigned writeBufSize
      = strlen(cmdFmt) + strlen(url) + strlen(authenticatorStr) + sdpSize;
    char* writeBuf = new char[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "sdpDescriptor"
    // can come from an external user
    sprintf(writeBuf, cmdFmt,
	    url, ++fCSeq, authenticatorStr, sdpSize,
	    sdpDescription);
#else
    snprintf(writeBuf, writeBufSize, cmdFmt,
	     url, ++fCSeq, authenticatorStr, sdpSize,
	     sdpDescription);
#endif
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("ANNOUNCE send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received ANNOUNCE response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code 200
    char* firstLine = readBuf;
    char* nextLineStart = getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode != 200) {
      if (responseCode == 401 && authenticator != NULL) {
	// We have an authentication failure, so fill in "authenticator"
	// using the contents of a following "WWW-Authenticate:" line.
	// (Once we compute a 'response' for "authenticator", it can be
	//  used in a subsequent request - that will hopefully succeed.)
	char* lineStart;
	while (1) {
	  lineStart = nextLineStart;
	  if (lineStart == NULL) break;

	  nextLineStart = getLine(lineStart);
	  if (lineStart[0] == '\0') break; // this is a blank line

	  char* realm = strdup(lineStart); char* nonce = strdup(lineStart);
	  if (sscanf(lineStart, "WWW-Authenticate: Digest realm=\"%[^\"]\", nonce=\"%[^\"]\"",
		     realm, nonce) == 2) {
	    authenticator->realm = realm;
	    authenticator->nonce = nonce;
	    break;
	  } else {
	    delete realm; delete nonce;
	  }
	} 
      }
      envir().setResultMsg("cannot handle ANNOUNCE response: ", firstLine);
      break;
    }
    // (Later, check "CSeq" too #####)

    return True;
  } while (0);

  return False;
}

static char* computeDigestResponse(AuthRecord const& authenticator,
				   char const* cmd, char const* url) {
  // The "response" field is computed as:
  //    md5(md5(<username>:<realm>:<password>):<nonce>:md5(<cmd>:<url>))
  unsigned const ha1DataLen = strlen(authenticator.username) + 1
    + strlen(authenticator.realm) + 1 + strlen(authenticator.password);
  unsigned char* ha1Data = new unsigned char[ha1DataLen+1];
  sprintf((char*)ha1Data, "%s:%s:%s",
	  authenticator.username, authenticator.realm,
	  authenticator.password);
  char ha1Buf[33];
  our_MD5Data(ha1Data, ha1DataLen, ha1Buf);

  unsigned const ha2DataLen = strlen(cmd) + 1 + strlen(url);
  unsigned char* ha2Data = new unsigned char[ha2DataLen+1];
  sprintf((char*)ha2Data, "%s:%s", cmd, url);
  char ha2Buf[33];
  our_MD5Data(ha2Data, ha2DataLen, ha2Buf);

  unsigned const digestDataLen
    = 32 + 1 + strlen(authenticator.nonce) + 1 + 32;
  unsigned char* digestData = new unsigned char[digestDataLen+1];
  sprintf((char*)digestData, "%s:%s:%s",
	  ha1Buf, authenticator.nonce, ha2Buf);
  return our_MD5Data(digestData, digestDataLen, NULL);
}

Boolean RTSPClient
::announceWithPassword(char const* url, char const* sdpDescription,
		       char const* username, char const* password) {
  AuthRecord authenticator;
  authenticator.realm = authenticator.nonce = NULL;
  authenticator.username = username; authenticator.password = password;
  if (announceSDPDescription(url, sdpDescription, &authenticator)) {
    // We are already authorized
    return True;
  }

  // The "realm" and "nonce" fields should have been filled in:
  if (authenticator.realm == NULL || authenticator.nonce == NULL) {
    // We haven't been given enough information to try again, so fail:
    return False;
  }

  // Try again:
  Boolean secondTrySuccess
    = announceSDPDescription(url, sdpDescription, &authenticator);

  if (secondTrySuccess) {
    // The authenticator worked, so use it in future requests:
    useAuthenticator(&authenticator);
  }

  // The "realm" and "nonce" fields were dynamically
  // allocated; free them now:
  delete (char*)authenticator.realm;
  delete (char*)authenticator.nonce;

  return secondTrySuccess;
}

Boolean RTSPClient::setupMediaSubsession(MediaSubsession& subsession,
					 Boolean streamOutgoing,
					 Boolean streamUsingTCP) {
  do {
    // Construct the SETUP command:

    // First, construct an authenticator string:
    char* authenticatorStr
      = createAuthenticatorString(fCurrentAuthenticator,
				  "SETUP", fBaseURL);

    // We normally don't include a "Session:" header in the "SETUP"
    // request.  However, when sending more than one "SETUP" request
    // with an authenticator, DSS wants the 2nd and later "SETUP"s
    // to include a "Session:" header.  This is probably nonstandard,
    // but it's par for the course for DSS - sigh...
    char* bogusSessionStr;
    if (authenticatorStr[0] != '\0' && fLastSessionId != NULL) {
      bogusSessionStr = new char[20+strlen(fLastSessionId)];
      sprintf(bogusSessionStr, "Session: %s\r\n", fLastSessionId);
    } else {
      bogusSessionStr = "";
    }

    // (Later implement more, as specified in the RTSP spec, sec D.1 #####)
    char* const cmdFmt = "SETUP %s%s%s RTSP/1.0\r\nCSeq: %d\r\nTransport: RTP/AVP%s%s%s=%d-%d\r\n%s%s%s\r\n";

    char const *prefix, *separator, *suffix;
    constructSubsessionURL(subsession, prefix, separator, suffix);

    char const* transportTypeString;
    char const* modeString = streamOutgoing ? ";mode=receive" : "";
       // Note: I think the above is nonstandard, but DSS wants it this way
    char const* portTypeString;
    unsigned short rtpNumber, rtcpNumber;
    if (streamUsingTCP) { // streaming over the RTSP connection
      transportTypeString = "/TCP;unicast";
      portTypeString = ";interleaved";
      rtpNumber = fTCPStreamIdCount++;
      rtcpNumber = fTCPStreamIdCount++;
    } else { // normal RTP streaming      
      unsigned connectionAddress = subsession.connectionEndpointAddress();
      transportTypeString
	= IsMulticastAddress(connectionAddress)	? ";multicast" : ";unicast";
      portTypeString = ";client_port";
      rtpNumber = subsession.clientPortNum();
      if (rtpNumber == 0) {
	envir().setResultMsg("Client port number unknown\n");
	break;
      }
      rtcpNumber = rtpNumber + 1;
    }
    unsigned const writeBufSize = 1000;
    char writeBuf[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "prefix" or "suffix" can come from
    // an external user
    sprintf(writeBuf, cmdFmt, prefix, separator, suffix,
	    ++fCSeq, transportTypeString, modeString, portTypeString,
	    rtpNumber, rtcpNumber, bogusSessionStr, authenticatorStr,
	    fUserAgentHeaderStr);
#else
    snprintf(writeBuf, writeBufSize, cmdFmt, prefix, separator, suffix,
	     ++fCSeq, transportTypeString, modeString, portTypeString,
	     rtpNumber, rtcpNumber, bogusSessionStr, authenticatorStr,
	     fUserAgentHeaderStr);
#endif
    delete authenticatorStr;
    if (bogusSessionStr[0] != '\0') delete bogusSessionStr;

    // And then sent it:
    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("SETUP send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received SETUP response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code 200
    char* firstLine = readBuf;
    char* nextLineStart = getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode != 200) {
      envir().setResultMsg("cannot handle SETUP response: ", firstLine);
      break;
    }

    // Look for a "Session:" header (to set our session id), and
    // a "Transport: " header (to set the server address/port)
    // For now, ignore other headers.  (Later, check for "CSeq" also #####)
    char* lineStart;
    char* sessionId = new char[readBufSize]; // ensures we have enough space
    while (1) {
      lineStart = nextLineStart;
      if (lineStart == NULL) break;

      nextLineStart = getLine(lineStart);

      if (sscanf(lineStart, "Session: %s", sessionId) == 1) {
	subsession.sessionId = strdup(sessionId);
	delete fLastSessionId; fLastSessionId = strdup(sessionId);
	continue;
      }

      char* serverAddressStr;
      unsigned short serverPortNum;
      unsigned char rtpChannelId, rtcpChannelId;
      if (parseTransportResponse(lineStart,
				 serverAddressStr, serverPortNum,
				 rtpChannelId, rtcpChannelId)) {
	delete subsession.connectionEndpointName();
	subsession.connectionEndpointName() = serverAddressStr;
	subsession.serverPortNum = serverPortNum;
	subsession.rtpChannelId = rtpChannelId;
	subsession.rtcpChannelId = rtcpChannelId;
	continue;
      }
    } 
    delete sessionId;

    if (subsession.sessionId == NULL) {
      envir().setResultMsg("\"Session:\" header is missing in the response");
      break;
    }

    if (streamUsingTCP) {
      // Tell the subsession to receive RTP (and send/receive RTCP)
      // over the RTSP stream:
      subsession.rtpSource()->setStreamSocket(fSocketNum,
					      subsession.rtpChannelId);
      subsession.rtcpInstance()->setStreamSocket(fSocketNum,
						 subsession.rtcpChannelId);
    } else {
      // Normal case.
      // Set the RTP and RTCP sockets' destination address and port
      // from the information in the SETUP response: 
      subsession.setDestinations(fServerAddress);
    }

    return True;
  } while (0);

  return False;
}

Boolean RTSPClient::playMediaSubsession(MediaSubsession& subsession,
					Boolean hackForDSS) {
  do {
    // First, make sure that we have a RTSP session in progress
    if (subsession.sessionId == NULL) {
      envir().setResultMsg("No RTSP session is currently in progress\n");
      break;
    }

    // Send the PLAY command:

    // First, construct an authenticator string:
    char* authenticatorStr
      = createAuthenticatorString(fCurrentAuthenticator, "PLAY", fBaseURL);

    char* const cmdFmt = "PLAY %s%s%s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nRange: npt=0-\r\n%s%s\r\n";

    char const *prefix, *separator, *suffix;
    constructSubsessionURL(subsession, prefix, separator, suffix);
    if (hackForDSS) {
      // When "PLAY" is used to inject RTP packets into a DSS
      // (violating the RTSP spec, btw; "RECORD" should have been used)
      // the DSS can crash (or hang) if the '/trackid=...' portion of
      // the URL is present.
      separator = suffix = "";
    }

    unsigned const writeBufSize = 1000;
    char writeBuf[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "prefix" or "suffix" can come from an
    // external user
    sprintf(writeBuf, cmdFmt, prefix, separator, suffix,
	    ++fCSeq, subsession.sessionId, authenticatorStr,
	    fUserAgentHeaderStr);
#else
    snprintf(writeBuf, writeBufSize, cmdFmt, prefix, separator, suffix,
	     ++fCSeq, subsession.sessionId, authenticatorStr,
	     fUserAgentHeaderStr);
#endif
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("PLAY send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received PLAY response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code 200
    char* firstLine = readBuf;
    /*char* nextLineStart =*/ getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode != 200) {
      envir().setResultMsg("cannot handle PLAY response: ", firstLine);
      break;
    }
    // (Later, check "CSeq" too #####)

    return True;
  } while (0);

  return False;
}

Boolean RTSPClient::recordMediaSubsession(MediaSubsession& subsession) {
  do {
    // First, make sure that we have a RTSP session in progress
    if (subsession.sessionId == NULL) {
      envir().setResultMsg("No RTSP session is currently in progress\n");
      break;
    }

    // Send the RECORD command:

    // First, construct an authenticator string:
    char* authenticatorStr
      = createAuthenticatorString(fCurrentAuthenticator,
				  "RECORD", fBaseURL);

    char* const cmdFmt = "RECORD %s%s%s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\nRange: npt=0-\r\n%s%s\r\n";

    char const *prefix, *separator, *suffix;
    constructSubsessionURL(subsession, prefix, separator, suffix);

    unsigned const writeBufSize = 1000;
    char writeBuf[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "prefix" or "suffix" can come from an
    // external user
    sprintf(writeBuf, cmdFmt, prefix, separator, suffix,
	    ++fCSeq, subsession.sessionId, authenticatorStr,
	    fUserAgentHeaderStr);
#else
    snprintf(writeBuf, writeBufSize, cmdFmt, prefix, separator, suffix,
	     ++fCSeq, subsession.sessionId, authenticatorStr,
	     fUserAgentHeaderStr);
#endif
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("RECORD send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received RECORD response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code 200
    char* firstLine = readBuf;
    /*char* nextLineStart =*/ getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode != 200) {
      envir().setResultMsg("cannot handle RECORD response: ", firstLine);
      break;
    }
    // (Later, check "CSeq" too #####)

    return True;
  } while (0);

  return False;
}

Boolean RTSPClient::teardownMediaSubsession(MediaSubsession& subsession) {
  do {
    // First, make sure that we have a RTSP session in progreee
    if (subsession.sessionId == NULL) {
      envir().setResultMsg("No RTSP session is currently in progress\n");
      break;
    }

    // Send the TEARDOWN command:

    // First, construct an authenticator string:
    char* authenticatorStr
      = createAuthenticatorString(fCurrentAuthenticator,
				  "TEARDOWN", fBaseURL);

    char* const cmdFmt = "TEARDOWN %s%s%s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\n%s%s\r\n";

    char const *prefix, *separator, *suffix;
    constructSubsessionURL(subsession, prefix, separator, suffix);

    unsigned const writeBufSize = 1000;
    char writeBuf[writeBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if "prefix" or "suffix" can come from an
    // external user
    sprintf(writeBuf, cmdFmt, prefix, separator, suffix,
	    ++fCSeq, subsession.sessionId, authenticatorStr,
	    fUserAgentHeaderStr);
#else
    snprintf(writeBuf, writeBufSize, cmdFmt, prefix, separator, suffix,
	     ++fCSeq, subsession.sessionId, authenticatorStr,
	     fUserAgentHeaderStr);
#endif
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("TEARDOWN send() failed: ");
      break;
    }

  // Don't bother getting the response to the TEARDOWN.
  // (The Darwin server appears to hang after the first subsession TEARDOWN)
#ifdef GET_TEARDOWN_RESPONSE
    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    int bytesRead = getResponse(readBuf, readBufSize);
    if (bytesRead < 0) break;
    if (fVerbosityLevel >= 1) {
      fprintf(stderr, "Received TEARDOWN response: %s\n", readBuf); fflush(stderr);
    }

    // Inspect the first line to check whether it's a result code 200
    char* firstLine = readBuf;
    /*char* nextLineStart =*/ getLine(firstLine);
    unsigned responseCode;
    if (!parseResponseCode(firstLine, responseCode)) break;
    if (responseCode != 200) {
      envir().setResultMsg("cannot handle TEARDOWN response: ", firstLine);
      break;
    }
    // (Later, check "CSeq" too #####)
#endif

    delete (char*)subsession.sessionId;
    subsession.sessionId = NULL;
    // we're done with this session

    return True;
  } while (0);

  return False;
}

Boolean RTSPClient::openConnectionFromURL(char const* url) {
  do {
    // Set this as our base URL:
    delete fBaseURL; fBaseURL = strdup(url); if (fBaseURL == NULL) break;

    // Begin by parsing the URL:
    NetAddress destAddress;
    unsigned short destPortNum;
    if (!parseURL(url, destAddress, destPortNum)) break;
    
    if (fSocketNum < 0) {
      // We don't yet have a TCP socket.  Set one up (blocking) now:
      fSocketNum = setupStreamSocket(envir(), 0, False /* =>blocking */);
      if (fSocketNum < 0) break;
    
      // Connect to the remote endpoint:
      struct sockaddr_in remoteName;
      remoteName.sin_family = AF_INET;
      remoteName.sin_port = htons(destPortNum);
      remoteName.sin_addr.s_addr = fServerAddress
	= *(unsigned*)(destAddress.data());
      if (connect(fSocketNum, (struct sockaddr*)&remoteName, sizeof remoteName)
	  != 0) {
	envir().setResultErrMsg("connect() failed: ");
	break;
      }
    }

    return True; 
  } while (0);

  return False;
}

Boolean RTSPClient::parseURL(char const* url,
			     NetAddress& address, unsigned short& portNum) {
  do {
    // Parse the URL as "rtsp://<address>:<port>/<etc>"
    // (with ":<port>" and "/<etc>" optional)
    char const* prefix = "rtsp://";
    unsigned const prefixLength = 7;
    if (_strncasecmp(url, prefix, prefixLength) != 0) {
      envir().setResultMsg("URL is not of the form \"", prefix, "\"");
      break;
    }

    unsigned const parseBufferSize = 100;
    char parseBuffer[parseBufferSize];
    char const* from = &url[prefixLength];
    char* to = &parseBuffer[0];
    unsigned i;
    for (i = 0; i < parseBufferSize; ++i) {
      if (*from == '\0' || *from == ':' || *from == '/') {
	// We've completed parsing the address
	*to = '\0';
	break;
      }
      *to++ = *from++;
    }
    if (i == parseBufferSize) {
      envir().setResultMsg("URL is too long");
      break;
    }

    NetAddressList addresses(parseBuffer);
    if (addresses.numAddresses() == 0) {
      envir().setResultMsg("Failed to find network address for \"",
			   parseBuffer, "\"");
      break;
    }
    address = *(addresses.firstAddress());

    portNum = 554; // default value
    char nextChar = *from;
    if (nextChar == ':') {
      int portNumInt;
      if (sscanf(++from, "%d", &portNumInt) != 1) {
	envir().setResultMsg("No port number follows ':'");
	break;
      }
      if (portNumInt < 1 || portNumInt > 65535) {
	envir().setResultMsg("Bad port number");
	break;
      }
      portNum = (unsigned short)portNumInt;
    }

    return True;
  } while (0);

  return False;
}

char*
RTSPClient::createAuthenticatorString(AuthRecord const* authenticator,
				      char const* cmd, char const* url) {
  if (authenticator != NULL && authenticator->realm != NULL
      && authenticator->nonce != NULL && authenticator->username != NULL
      && authenticator->password != NULL) {
    // We've been provided a filled-in authenticator, so use it:
    char* const authFmt = "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"\r\n";
    char const* response = computeDigestResponse(*authenticator, cmd, url);
    unsigned authBufSize = strlen(authFmt)
      + strlen(authenticator->username) + strlen(authenticator->realm)
      + strlen(authenticator->nonce) + strlen(url) + strlen(response);
    char* authenticatorStr = new char[authBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if the component strings
    // can come from an external user
    sprintf(authenticatorStr, authFmt,
	    authenticator->username, authenticator->realm,
	    authenticator->nonce, url, response);
#else
    snprintf(authenticatorStr, authBufSize, authFmt,
	     authenticator->username, authenticator->realm,
	     authenticator->nonce, url, response);
#endif
    delete (char*)response;

    return authenticatorStr;
  }

  return strdup("");
}

void RTSPClient::useAuthenticator(AuthRecord const* authenticator) {
  resetCurrentAuthenticator();
  if (authenticator != NULL && authenticator->realm != NULL
      && authenticator->nonce != NULL && authenticator->username != NULL
      && authenticator->password != NULL) {
    fCurrentAuthenticator = new AuthRecord;
    fCurrentAuthenticator->realm = strdup(authenticator->realm);
    fCurrentAuthenticator->nonce = strdup(authenticator->nonce);
    fCurrentAuthenticator->username = strdup(authenticator->username);
    fCurrentAuthenticator->password = strdup(authenticator->password);
  }
}

void RTSPClient::resetCurrentAuthenticator() {
  if (fCurrentAuthenticator == NULL) return;

  delete (char*)fCurrentAuthenticator->realm;
  delete (char*)fCurrentAuthenticator->nonce;
  delete (char*)fCurrentAuthenticator->username;
  delete (char*)fCurrentAuthenticator->password;

  delete fCurrentAuthenticator; fCurrentAuthenticator = NULL;
}

Boolean RTSPClient::sendRequest(char const* requestString) {
  if (fVerbosityLevel >= 1) {
    fprintf(stderr, "Sending request: %s\n", requestString); fflush(stderr);
  }
  return send(fSocketNum, requestString, strlen(requestString), 0) >= 0;
}

int RTSPClient::getResponse(char* responseBuffer,
			    unsigned responseBufferSize) {
  struct sockaddr_in fromAddress;
  
  if (responseBufferSize == 0) return 0; // just in case...
  responseBuffer[0] = '\0'; // ditto

  // Begin by reading and checking the first byte of the response.
  // If it's '$', then there's an interleaved RTP (or RTCP)-over-TCP
  // packet here.  We need to read and discard it first.
  Boolean success = False;
  while (1) {
    unsigned char firstByte;
    if (readSocket(envir(), fSocketNum, &firstByte, 1, fromAddress)
	!= 1) break;
    if (firstByte != '$') {
      // Normal case: This is the start of a regular response; use it:
      responseBuffer[0] = firstByte;
      success = True;
      break;
    } else {
      // This is an interleaved packet; read and discard it:
      unsigned char streamChannelId;
      if (readSocket(envir(), fSocketNum, &streamChannelId, 1, fromAddress)
	  != 1) break;

      unsigned short size;
      if (readSocket(envir(), fSocketNum, (unsigned char*)&size, 2,
		     fromAddress) != 2) break;
      size = ntohs(size);
      if (fVerbosityLevel >= 1) {
	fprintf(stderr, "Discarding interleaved RTP or RTCP packet (%d bytes, channel id %d)\n",
		size, streamChannelId);
      }

      unsigned char* tmpBuffer = new unsigned char[size];
      if (tmpBuffer == NULL) break;
      unsigned bytesRead = 0;
      unsigned bytesToRead = size;
      unsigned curBytesRead;
      while ((curBytesRead = readSocket(envir(), fSocketNum,
					&tmpBuffer[bytesRead], bytesToRead,
					fromAddress)) > 0) {
	bytesRead += curBytesRead;
	if (bytesRead >= size) break;
	bytesToRead -= curBytesRead;
      }
      delete tmpBuffer;
      if (bytesRead != size) break;

      success = True;
    }
  }
  if (!success) return 0;
    
  // Keep reading data from the socket until we see "\r\n\r\n"
  // (or until we fill up our buffer).  Don't read any more than this.
  int bytesRead = 1; // because we've already read the first byte
  while (1) {
    bytesRead += readSocket(envir(), fSocketNum,
			    (unsigned char*)(responseBuffer+bytesRead),
			    1, fromAddress);
    if (bytesRead == 0) {
      envir().setResultMsg("RTSP response was truncated");
      break;
    }
    if (bytesRead >= (int)responseBufferSize) return responseBufferSize;
    
    // Check whether we have "\r\n\r\n":
    char* lastToCheck = responseBuffer+bytesRead-4;
    if (lastToCheck < responseBuffer) continue;
    for (char* p = responseBuffer; p <= lastToCheck; ++p) {
      if (*p == '\r' && *(p+1) == '\n' &&
	  *(p+2) == '\r' && *(p+3) == '\n') {
	responseBuffer[bytesRead] = '\0';
	return bytesRead;
      }
    }
  }
  
  return 0;
}

Boolean RTSPClient::parseResponseCode(char const* line, 
				      unsigned& responseCode) {
  if (sscanf(line, "%*s%u", &responseCode) != 1) {
    envir().setResultMsg("no response code in line: \"", line, "\"");
    return False;
  }

  return True;
}

Boolean RTSPClient::parseTransportResponse(char const* line, 
					   char*& serverAddressStr,
					   unsigned short& serverPortNum,
					   unsigned char& rtpChannelId,
					   unsigned char& rtcpChannelId) {
  char* foundServerAddressStr = NULL;
  Boolean foundServerPortNum = False;
  Boolean foundChannelIds = False;
  unsigned rtpCid, rtcpCid;

  // First, check for "Transport:"
  if (strncmp(line, "Transport: ", 11) != 0) return False;
  line += 11;

  // Then, run through each of the fields, looking for ones we handle:
  char const* fields = line;
  char* field = strdup(fields);
  while (sscanf(fields, "%[^;]", field) == 1) {
    if (sscanf(field, "server_port=%hu", &serverPortNum) == 1) {
      foundServerPortNum = True;
    } else if (strncmp(field, "source=", 7) == 0) {
      delete foundServerAddressStr;
      foundServerAddressStr = strdup(field+7);
    } else if (sscanf(field, "interleaved=%u-%u", &rtpCid, &rtcpCid) == 2) {
      rtpChannelId = (unsigned char)rtpCid;
      rtcpChannelId = (unsigned char)rtcpCid;
      foundChannelIds = True;
    }

    // (Later, do more extensive checking on the "Transport: " hdr #####)

    fields += strlen(field);
    if (fields[0] == '\0') break;
    ++fields; // skip over the ';'
  }
  delete field;

  if (foundServerPortNum) {
    serverAddressStr = foundServerAddressStr;
    return True;
  } else if (foundChannelIds) {
    return True;
  }

  delete foundServerAddressStr;
  return False;
}
