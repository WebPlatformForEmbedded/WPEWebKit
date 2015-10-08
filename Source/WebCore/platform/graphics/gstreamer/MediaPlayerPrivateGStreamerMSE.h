/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009, 2010 Igalia S.L
 * Copyright (C) 2014 Cable Television Laboratories, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef MediaPlayerPrivateGStreamerMSE_h
#define MediaPlayerPrivateGStreamerMSE_h
#if ENABLE(VIDEO) && USE(GSTREAMER) && ENABLE(MEDIA_SOURCE) && ENABLE(VIDEO_TRACK)

#include "GRefPtrGStreamer.h"
#include "MediaPlayerPrivateGStreamerBase.h"
#include "MediaSample.h"
#include "Timer.h"

#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>
#include <wtf/Forward.h>
#include <wtf/glib/GSourceWrap.h>
#include <wtf/HashMap.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefPtr.h>

#if USE(GSTREAMER_MPEGTS)
#include <wtf/text/AtomicStringHash.h>
#endif

#include "MediaSourceGStreamer.h"
#include "WebKitMediaSourceGStreamer.h"

typedef struct _GstBuffer GstBuffer;
typedef struct _GstMessage GstMessage;
typedef struct _GstElement GstElement;
typedef struct _GstMpegtsSection GstMpegtsSection;

namespace WebCore {

#if ENABLE(WEB_AUDIO)
class AudioSourceProvider;
class AudioSourceProviderGStreamer;
#endif

class AudioTrackPrivateGStreamer;
class InbandMetadataTextTrackPrivateGStreamer;
class InbandTextTrackPrivateGStreamer;
class MediaPlayerRequestInstallMissingPluginsCallback;
class VideoTrackPrivateGStreamer;
class MediaSourceClientGStreamerMSE;
class AppendPipeline;

class MediaPlayerPrivateGStreamerMSE : public MediaPlayerPrivateGStreamerBase {
    WTF_MAKE_NONCOPYABLE(MediaPlayerPrivateGStreamerMSE); WTF_MAKE_FAST_ALLOCATED;

    friend class MediaSourceClientGStreamerMSE;

public:
    explicit MediaPlayerPrivateGStreamerMSE(MediaPlayer*);
    ~MediaPlayerPrivateGStreamerMSE();

    static void registerMediaEngine(MediaEngineRegistrar);
    void handleSyncMessage(GstMessage*);
    gboolean handleMessage(GstMessage*);
    void handlePluginInstallerResult(GstInstallPluginsReturn);

    bool hasVideo() const override { return m_hasVideo; }
    bool hasAudio() const override { return m_hasAudio; }

    void load(const String &url) override;
    void load(const String& url, MediaSourcePrivateClient*) override;
#if ENABLE(MEDIA_STREAM)
    void load(MediaStreamPrivate&) override;
#endif
    void commitLoad();
    void cancelLoad() override;

    void prepareToPlay() override;
    void play() override;
    void pause() override;

    bool paused() const override;
    bool seeking() const override;

    float duration() const override;
    float currentTime() const override;
    void seek(float) override;

    void setReadyState(MediaPlayer::ReadyState state);
    void waitForSeekCompleted();
    void seekCompleted();
    MediaSourcePrivateClient* mediaSourcePrivateClient() { return m_mediaSource.get(); }

    void setRate(float) override;
    double rate() const override;
    void setPreservesPitch(bool) override;

    void setPreload(MediaPlayer::Preload) override;
    void fillTimerFired();

    std::unique_ptr<PlatformTimeRanges> buffered() const override;
    float maxTimeSeekable() const override;
    bool didLoadingProgress() const override;
    unsigned long long totalBytes() const override;
    float maxTimeLoaded() const override;

    void loadStateChanged();
    void timeChanged();
    void didEnd();
    void notifyDurationChanged();
    void durationChanged();
    void loadingFailed(MediaPlayer::NetworkState);

    void videoChanged();
    void videoCapsChanged();
    void audioChanged();
    void notifyPlayerOfVideo();
    void notifyPlayerOfVideoCaps();
    void notifyPlayerOfAudio();

    void textChanged();
    void notifyPlayerOfText();

    void newTextSample();
    void notifyPlayerOfNewTextSample();

    void sourceChanged();
    GstElement* audioSink() const override;
    void configurePlaySink();

    void setAudioStreamProperties(GObject*);

    void simulateAudioInterruption() override;

