#pragma once

#if ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/text/WTFString.h>
#include <wtf/Seconds.h>
#include <wtf/MonotonicTime.h>
#include <gst/gst.h>

namespace WebCore {

class MediaPlayerGStreamerEncryptedPlayTracker : public ThreadSafeRefCounted<MediaPlayerGStreamerEncryptedPlayTracker> {
    public:
        static Ref<MediaPlayerGStreamerEncryptedPlayTracker> create() {
            return adoptRef(*new MediaPlayerGStreamerEncryptedPlayTracker());
        }

        ~MediaPlayerGStreamerEncryptedPlayTracker();

        void setURL(String url);
        void setKeySystem(const String &keySystem);
        void notifyStateChange(GstState current, GstState pending);
        void notifyDecryptionStarted();

    private:
        MediaPlayerGStreamerEncryptedPlayTracker() {
            m_playStart = WTF::MonotonicTime::fromRawSeconds(-1);
            m_decryptionStart = WTF::MonotonicTime::fromRawSeconds(-1);
            m_playTime = WTF::Seconds(-1);
        }

        enum PlayState {
            PLAYBACK_STARTED,
            DECRYPTION_STARTED
        };
        void logPlayStart(PlayState state);
        void logPlayEnd();

        String m_url;
        String m_keySystem;
        WTF::MonotonicTime m_playStart;
        WTF::MonotonicTime m_decryptionStart;
        WTF::Seconds m_playTime;
};

} //namespace WebCore

#endif // ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)
