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
// Copyright (c) 1996-2003, Live Networks, Inc.  All rights reserved
// A common framework, used for the "openRTSP" and "playSIP" applications
// Implementation

#include "playCommon.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define snprintf _snprintf
#else
#include <signal.h>
#define USE_SIGNALS 1
#endif

// Forward function definitions:
void setupStreams();
void startPlayingStreams();
void tearDownStreams();
void closeMediaSinks();
void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData);
void sessionAfterPlaying(void* clientData = NULL);
void sessionTimerHandler(void* clientData);
void shutdown(int exitCode = 1);
void signalHandlerShutdown(int sig);
void checkForPacketArrival(void* clientData);
void beginQOSMeasurement();
Boolean setupDestinationRTSPServer();

char const* progName;
UsageEnvironment* env;
Medium* ourClient = NULL;
MediaSession* session = NULL;
TaskToken sessionTimerTask = NULL;
TaskToken arrivalCheckTimerTask = NULL;
TaskToken qosMeasurementTimerTask = NULL;
Boolean createReceivers = True;
Boolean outputQuickTimeFile = False;
QuickTimeFileSink* qtOut = NULL;
Boolean audioOnly = False;
Boolean videoOnly = False;
char const* singleMedium = NULL;
int verbosityLevel = 0;
double endTime = 0;
double endTimeSlop = -1.0; // extra seconds to play at the end
Boolean playContinuously = False;
int simpleRTPoffsetArg = -1;
Boolean sendOptionsRequestOnly = False;
Boolean oneFilePerFrame = False;
Boolean notifyOnPacketArrival = False;
Boolean streamUsingTCP = False;
char* username = NULL;
char* password = NULL;
char* proxyServerName = NULL;
unsigned short proxyServerPortNum = 0;
unsigned char desiredAudioRTPPayloadFormat = 0;
char* mimeSubtype = NULL;
unsigned short movieWidth = 240;
unsigned short movieHeight = 180;
unsigned movieFPS = 15;
char* fileNamePrefix = "";
unsigned fileSinkBufferSize = 10000;
Boolean packetLossCompensate = False;
Boolean syncStreams = False;
Boolean generateHintTracks = False;
char* destRTSPURL = NULL;
unsigned qosMeasurementIntervalMS = 0; // 0 means: Don't output QOS data
unsigned statusCode = 0;

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif
struct timeval startTime;

void usage() {
  *env << "Usage: " << progName
       << " [-p <startPortNum>] [-r|-q] [-a|-v] [-V] [-e <endTime>] [-c] [-s <offset>] [-n]"
	   << (controlConnectionUsesTCP ? " [-t]" : "")
       << " [-u <username> <password>"
	   << (allowProxyServers ? " [<proxy-server> [<proxy-server-port>]]" : "")
       << "]" << (supportCodecSelection ? " [-A <audio-codec-rtp-payload-format-code>|-D <mime-subtype-name>]" : "")
       << " [-w <width> -h <height>] [-f <frames-per-second>] [-y] [-H] [-Q [<measurement-interval>]] [-F <filename-prefix>] [-b <file-sink-buffer-size>] [-m] <url> (or " << progName << " -o [-V] <url>)\n";
  //##### Add "-R <dest-rtsp-url>" #####
  shutdown();
}

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  progName = argv[0];

  gettimeofday(&startTime, &Idunno);

#ifdef USE_SIGNALS
  // Allow ourselves to be shut down gracefully by a SIGHUP or a SIGUSR1:
  signal(SIGHUP, signalHandlerShutdown);
  signal(SIGUSR1, signalHandlerShutdown);
