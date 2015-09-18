/*
 *  Copyright (C) 2015 Igalia S.L. All rights reserved.
 *  Copyright (C) 2015 Metrological. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef MediaPlayerPrivateGStreamerOwr_h
#define MediaPlayerPrivateGStreamerOwr_h

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && USE(OPENWEBRTC)

#include "MediaPlayerPrivateGStreamerBase.h"
#include "RealtimeMediaSource.h"

typedef struct _OwrGstVideoRenderer OwrGstVideoRenderer;
typedef struct _OwrGstAudioRenderer OwrGstAudioRenderer;

namespace WebCore {

class MediaStreamPrivate;
class RealtimeMediaSourceOwr;
class URL;

class MediaPlayerPrivateGStreamerOwr : public MediaPlayerPrivateGStreamerBase, private RealtimeMediaSource::Observer {
public:
    explicit MediaPlayerPrivateGStreamerOwr(MediaPlayer*);
    ~MediaPlayerPrivateGStreamerOwr();

    static void registerMediaEngine(MediaEngineRegistrar);
    virtual String engineDescription() const { return "OpenWebRTC"; }

    virtual void load(const String&);
#if ENABLE(MEDIA_SOURCE)
    virtual void load(const String&, MediaSourcePrivateClient*) { }
#endif
    virtual void load(MediaStreamPrivate&);
    virtual void cancelLoad() { }

    virtual void prepareToPlay() { }
    void play();
    void pause();

    bool hasVideo() const;
    bool hasAudio() const;

    virtual float duration() const { return 0; }

    virtual float currentTime() const;
    virtual void seek(float) { }
    virtual bool seeking() const { return false; }

    virtual void setRate(float) { }
    virtual void setPreservesPitch(bool) { }
    virtual bool paused() const { return m_paused; }

    virtual bool hasClosedCaptions() const { return false; }
    virtual void setClosedCaptionsVisible(bool) { };

    virtual float maxTimeSeekable() const { return 0; }
    virtual std::unique_ptr<PlatformTimeRanges> buffered() const { return std::make_unique<PlatformTimeRanges>(); }
    bool didLoadingProgress() const;

    virtual unsigned long long totalBytes() const { return 0; }
    virtual unsigned bytesLoaded() const { return 0; }

    virtual bool canLoadPoster() const { return false; }
    virtual void setPoster(const String&) { }

    virtual bool isLiveStream() const { return true; }

    // RealtimeMediaSource::Observer implementation
    virtual void sourceStopped() override final;
    virtual void sourceMutedChanged() override final;
    virtual bool preventSourceFromStopping() override final;

protected:
    virtual GstElement* createVideoSink();

private:
    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);
    static bool isAvailable();
    void createGSTAudioSinkBin();
    void loadingFailed(MediaPlayer::NetworkState error);
    bool internalLoad();
    void stop();
    virtual GstElement* audioSink() const { return m_audioSink.get(); }

private:
    bool m_paused;
    bool m_stopped;
    RefPtr<RealtimeMediaSourceOwr> m_videoSource;
    RefPtr<RealtimeMediaSourceOwr> m_audioSource;
    GRefPtr<GstElement> m_audioSink;
    RefPtr<MediaStreamPrivate> m_streamPrivate;
    OwrGstVideoRenderer* m_videoRenderer;
    OwrGstAudioRenderer* m_audioRenderer;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER) && USE(OPENWEBRTC)

#endif // MediaPlayerPrivateGStreamerOwr_h