    bool changePipelineState(GstState);

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
    void dispatchDecryptionKey(GstBuffer*) override;
#endif
#if USE(DXDRM)
    void emitSession() override;
#endif

#if ENABLE(WEB_AUDIO)
    AudioSourceProvider* audioSourceProvider() override { return reinterpret_cast<AudioSourceProvider*>(m_audioSourceProvider.get()); }
#endif

    bool isLiveStream() const override { return m_isStreaming; }

    void trackDetected(RefPtr<AppendPipeline>, RefPtr<WebCore::TrackPrivateBase> oldTrack, RefPtr<WebCore::TrackPrivateBase> newTrack);
    void notifySeekNeedsData(const MediaTime& seekTime);

private:
    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

    static bool isAvailable();

    GstElement* createAudioSink() override;

    float playbackPosition() const;

    void cacheDuration();
    void updateStates();
    void asyncStateChangeDone();

    void createGSTPlayBin();

    bool loadNextLocation();
    void mediaLocationChanged(GstMessage*);

    void processBufferingStats(GstMessage*);
#if USE(GSTREAMER_MPEGTS)
    void processMpegTsSection(GstMpegtsSection*);
#endif
    void processTableOfContents(GstMessage*);
    void processTableOfContentsEntry(GstTocEntry*, GstTocEntry* parent);
    bool doSeek(gint64 position, float rate, GstSeekFlags seekType);
    void updatePlaybackRate();


    String engineDescription() const override { return "GStreamer"; }
    bool didPassCORSAccessCheck() const override;
    bool canSaveMediaData() const override;

    // TODO: Implement
    unsigned long totalVideoFrames() override { return 0; }
    unsigned long droppedVideoFrames() override { return 0; }
    unsigned long corruptedVideoFrames() override { return 0; }
    MediaTime totalFrameDelay() override { return MediaTime::zeroTime(); }
    bool timeIsBuffered(float);

    void setMediaSourceClient(PassRefPtr<MediaSourceClientGStreamerMSE>);
    RefPtr<MediaSourceClientGStreamerMSE> mediaSourceClient();

    RefPtr<AppendPipeline> appendPipelineByTrackId(const AtomicString& trackId);

private:
    GRefPtr<GstElement> m_webKitMediaSrc;
    GRefPtr<GstElement> m_textAppSink;
    GRefPtr<GstPad> m_textAppSinkPad;
    float m_seekTime;
    bool m_changingRate;
    float m_endTime;
    mutable bool m_isStreaming;
    GstStructure* m_mediaLocations;
    int m_mediaLocationCurrentIndex;
    bool m_resetPipeline;
    bool m_paused;
    bool m_playbackRatePause;
    bool m_seeking;
    bool m_seekIsPending;
    float m_timeOfOverlappingSeek;
    bool m_canFallBackToLastFinishedSeekPosition;
    bool m_buffering;
    float m_playbackRate;
    float m_lastPlaybackRate;
    bool m_errorOccured;
    mutable gfloat m_mediaDuration;
    bool m_downloadFinished;
    Timer m_fillTimer;
    float m_maxTimeLoaded;
    int m_bufferingPercentage;
    MediaPlayer::Preload m_preload;
    bool m_delayingLoad;
    bool m_mediaDurationKnown;
    mutable float m_maxTimeLoadedAtLastDidLoadingProgress;
    bool m_volumeAndMuteInitialized;
    bool m_hasVideo;
    bool m_hasAudio;
    GSourceWrap::Static m_audioTimerHandler;
    GSourceWrap::Static m_textTimerHandler;
    GSourceWrap::Static m_videoTimerHandler;
    GSourceWrap::Static m_videoCapsTimerHandler;
    GSourceWrap::Static m_readyTimerHandler;
    mutable unsigned long long m_totalBytes;
    URL m_url;
    bool m_preservesPitch;
    mutable float m_cachedPosition;
    mutable double m_lastQuery;
#if ENABLE(WEB_AUDIO)
    std::unique_ptr<AudioSourceProviderGStreamer> m_audioSourceProvider;
#endif
    GstState m_requestedState;
    GRefPtr<GstElement> m_autoAudioSink;
    RefPtr<MediaPlayerRequestInstallMissingPluginsCallback> m_missingPluginsCallback;
    Vector<RefPtr<AudioTrackPrivateGStreamer>> m_audioTracks;
    Vector<RefPtr<InbandTextTrackPrivateGStreamer>> m_textTracks;
    Vector<RefPtr<VideoTrackPrivateGStreamer>> m_videoTracks;
    RefPtr<InbandMetadataTextTrackPrivateGStreamer> m_chaptersTrack;
#if USE(GSTREAMER_MPEGTS)
    HashMap<AtomicString, RefPtr<InbandMetadataTextTrackPrivateGStreamer>> m_metadataTracks;
#endif
    RefPtr<MediaSourcePrivateClient> m_mediaSource;
#if USE(GSTREAMER_GL)
    GstGLContext* m_glContext;
    GstGLDisplay* m_glDisplay;
#endif
    Mutex m_pendingAsyncOperationsLock;
    GList* m_pendingAsyncOperations;
    bool m_seekCompleted;

