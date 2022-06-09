
#include "config.h"
#include "MediaPlayerGStreamerEncryptedPlayTracker.h"

#include <wtf/Atomics.h>

#if ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)

namespace WebCore {

MediaPlayerGStreamerEncryptedPlayTracker::~MediaPlayerGStreamerEncryptedPlayTracker() {
    // Mimic Playback end i.e EOS
    notifyStateChange(GST_STATE_READY, GST_STATE_NULL);
}

void MediaPlayerGStreamerEncryptedPlayTracker::setURL(String url) {
    m_url = url;
}

void MediaPlayerGStreamerEncryptedPlayTracker::setKeySystem(const String &keySystem) {
    m_keySystem = keySystem;
}

void MediaPlayerGStreamerEncryptedPlayTracker::notifyStateChange(GstState current, GstState pending) {
    if(current != GST_STATE_PLAYING && pending == GST_STATE_PLAYING) {
        m_playStart = WTF::MonotonicTime::now();
        logPlayStart(PLAYBACK_STARTED);
    } else if(current != GST_STATE_PAUSED && pending == GST_STATE_PAUSED) {
        if(m_playStart > WTF::MonotonicTime::fromRawSeconds(0))
            m_playTime += WTF::MonotonicTime::now() - std::max(m_playStart, m_decryptionStart);

        m_playStart = WTF::MonotonicTime::fromRawSeconds(-1);
    } else if(current == GST_STATE_READY && pending == GST_STATE_NULL) {
        if(m_playStart > WTF::MonotonicTime::fromRawSeconds(0))
            m_playTime += WTF::MonotonicTime::now() - std::max(m_playStart, m_decryptionStart);

        if(m_playTime > 0_s && m_decryptionStart > WTF::MonotonicTime::fromRawSeconds(0)) {
            fprintf(stderr, "Encrypted Content play time for url=%s is %.3f sec\n", m_url.utf8().data(), m_playTime.seconds());
        }

        m_decryptionStart = WTF::MonotonicTime::fromRawSeconds(-1);
        m_playStart = WTF::MonotonicTime::fromRawSeconds(-1);
        m_playTime = WTF::Seconds(-1);
    }
}

void MediaPlayerGStreamerEncryptedPlayTracker::notifyDecryptionStarted() {
    if(m_decryptionStart < WTF::MonotonicTime::fromRawSeconds(0)) {
        MonotonicTime  currentTime = WTF::MonotonicTime::now();
        if(WTF::atomicCompareExchangeWeakRelaxed(&m_decryptionStart, WTF::MonotonicTime::fromRawSeconds(-1), currentTime))
            logPlayStart(DECRYPTION_STARTED);
    }
}

void MediaPlayerGStreamerEncryptedPlayTracker::logPlayStart(PlayState state) {
    if(m_playTime < 0_s &&
        ((state == PLAYBACK_STARTED && m_decryptionStart > WTF::MonotonicTime::fromRawSeconds(0)) ||
         (state == DECRYPTION_STARTED && m_playStart > WTF::MonotonicTime::fromRawSeconds(0)))) {
            fprintf(stderr, "Started playing Encrypted Content, url=%s, keySystem=%s\n", m_url.utf8().data(), m_keySystem.utf8().data());
    }
}

} //namespace WebCore

#endif // ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)
