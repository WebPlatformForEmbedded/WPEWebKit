#include "config.h"

#include "WebRtcOrgUtils.h"

#include "webrtc/base/ssladapter.h"

namespace WebCore {

void initializeWebRtcOrg()
{
    // TODO: Route RTC logs to WebCore's logger
    const char* logConfig = getenv("WRTC_LOG");
    if (logConfig == nullptr) {
        logConfig = "tstamp info debug";
    }
    rtc::LogMessage::ConfigureLogging(logConfig);
    rtc::InitializeSSL();
    rtc::ThreadManager::Instance();
}

void shutdownWebRtcOrg() 
{
	rtc::CleanupSSL();
}

#endif // USE(OPENWEBRTC)