    HashMap<RefPtr<SourceBufferPrivateGStreamer>, RefPtr<AppendPipeline> > m_appendPipelinesMap;
    RefPtr<PlaybackPipeline> m_playbackPipeline;
    RefPtr<MediaSourceClientGStreamerMSE> m_mediaSourceClient;
};

class GStreamerMediaSample : public MediaSample
{
private:
    MediaTime m_pts, m_dts, m_duration;
    AtomicString m_trackID;
    size_t m_size;
    GstSample* m_sample;
    FloatSize m_presentationSize;
    MediaSample::SampleFlags m_flags;

    GStreamerMediaSample(GstSample* sample, const FloatSize& presentationSize, const AtomicString& trackID);

public:
    static PassRefPtr<GStreamerMediaSample> create(GstSample* sample, const FloatSize& presentationSize, const AtomicString& trackID);
    static PassRefPtr<GStreamerMediaSample> createFakeSample(GstCaps* caps, MediaTime pts, MediaTime dts, MediaTime duration, const FloatSize& presentationSize, const AtomicString& trackID);

    virtual ~GStreamerMediaSample();

    MediaTime presentationTime() const { return m_pts; }
    MediaTime decodeTime() const { return m_dts; }
    MediaTime duration() const { return m_duration; }
    AtomicString trackID() const { return m_trackID; }
    size_t sizeInBytes() const { return m_size; }
    GstSample* sample() const { return m_sample; }
    FloatSize presentationSize() const { return m_presentationSize; }
    void offsetTimestampsBy(const MediaTime&) { }
    void setTimestamps(const MediaTime&, const MediaTime&) { }
    SampleFlags flags() const { return m_flags; }
    PlatformSample platformSample() { return PlatformSample(); }
    void dump(PrintStream&) const {}
};

class ContentType;
class SourceBufferPrivateGStreamer;

class MediaSourceClientGStreamerMSE: public RefCounted<MediaSourceClientGStreamerMSE> {
    public:
        static PassRefPtr<MediaSourceClientGStreamerMSE> create(MediaPlayerPrivateGStreamerMSE* playerPrivate);
        virtual ~MediaSourceClientGStreamerMSE();

        // From MediaSourceGStreamer
        MediaSourcePrivate::AddStatus addSourceBuffer(RefPtr<SourceBufferPrivateGStreamer>, const ContentType&);
        void durationChanged(const MediaTime&);
        void markEndOfStream(MediaSourcePrivate::EndOfStreamStatus);

        // From SourceBufferPrivateGStreamer
        void abort(PassRefPtr<SourceBufferPrivateGStreamer>);
        bool append(PassRefPtr<SourceBufferPrivateGStreamer>, const unsigned char*, unsigned);
        void removedFromMediaSource(RefPtr<SourceBufferPrivateGStreamer>);
        void flushAndEnqueueNonDisplayingSamples(Vector<RefPtr<MediaSample> > samples);
        void enqueueSample(PassRefPtr<MediaSample> sample);

        // From our WebKitMediaSrc
        // FIXME: these can be removed easily by directly invoking methods on SourceBufferPrivateGStreamer
        void didReceiveInitializationSegment(SourceBufferPrivateGStreamer*, const SourceBufferPrivateClient::InitializationSegment&);
        void didReceiveAllPendingSamples(SourceBufferPrivateGStreamer* sourceBuffer);

        void clearPlayerPrivate();

        MediaTime duration();
        GRefPtr<WebKitMediaSrc> webKitMediaSrc();

    private:
        MediaSourceClientGStreamerMSE(MediaPlayerPrivateGStreamerMSE* playerPrivate);

        // Would better be a RefPtr, but the playerprivate is a unique_ptr, so
        // we can't mess with references here. In exchange, the playerprivate
        // must notify us when it's being destroyed, so we can clear our pointer.
        MediaPlayerPrivateGStreamerMSE* m_playerPrivate;
        MediaTime m_duration;
};

} // namespace WebCore

#endif // USE(GSTREAMER)
#endif