#endif

  unsigned short desiredPortNum = 0;

  // unfortunately we can't use getopt() here, as Windoze doesn't have it
  while (argc > 2) {
    char* const opt = argv[1];
    if (opt[0] != '-') usage();
    switch (opt[1]) {

    case 'p': { // specify start port number
      int portArg;
      if (sscanf(argv[2], "%d", &portArg) != 1) {
	usage();
      }
      if (portArg <= 0 || portArg >= 65536 || portArg&1) {
	*env << "bad port number: " << portArg
		<< " (must be even, and in the range (0,65536))\n";
	usage();
      }
      desiredPortNum = (unsigned short)portArg;
      ++argv; --argc;
      break;
    }

    case 'r': { // do not receive data (instead, just 'play' the stream(s))
      createReceivers = False;
      break;
    }

    case 'q': { // output a QuickTime file (to stdout)
      outputQuickTimeFile = True;
      break;
    }

    case 'a': { // receive/record an audio stream only
      audioOnly = True;
      singleMedium = "audio";
      break;
    }

    case 'v': { // receive/record a video stream only
      videoOnly = True;
      singleMedium = "video";
      break;
    }

    case 'V': { // verbose output
      verbosityLevel = 1;
      break;
    }

    case 'e': { // specify end time, or how much to delay after end time
      float arg;
      if (sscanf(argv[2], "%g", &arg) != 1) {
	usage();
      }
      if (argv[2][0] == '-') { // not "arg<0", in case argv[2] was "-0"
	// a 'negative' argument was specified; use this for "endTimeSlop":
	endTime = 0; // use whatever's in the SDP
	endTimeSlop = -arg;
      } else {
	endTime = arg;
	endTimeSlop = 0;
      }
      ++argv; --argc;
      break;
    }

    case 'c': { // play continuously
      playContinuously = True;
      break;
    }

    case 's': { // specify an offset to use with "SimpleRTPSource"s
      if (sscanf(argv[2], "%d", &simpleRTPoffsetArg) != 1) {
	usage();
      }
      if (simpleRTPoffsetArg < 0) {
	*env << "offset argument to \"-s\" must be >= 0\n";
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'o': { // Send only the "OPTIONS" request to the server
      sendOptionsRequestOnly = True;
      break;
    }

    case 'm': { // output multiple files - one for each frame
      oneFilePerFrame = True;
      break;
    }

    case 'n': { // notify the user when the first data packet arrives
      notifyOnPacketArrival = True;
      break;
    }

    case 't': {
      // stream RTP and RTCP over the TCP 'control' connection
      if (controlConnectionUsesTCP) {
	streamUsingTCP = True;
      } else {
	usage();
      }
      break;
    }

    case 'u': { // specify a username and password
      username = argv[2];
      password = argv[3];
      argv+=2; argc-=2;
      if (allowProxyServers && argc > 3 && argv[2][0] != '-') {
	// The next argument is the name of a proxy server:
	proxyServerName = argv[2];
	++argv; --argc;

	if (argc > 3 && argv[2][0] != '-') {
	  // The next argument is the proxy server port number:
	  if (sscanf(argv[2], "%hu", &proxyServerPortNum) != 1) {
	    usage();
	  }
	  ++argv; --argc;
	}
      }
      break;
    }

    case 'A': { // specify a desired audio RTP payload format
      unsigned formatArg;
      if (sscanf(argv[2], "%u", &formatArg) != 1
	  || formatArg >= 96) {
	usage();
      }
      desiredAudioRTPPayloadFormat = (unsigned char)formatArg;
      ++argv; --argc;
      break;
    }

    case 'D': { // specify a MIME subtype for a dynamic RTP payload type
      mimeSubtype = argv[2];
      if (desiredAudioRTPPayloadFormat==0) desiredAudioRTPPayloadFormat =96;
      ++argv; --argc;
      break;
    }

    case 'w': { // specify a width (pixels) for an output QuickTime movie
      if (sscanf(argv[2], "%hu", &movieWidth) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'h': { // specify a height (pixels) for an output QuickTime movie
      if (sscanf(argv[2], "%hu", &movieHeight) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'f': { // specify a frame rate (per second) for an output QT movie
      if (sscanf(argv[2], "%u", &movieFPS) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'F': { // specify a prefix for the audio and video output files
      fileNamePrefix = argv[2];
      ++argv; --argc;
      break;
    }

    case 'b': { // specify the size of buffers for "FileSink"s
      if (sscanf(argv[2], "%u", &fileSinkBufferSize) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    // Note: The following option is deprecated, and may someday be removed:
    case 'l': { // try to compensate for packet loss by repeating frames
      packetLossCompensate = True;
      break;
    }

    case 'y': { // synchronize audio and video streams
      syncStreams = True;
      break;
    }

    case 'H': { // generate hint tracks (as well as the regular data tracks)
      generateHintTracks = True;
      break;
    }

    case 'Q': { // output QOS measurements
      qosMeasurementIntervalMS = 1000; // default: 1 second

      if (argc > 3 && argv[2][0] != '-') {
	// The next argument is the measurement interval,
	// in multiples of 100 ms
	if (sscanf(argv[2], "%u", &qosMeasurementIntervalMS) != 1) {
	  usage();
	}
	qosMeasurementIntervalMS *= 100;
	++argv; --argc;
      }
      break;
    }

    case 'R': { // inject received data into a RTSP server
      destRTSPURL = argv[2];
      ++argv; --argc;
      break;
    }

    default: {
      usage();
      break;
    }
    }

    ++argv; --argc;
  }
  if (argc != 2) usage();
  if (!createReceivers && outputQuickTimeFile) {
    *env << "The -r and -q flags cannot both be used!\n";
    usage();
  }
  if (destRTSPURL != NULL && (!createReceivers || outputQuickTimeFile)) {
    *env << "The -R flag cannot be used with -r or -q!\n";
    usage();
  }
  if (audioOnly && videoOnly) {
    *env << "The -a and -v flags cannot both be used!\n";
    usage();
  }
  if (!createReceivers && notifyOnPacketArrival) {
    *env << "Warning: Because we're not receiving stream data, the -n flag has no effect\n";
  }
  if (endTimeSlop < 0) {
    // This parameter wasn't set, so use a default value.
    // If we're measuring QOS stats, then don't add any slop, to avoid
    // having 'empty' measurement intervals at the end.
    endTimeSlop = qosMeasurementIntervalMS > 0 ? 0.0 : 5.0;
  }

  char* url = argv[1];

  // Create our client object:
  ourClient = createClient(*env, verbosityLevel, progName);
  if (ourClient == NULL) {
    *env << "Failed to create " << clientProtocolName
		<< " client: " << env->getResultMsg() << "\n";
    shutdown();
  }

  if (sendOptionsRequestOnly) {
    char* optionsResponse = getOptionsResponse(ourClient, url);
    if (optionsResponse == NULL) {
      *env << clientProtocolName << " \"OPTIONS\" request failed: "
	   << env->getResultMsg() << "\n";
    } else {
      *env << clientProtocolName << " \"OPTIONS\" request returned: "
	   << optionsResponse << "\n";
      delete[] optionsResponse;
    }
    shutdown();
  }

  // Open the URL, to get a SDP description:
  char* sdpDescription
    = getSDPDescriptionFromURL(ourClient, url, username, password,
			       proxyServerName, proxyServerPortNum,
			       desiredPortNum);
  if (sdpDescription == NULL) {
    *env << "Failed to get a SDP description from URL \"" << url
		<< "\": " << env->getResultMsg() << "\n";
    shutdown();
  }

  *env << "Opened URL \"" << url
	  << "\", returning a SDP description:\n" << sdpDescription << "\n";

  // Create a media session object from this SDP description:
  session = MediaSession::createNew(*env, sdpDescription);
  delete[] sdpDescription;
  if (session == NULL) {
    *env << "Failed to create a MediaSession object from the SDP description: " << env->getResultMsg() << "\n";
    shutdown();
  } else if (!session->hasSubsessions()) {
    *env << "This session has no media subsessions (i.e., \"m=\" lines)\n";
    shutdown();
  }

  // Then, setup the "RTPSource"s for the session:
  MediaSubsessionIterator iter(*session);
  MediaSubsession *subsession;
  Boolean madeProgress = False;
  char const* singleMediumToTest = singleMedium;
  while ((subsession = iter.next()) != NULL) {
    // If we've asked to receive only a single medium, then check this now:
    if (singleMediumToTest != NULL) {
      if (strcmp(subsession->mediumName(), singleMediumToTest) != 0) {
		  *env << "Ignoring \"" << subsession->mediumName()
			  << "/" << subsession->codecName()
			  << "\" subsession, because we've asked to receive a single " << singleMedium
			  << " session only\n";
	continue;
      } else {
	// Receive this subsession only
	singleMediumToTest = "xxxxx";
	    // this hack ensures that we get only 1 subsession of this type
      }
    }

    if (desiredPortNum != 0) {
      subsession->setClientPortNum(desiredPortNum);
      desiredPortNum += 2;
    }

    if (createReceivers) {
      if (!subsession->initiate(simpleRTPoffsetArg)) {
		*env << "Unable to create receiver for \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession: " << env->getResultMsg() << "\n";
      } else {
		*env << "Created receiver for \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession (client ports " << subsession->clientPortNum()
			<< "-" << subsession->clientPortNum()+1 << ")\n";
		madeProgress = True;

		// Because we're saving the incoming data, rather than playing it
		// in real time, allow an especially large time threshold (1 second)
		// for reordering misordered incoming packets:
		if (subsession->rtpSource() != NULL) {
		 unsigned const thresh = 1000000; // 1 second 
		 subsession->rtpSource()->setPacketReorderingThresholdTime(thresh);
		}
      }
    } else {
      if (subsession->clientPortNum() == 0) {
		*env << "No client port was specified for the \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession.  (Try adding the \"-p <portNum>\" option.)\n";
      } else {	
		madeProgress = True;
      }
    }
  }
  if (!madeProgress) shutdown();

  // Perform additional 'setup' on each subsession, before playing them:
  setupStreams();

  // Create output files:
  if (createReceivers) {
    if (outputQuickTimeFile) {
      // Create a "QuickTimeFileSink", to write to 'stdout':
      qtOut = QuickTimeFileSink::createNew(*env, *session, "stdout",
					   movieWidth, movieHeight,
					   movieFPS,
					   packetLossCompensate,
					   syncStreams,
					   generateHintTracks);
      if (qtOut == NULL) {
		*env << "Failed to create QuickTime file sink for stdout: " << env->getResultMsg();
		shutdown();
      }

      qtOut->startPlaying(sessionAfterPlaying, NULL);
    } else if (destRTSPURL != NULL) {
      // Announce the session into a (separate) RTSP server,
      // and create one or more "RTPTranslator"s to tie the source
      // and destination together:
      if (setupDestinationRTSPServer()) {
		  *env << "Set up destination RTSP session for \"" << destRTSPURL << "\"\n";
      } else {
		  *env << "Failed to set up destination RTSP session for \"" << destRTSPURL
			  << "\": " << env->getResultMsg() << "\n";
		shutdown();
      }
    } else {
      // Create and start "FileSink"s for each subsession:
      madeProgress = False;
      iter.reset();
      while ((subsession = iter.next()) != NULL) {
	if (subsession->readSource() == NULL) continue; // was not initiated
	
	// Create an output file for each desired stream:
	char outFileName[1000];
	if (singleMedium == NULL) {
	  // Output file name is
	  //     "<filename-prefix><medium_name>-<codec_name>-<counter>"
	  static unsigned streamCounter = 0;
	  snprintf(outFileName, sizeof outFileName, "%s%s-%s-%d",
		   fileNamePrefix, subsession->mediumName(),
		   subsession->codecName(), ++streamCounter);
	} else {
	  sprintf(outFileName, "stdout");
	}
	FileSink* fileSink
	  = FileSink::createNew(*env, outFileName,
				fileSinkBufferSize, oneFilePerFrame);
	subsession->sink = fileSink;
	if (subsession->sink == NULL) {
	  *env << "Failed to create FileSink for \"" << outFileName
		  << "\": " << env->getResultMsg() << "\n";
	} else {
	  if (singleMedium == NULL) {
	    *env << "Created output file: \"" << outFileName << "\"\n";
	  } else {
	    *env << "Outputting data from the \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession to 'stdout'\n";
	  }

	  if (strcmp(subsession->mediumName(), "video") == 0
	      && strcmp(subsession->codecName(), "MP4V-ES") == 0
	      && subsession->fmtp_config() != NULL) {
	    // For MPEG-4 video RTP streams, the 'config' information
	    // from the SDP description contains useful VOL etc. headers.
	    // Insert this data at the front of the output file:
	    unsigned configLen;
	    unsigned char* configData
	      = parseGeneralConfigStr(subsession->fmtp_config(), configLen);
	    struct timeval timeNow;
	    gettimeofday(&timeNow, &Idunno);
	    fileSink->addData(configData, configLen, timeNow);
	  }

	  subsession->sink->startPlaying(*(subsession->readSource()),
					 subsessionAfterPlaying,
					 subsession);
	  
	  // Also set a handler to be called if a RTCP "BYE" arrives
	  // for this subsession:
	  if (subsession->rtcpInstance() != NULL) {
	    subsession->rtcpInstance()->setByeHandler(subsessionByeHandler,
						      subsession);
	  }

	  madeProgress = True;
	}
      }
      if (!madeProgress) shutdown();
    }
  }
    
  // Finally, start playing each subsession, to start the data flow:

  startPlayingStreams();

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}


void setupStreams() {
  MediaSubsessionIterator iter(*session);
  MediaSubsession *subsession;
  Boolean madeProgress = False;

  while ((subsession = iter.next()) != NULL) {
    if (subsession->clientPortNum() == 0) continue; // port # was not set

    if (!clientSetupSubsession(ourClient, subsession, streamUsingTCP)) {
      *env << "Failed to setup \"" << subsession->mediumName()
		<< "/" << subsession->codecName()
		<< "\" subsession: " << env->getResultMsg() << "\n";
    } else {
      *env << "Setup \"" << subsession->mediumName()
		<< "/" << subsession->codecName()
		<< "\" subsession (client ports " << subsession->clientPortNum()
		<< "-" << subsession->clientPortNum()+1 << ")\n";
      madeProgress = True;
    }
  }
  if (!madeProgress) shutdown();
}

void startPlayingStreams() {
  if (!clientStartPlayingSession(ourClient, session)) {
    *env << "Failed to start playing session: " << env->getResultMsg() << "\n";
    shutdown();
  } else {
    *env << "Started playing session\n";
  }

  if (qosMeasurementIntervalMS > 0) {
    // Begin periodic QOS measurements:
    beginQOSMeasurement();
  }

  // Figure out how long to delay (if at all) before shutting down, or
  // repeating the playing
  Boolean timerIsBeingUsed = False;
  double totalEndTime = endTime;
  if (endTime == 0) endTime = session->playEndTime(); // use SDP end time
  if (endTime > 0) {
    double const maxDelayTime
      = (double)( ((unsigned)0x7FFFFFFF)/1000000.0 );
    if (endTime > maxDelayTime) {
      *env << "Warning: specified end time " << endTime
		<< " exceeds maximum " << maxDelayTime
		<< "; will not do a delayed shutdown\n";
      endTime = 0.0;
    } else {
      timerIsBeingUsed = True;
      totalEndTime = endTime + endTimeSlop;

      int uSecsToDelay = (int)(totalEndTime*1000000.0);
      sessionTimerTask = env->taskScheduler().scheduleDelayedTask(
         uSecsToDelay, (TaskFunc*)sessionTimerHandler, (void*)NULL);
    }
  }

  char const* actionString
    = createReceivers? "Receiving streamed data":"Data is being streamed";
  if (timerIsBeingUsed) {
    *env << actionString
		<< " (for up to " << totalEndTime
		<< " seconds)...\n";
  } else {
#ifdef USE_SIGNALS
    pid_t ourPid = getpid();
    *env << actionString
		<< " (signal with \"kill -HUP " << (int)ourPid
		<< "\" or \"kill -USR1 " << (int)ourPid
		<< "\" to terminate)...\n";
#else
    *env << actionString << "...\n";
#endif
  }

  // Watch for incoming packets (if desired):
  checkForPacketArrival(NULL);
}

void tearDownStreams() {
  if (session == NULL) return;

  clientTearDownSession(ourClient, session);
}

void closeMediaSinks() {
  Medium::close(qtOut);

  if (session == NULL) return;
  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    Medium::close(subsession->sink);
    subsession->sink = NULL;
  }
}

void subsessionAfterPlaying(void* clientData) {
  // Begin by closing this media subsession:
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  Medium::close(subsession->sink);
  subsession->sink = NULL;

  // Next, check whether *all* subsessions have now been closed:
  MediaSession& session = subsession->parentSession();
  MediaSubsessionIterator iter(session);
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sink != NULL) return; // this subsession is still active
  }

  // All subsessions have now been closed
  sessionAfterPlaying();
}

void subsessionByeHandler(void* clientData) {
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);
  unsigned secsDiff = timeNow.tv_sec - startTime.tv_sec;

  MediaSubsession* subsession = (MediaSubsession*)clientData;
  *env << "Received RTCP \"BYE\" on \"" << subsession->mediumName()
	<< "/" << subsession->codecName()
	<< "\" subsession (after " << secsDiff
	<< " seconds)\n";

  // Act now as if the subsession had closed:
  subsessionAfterPlaying(subsession);
}

void sessionAfterPlaying(void* /*clientData*/) {
  if (!playContinuously) {
    shutdown(0);
  } else {
    // We've been asked to play the stream(s) over again:
    startPlayingStreams();
  }
}

void sessionTimerHandler(void* /*clientData*/) {
  sessionTimerTask = NULL;

  sessionAfterPlaying();
}

class qosMeasurementRecord {
public:
  qosMeasurementRecord(struct timeval const& startTime, RTPSource* src)
    : fSource(src), fNext(NULL),
      kbits_per_second_min(1e20), kbits_per_second_max(0),
      kBytesTotal(0.0),
      packet_loss_fraction_min(1.0), packet_loss_fraction_max(0.0),
      totNumPacketsReceived(0), totNumPacketsExpected(0) {
    measurementEndTime = measurementStartTime = startTime;

    RTPReceptionStatsDB::Iterator statsIter(src->receptionStatsDB());
    // Assume that there's only one SSRC source (usually the case):
    RTPReceptionStats* stats = statsIter.next(True);
    if (stats != NULL) {
      kBytesTotal = stats->totNumKBytesReceived();
      totNumPacketsReceived = stats->totNumPacketsReceived();
      totNumPacketsExpected = stats->totNumPacketsExpected();
    }
  }
  virtual ~qosMeasurementRecord() { delete fNext; }
    
  void periodicQOSMeasurement(struct timeval const& timeNow);

public:
  RTPSource* fSource;
  qosMeasurementRecord* fNext;

public:
  struct timeval measurementStartTime, measurementEndTime;
  double kbits_per_second_min, kbits_per_second_max;
  double kBytesTotal;
  double packet_loss_fraction_min, packet_loss_fraction_max;
  unsigned totNumPacketsReceived, totNumPacketsExpected;
};

static qosMeasurementRecord* qosRecordHead = NULL;

static void periodicQOSMeasurement(void* clientData); // forward

static unsigned nextQOSMeasurementUSecs;

static void scheduleNextQOSMeasurement() {
  nextQOSMeasurementUSecs += qosMeasurementIntervalMS*1000;
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);
  unsigned timeNowUSecs = timeNow.tv_sec*1000000 + timeNow.tv_usec;
  unsigned usecsToDelay = nextQOSMeasurementUSecs < timeNowUSecs ? 0
    : nextQOSMeasurementUSecs - timeNowUSecs;

  qosMeasurementTimerTask = env->taskScheduler().scheduleDelayedTask(
     usecsToDelay, (TaskFunc*)periodicQOSMeasurement, (void*)NULL);
}

static void periodicQOSMeasurement(void* /*clientData*/) {
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);

  for (qosMeasurementRecord* qosRecord = qosRecordHead;
       qosRecord != NULL; qosRecord = qosRecord->fNext) {
    qosRecord->periodicQOSMeasurement(timeNow);
  }

  // Do this again later:
  scheduleNextQOSMeasurement();
}

void qosMeasurementRecord
::periodicQOSMeasurement(struct timeval const& timeNow) {
  unsigned secsDiff = timeNow.tv_sec - measurementEndTime.tv_sec;
  int usecsDiff = timeNow.tv_usec - measurementEndTime.tv_usec;
  double timeDiff = secsDiff + usecsDiff/1000000.0;
  measurementEndTime = timeNow;

  RTPReceptionStatsDB::Iterator statsIter(fSource->receptionStatsDB());
  // Assume that there's only one SSRC source (usually the case):
  RTPReceptionStats* stats = statsIter.next(True);
  if (stats != NULL) {
    double kBytesTotalNow = stats->totNumKBytesReceived();
    double kBytesDeltaNow = kBytesTotalNow - kBytesTotal;
    kBytesTotal = kBytesTotalNow;

    double kbpsNow = timeDiff == 0.0 ? 0.0 : 8*kBytesDeltaNow/timeDiff;
    if (kbpsNow < 0.0) kbpsNow = 0.0; // in case of roundoff error
    if (kbpsNow < kbits_per_second_min) kbits_per_second_min = kbpsNow;
    if (kbpsNow > kbits_per_second_max) kbits_per_second_max = kbpsNow;

    unsigned totReceivedNow = stats->totNumPacketsReceived();
    unsigned totExpectedNow = stats->totNumPacketsExpected();
    unsigned deltaReceivedNow = totReceivedNow - totNumPacketsReceived;
    unsigned deltaExpectedNow = totExpectedNow - totNumPacketsExpected;
    totNumPacketsReceived = totReceivedNow;
    totNumPacketsExpected = totExpectedNow;

    double lossFractionNow = deltaExpectedNow == 0 ? 0.0
      : 1.0 - deltaReceivedNow/(double)deltaExpectedNow;
    //if (lossFractionNow < 0.0) lossFractionNow = 0.0; //reordering can cause
    if (lossFractionNow < packet_loss_fraction_min) {
      packet_loss_fraction_min = lossFractionNow;
    }
    if (lossFractionNow > packet_loss_fraction_max) {
      packet_loss_fraction_max = lossFractionNow;
    }
  }
}

void beginQOSMeasurement() {
  // Set up a measurement record for each active subsession:
  struct timeval startTime;
  gettimeofday(&startTime, &Idunno);
  nextQOSMeasurementUSecs = startTime.tv_sec*1000000 + startTime.tv_usec;
  qosMeasurementRecord* qosRecordTail = NULL;
  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    RTPSource* src = subsession->rtpSource();
    if (src == NULL) continue;

    qosMeasurementRecord* qosRecord
      = new qosMeasurementRecord(startTime, src);
    if (qosRecordHead == NULL) qosRecordHead = qosRecord;
    if (qosRecordTail != NULL) qosRecordTail->fNext = qosRecord;
    qosRecordTail  = qosRecord;
  }

  // Then schedule the first of the periodic measurements:
  scheduleNextQOSMeasurement();
}

void printQOSData(int exitCode) {
  if (exitCode != 0 && statusCode == 0) statusCode = 2;
  *env << "begin_QOS_statistics\n";
  *env << "server_availability\t" << (statusCode == 1 ? 0 : 100) << "\n";
  *env << "stream_availability\t" << (statusCode == 0 ? 100 : 0) << "\n";

  // Print out stats for each active subsession:
  qosMeasurementRecord* curQOSRecord = qosRecordHead;
  if (session != NULL) {
    MediaSubsessionIterator iter(*session);
    MediaSubsession* subsession;
    while ((subsession = iter.next()) != NULL) {
      RTPSource* src = subsession->rtpSource();
      if (src == NULL) continue;

      *env << "subsession\t" << subsession->mediumName()
		<< "/" << subsession->codecName() << "\n";

      unsigned numPacketsReceived = 0, numPacketsExpected = 0;

      if (curQOSRecord != NULL) {
		numPacketsReceived = curQOSRecord->totNumPacketsReceived;
		numPacketsExpected = curQOSRecord->totNumPacketsExpected;
      }
      *env << "num_packets_received\t" << numPacketsReceived << "\n";
      *env << "num_packets_lost\t" << numPacketsExpected - numPacketsReceived << "\n";
      
      if (curQOSRecord != NULL) {
		unsigned secsDiff = curQOSRecord->measurementEndTime.tv_sec
		 - curQOSRecord->measurementStartTime.tv_sec;
		int usecsDiff = curQOSRecord->measurementEndTime.tv_usec
		 - curQOSRecord->measurementStartTime.tv_usec;
		double measurementTime = secsDiff + usecsDiff/1000000.0;
		*env << "elapsed_measurement_time\t" << measurementTime << "\n";
	
		*env << "kBytes_received_total\t" << curQOSRecord->kBytesTotal << "\n";
	
		*env << "measurement_sampling_interval_ms\t" << qosMeasurementIntervalMS << "\n";
	
		if (curQOSRecord->kbits_per_second_max == 0) {
			// special case: we didn't receive any data:
			*env <<
			"kbits_per_second_min\tunavailable\n"
			"kbits_per_second_ave\tunavailable\n"
			"kbits_per_second_max\tunavailable\n";
		} else {
			*env << "kbits_per_second_min\t" << curQOSRecord->kbits_per_second_min << "\n";
			*env << "kbits_per_second_ave\t"
				<< (measurementTime == 0.0 ? 0.0 : 8*curQOSRecord->kBytesTotal/measurementTime) << "\n";
			*env << "kbits_per_second_max\t" << curQOSRecord->kbits_per_second_max << "\n";
		}
	
		*env << "packet_loss_percentage_min\t" << 100*curQOSRecord->packet_loss_fraction_min << "\n";
		double packetLossFraction = numPacketsExpected == 0 ? 1.0
			: 1.0 - numPacketsReceived/(double)numPacketsExpected;
		if (packetLossFraction < 0.0) packetLossFraction = 0.0;
		*env << "packet_loss_percentage_ave\t" << 100*packetLossFraction << "\n";
		*env << "packet_loss_percentage_max\t"
			<< (packetLossFraction == 1.0 ? 100.0 : 100*curQOSRecord->packet_loss_fraction_max) << "\n";
	
		RTPReceptionStatsDB::Iterator statsIter(src->receptionStatsDB());
		// Assume that there's only one SSRC source (usually the case):
		RTPReceptionStats* stats = statsIter.next(True);
		if (stats != NULL) {
			*env << "inter_packet_gap_ms_min\t" << stats->minInterPacketGapUS()/1000.0 << "\n";
			struct timeval totalGaps = stats->totalInterPacketGaps();
			double totalGapsMS = totalGaps.tv_sec*1000.0 + totalGaps.tv_usec/1000.0;
			unsigned totNumPacketsReceived = stats->totNumPacketsReceived();
			*env << "inter_packet_gap_ms_ave\t"
				<< (totNumPacketsReceived == 0 ? 0.0 : totalGapsMS/totNumPacketsReceived) << "\n";
			*env << "inter_packet_gap_ms_max\t" << stats->maxInterPacketGapUS()/1000.0 << "\n";
		}

		curQOSRecord = curQOSRecord->fNext;
      }
    }
  }    

  *env << "end_QOS_statistics\n";
  delete qosRecordHead;
}

void shutdown(int exitCode) {
  if (env != NULL) {
    env->taskScheduler().unscheduleDelayedTask(sessionTimerTask);
    env->taskScheduler().unscheduleDelayedTask(arrivalCheckTimerTask);
    env->taskScheduler().unscheduleDelayedTask(qosMeasurementTimerTask);
  }

  if (qosMeasurementIntervalMS > 0) {
    printQOSData(exitCode);
  }

  // Close our output files:
  closeMediaSinks();

  // Teardown, then shutdown, any outstanding RTP/RTCP subsessions
  tearDownStreams();
  Medium::close(session);

  // Finally, shut down our client:
  Medium::close(ourClient);

  // Adios...
  exit(exitCode);
}

void signalHandlerShutdown(int /*sig*/) {
  *env << "Got shutdown signal\n";
  shutdown(0);
}

void checkForPacketArrival(void* clientData) {
  if (!notifyOnPacketArrival) return; // we're not checking 

  // Check each subsession, to see whether it has received data packets:
  unsigned numSubsessionsChecked = 0;
  unsigned numSubsessionsWithReceivedData = 0;
  unsigned numSubsessionsThatHaveBeenSynced = 0;

  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    RTPSource* src = subsession->rtpSource();
    if (src == NULL) continue;
    ++numSubsessionsChecked;

    if (src->receptionStatsDB().numActiveSourcesSinceLastReset() > 0) {
      // At least one data packet has arrived
      ++numSubsessionsWithReceivedData;
    }
    if (src->hasBeenSynchronizedUsingRTCP()) {
      ++numSubsessionsThatHaveBeenSynced;
    }
  }

  unsigned numSubsessionsToCheck = numSubsessionsChecked;
  if (qtOut != NULL) {
    // Special case for "QuickTimeFileSink"s: They might not use all of the
    // input sources:
    numSubsessionsToCheck = qtOut->numActiveSubsessions();
  }

  Boolean notifyTheUser;
  if (!syncStreams) {
    notifyTheUser = numSubsessionsWithReceivedData > 0; // easy case
  } else {
    notifyTheUser = numSubsessionsWithReceivedData >= numSubsessionsToCheck
      && numSubsessionsThatHaveBeenSynced == numSubsessionsChecked;
    // Note: A subsession with no active sources is considered to be synced
  }
  if (notifyTheUser) {
    struct timeval timeNow;
    gettimeofday(&timeNow, &Idunno);
	char timestampStr[100];
	sprintf(timestampStr, "%ld%03ld", timeNow.tv_sec, timeNow.tv_usec/1000);
    *env << (syncStreams ? "Synchronized d" : "D")
		<< "ata packets have begun arriving [" << timestampStr << "]\007\n";
    return;
  }

  // No luck, so reschedule this check again, after a delay:
  int uSecsToDelay = 100000; // 100 ms
  arrivalCheckTimerTask
    = env->taskScheduler().scheduleDelayedTask(uSecsToDelay,
			       (TaskFunc*)checkForPacketArrival, NULL);
}

// WORK IN PROGRESS #####
class RTPTranslator: public FramedFilter {
public:
  static RTPTranslator* createNew(UsageEnvironment& env,
				  FramedSource* source);

private:
  RTPTranslator(UsageEnvironment& env, FramedSource* source);
  virtual ~RTPTranslator();

  static void afterGettingFrame(void* clientData,
                                unsigned numBytesRead,
                                struct timeval presentationTime);
  void afterGettingFrame1(unsigned numBytesRead,
			  struct timeval presentationTime);

private: // redefined virtual function:
  virtual void doGetNextFrame();

private:
  //unsigned char fBuffer[50000];//##### Later: parameterize
};

RTPTranslator* RTPTranslator::createNew(UsageEnvironment& env,
					FramedSource* source) {
  // Check whether source is a "RTPSource"??? #####
  return new RTPTranslator(env, source);
}

RTPTranslator::RTPTranslator(UsageEnvironment& env, FramedSource* source)
  : FramedFilter(env, source) {
}

RTPTranslator::~RTPTranslator() {
}

void RTPTranslator::doGetNextFrame() {
  // For now, do a direct relay #####
  fInputSource->getNextFrame(fTo, fMaxSize,
			     afterGettingFrame, this,
			     handleClosure, this);
}

void RTPTranslator::afterGettingFrame(void* clientData,
				      unsigned numBytesRead,
				      struct timeval presentationTime) {
  RTPTranslator* rtpTranslator = (RTPTranslator*)clientData;
  rtpTranslator->afterGettingFrame1(numBytesRead, presentationTime);
}

void RTPTranslator::afterGettingFrame1(unsigned numBytesRead,
				       struct timeval presentationTime) {
  fFrameSize = numBytesRead;
  fPresentationTime = presentationTime;
  afterGetting(this);
}

//#####
RTSPClient* rtspClientOutgoing = NULL;
Boolean setupDestinationRTSPServer() {
  do {
    rtspClientOutgoing
      = RTSPClient::createNew(*env, verbosityLevel, progName);
    if (rtspClientOutgoing == NULL) break;

    // Construct the SDP description to announce into the RTSP server:

    // First, get our own IP address, and that of the RTSP server:
    struct in_addr ourIPAddress;
    ourIPAddress.s_addr = ourSourceAddressForMulticast(*env);
    char* ourIPAddressStr = strDup(our_inet_ntoa(ourIPAddress));

    NetAddress serverAddress;
    portNumBits serverPortNum;
    if (!RTSPClient::parseRTSPURL(*env, destRTSPURL,
				  serverAddress, serverPortNum)) break;
    struct in_addr serverIPAddress;
    serverIPAddress.s_addr = *(unsigned*)(serverAddress.data());
    char* serverIPAddressStr = strDup(our_inet_ntoa(serverIPAddress));

    char const* destSDPFmt =
      "v=0\r\n"
      "o=- %u %u IN IP4 %s\r\n"
      "s=RTSP session, relayed through \"%s\"\n"
      "i=relayed RTSP session\n"
      "t=0 0\n"
      "c=IN IP4 %s\n"
      "a=control:*\n"
      "m=audio 0 RTP/AVP %u\n"
      "a=control:trackID=0\n";
    //#####LATER: Support video as well; multiple tracks; other codecs #####
    unsigned destSDPFmtSize = strlen(destSDPFmt)
      + 20 /* max int len */ + 20 + strlen(ourIPAddressStr)
      + strlen(progName)
      + strlen(serverIPAddressStr)
      + 3 /* max char len */;
    char* destSDPDescription = new char[destSDPFmtSize];
    sprintf(destSDPDescription, destSDPFmt,
	    our_random(), our_random(), ourIPAddressStr,
	    progName,
	    serverIPAddressStr,
	    desiredAudioRTPPayloadFormat);
    Boolean announceResult;
    if (username != NULL) {
      announceResult
	= rtspClientOutgoing->announceWithPassword(destRTSPURL,
						   destSDPDescription,
						   username, password);
    } else {
      announceResult
	= rtspClientOutgoing->announceSDPDescription(destRTSPURL,
						     destSDPDescription);
    }
    delete[] serverIPAddressStr; delete[] ourIPAddressStr;
    if (!announceResult) break;
    
    // Then, create a "MediaSession" object from this SDP description:
    MediaSession* destSession
      = MediaSession::createNew(*env, destSDPDescription);
    delete[] destSDPDescription;
    if (destSession == NULL) break;

    // Initiate, setup and play "destSession".
    // ##### TEMP HACK - take advantage of the fact that we have
    // ##### a single audio session only.
    MediaSubsession* destSubsession;
    PrioritizedRTPStreamSelector* multiSource;
    int multiSourceSessionId;
    char const* mimeType
      = desiredAudioRTPPayloadFormat == 0 ? "audio/PCMU" 
      : desiredAudioRTPPayloadFormat == 3 ? "audio/GSM"
      : "audio/???"; //##### FIX
    if (!destSession->initiateByMediaType(mimeType, destSubsession,
					  multiSource,
					  multiSourceSessionId)) break;
    if (!rtspClientOutgoing->setupMediaSubsession(*destSubsession,
						  True, True)) break;
    if (!rtspClientOutgoing->playMediaSubsession(*destSubsession,
						 0.0, -1.0,
						 True/*hackForDSS*/)) break;

    // Next, set up "RTPSink"s for the outgoing packets:
    struct in_addr destAddr; destAddr.s_addr = 0; // because we're using TCP
    Groupsock* destGS = new Groupsock(*env, destAddr, 0/*aud*/, 255);
    if (destGS == NULL) break;
    RTPSink* destRTPSink = NULL;
    if (desiredAudioRTPPayloadFormat == 0) {
      destRTPSink = SimpleRTPSink::createNew(*env, destGS, 0, 8000,
					     "audio", "PCMU");
    } else if (desiredAudioRTPPayloadFormat == 3) {
      destRTPSink = GSMAudioRTPSink::createNew(*env, destGS);
    }
    if (destRTPSink == NULL) break;

    // Tell the sink to stream using TCP:
    destRTPSink->setStreamSocket(rtspClientOutgoing->socketNum(), 0/*aud*/);
    // LATER: set up RTCPInstance also #####

    // Next, set up RTPTranslator(s) between source(s) and destination(s),
    // and start playing them.
    MediaSubsessionIterator iter(*session);
    MediaSubsession *sourceSubsession = NULL;
    while ((sourceSubsession = iter.next()) != NULL) {
      if (strcmp(sourceSubsession->mediumName(), "audio") == 0) break;
    }
    if (sourceSubsession == NULL) break;
    RTPTranslator* rtpTranslator
      = RTPTranslator::createNew(*env, sourceSubsession->readSource());
    if (rtpTranslator == NULL) break;
    destRTPSink->startPlaying(*rtpTranslator,
			      subsessionAfterPlaying, sourceSubsession);
    
    // LATER: delete media on close #####

    return True;
  } while (0);

  return False;
}
