/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010, 2011, 2012, 2013 Igalia S.L
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

#include "config.h"
#include "MediaPlayerPrivateGStreamerMSE.h"

#if ENABLE(VIDEO) && USE(GSTREAMER) && ENABLE(MEDIA_SOURCE) && ENABLE(VIDEO_TRACK)

#include "GStreamerUtilities.h"
#include "URL.h"
#include "MIMETypeRegistry.h"
#include "MediaDescription.h"
#include "MediaPlayer.h"
#include "MediaPlayerRequestInstallMissingPluginsCallback.h"
#include "NotImplemented.h"
#include "SecurityOrigin.h"
#include "TimeRanges.h"
#include "UUID.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <limits>
#include <wtf/CurrentTime.h>
#include <wtf/HexNumber.h>
#include <wtf/MediaTime.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>


#include "AudioTrackPrivateGStreamer.h"
#include "InbandMetadataTextTrackPrivateGStreamer.h"
#include "InbandTextTrackPrivateGStreamer.h"
#include "SourceBufferPrivateGStreamer.h"
#include "TextCombinerGStreamer.h"
#include "TextSinkGStreamer.h"
#include "VideoTrackPrivateGStreamer.h"

#if USE(GSTREAMER_MPEGTS)
#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>
#undef GST_USE_UNSTABLE_API
#endif
#include <gst/audio/streamvolume.h>

#include "MediaSource.h"

#if ENABLE(WEB_AUDIO)
#include "AudioSourceProviderGStreamer.h"
#endif

#include <wtf/MainThread.h>

static const char* dumpReadyState(WebCore::MediaPlayer::ReadyState readyState)
{
    switch (readyState) {
    case WebCore::MediaPlayer::HaveNothing: return "HaveNothing";
    case WebCore::MediaPlayer::HaveMetadata: return "HaveMetadata";
    case WebCore::MediaPlayer::HaveCurrentData: return "HaveCurrentData";
    case WebCore::MediaPlayer::HaveFutureData: return "HaveFutureData";
    case WebCore::MediaPlayer::HaveEnoughData: return "HaveEnoughData";
    default: return "(unknown)";
    }
}

// Max interval in seconds to stay in the READY state on manual
// state change requests.
static const unsigned gReadyStateTimerInterval = 60;

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

class AppendPipeline : public ThreadSafeRefCounted<AppendPipeline> {
public:
    enum AppendStage { Invalid, NotStarted, Ongoing, KeyNegotiation, NoDataToDecode, Sampling, LastSample, Aborting };

    static const unsigned int s_noDataToDecodeTimeoutMsec = 1000;
    static const unsigned int s_lastSampleTimeoutMsec = 100;

    AppendPipeline(PassRefPtr<MediaSourceClientGStreamerMSE> mediaSourceClient, PassRefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate, MediaPlayerPrivateGStreamerMSE* playerPrivate);
    virtual ~AppendPipeline();

    void handleElementMessage(GstMessage*);

    gint id();
    AppendStage appendStage() { return m_appendStage; }
    void setAppendStage(AppendStage newAppendStage);

    GstFlowReturn handleNewSample(GstElement* appsink);

    // Takes ownership of caps.
    void parseDemuxerCaps(GstCaps* demuxerSrcPadCaps);
    void appSinkCapsChanged();
    void appSinkNewSample(GstSample* sample);
    void appSinkEOS();
    void didReceiveInitializationSegment();
    AtomicString trackId();
    void abort();

    void clearPlayerPrivate();
    RefPtr<MediaSourceClientGStreamerMSE> mediaSourceClient() { return m_mediaSourceClient; }
    RefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate() { return m_sourceBufferPrivate; }
    GstElement* pipeline() { return m_pipeline; }
    GstElement* appsrc() { return m_appsrc; }
    GstCaps* demuxerSrcPadCaps() { return m_demuxerSrcPadCaps; }
    GstCaps* appSinkCaps() { return m_appSinkCaps; }
    RefPtr<WebCore::TrackPrivateBase> track() { return m_track; }
    WebCore::MediaSourceStreamTypeGStreamer streamType() { return m_streamType; }

    void disconnectFromAppSinkFromAnyThread();
    void disconnectFromAppSink();
    void connectToAppSinkFromAnyThread(GstPad* demuxersrcpad);
    void connectToAppSink(GstPad* demuxersrcpad);

private:
    void resetPipeline();

// TODO: Hide everything and use getters/setters.
private:
    RefPtr<MediaSourceClientGStreamerMSE> m_mediaSourceClient;
    RefPtr<SourceBufferPrivateGStreamer> m_sourceBufferPrivate;
    MediaPlayerPrivateGStreamerMSE* m_playerPrivate;

    // (m_mediaType, m_id) is unique.
    gint m_id;

    GstFlowReturn m_flowReturn;

    GstElement* m_pipeline;
    GstElement* m_appsrc;
    GstElement* m_typefind;
    GstElement* m_qtdemux;

    GstElement* m_decryptor;

    // The demuxer has one src Stream only.
    GstElement* m_appsink;

    GMutex m_newSampleMutex;
    GCond m_newSampleCondition;
    GMutex m_padRemovedMutex;
    GCond m_padRemovedCondition;

    GstCaps* m_appSinkCaps;
    GstCaps* m_demuxerSrcPadCaps;
    FloatSize m_presentationSize;

    // Some appended data are only headers and don't generate any
    // useful stream data for decoding. This is detected with a
    // timeout and reported to the upper layers, so update/updateend
    // can be generated and the append operation doesn't block.
    guint m_noDataToDecodeTimeoutTag;

    // Used to detect the last sample. Rescheduled each time a new
    // sample arrives.
    guint m_lastSampleTimeoutTag;

    // Keeps track of the stages of append processing, to avoid
    // performing actions inappropriate for the current stage (eg:
    // processing more samples when the last one has been detected
    // or the noDataToDecodeTimeout has been triggered).
    // See setAppendStage() for valid transitions.
    AppendStage m_appendStage;

    // Aborts can only be completed when the normal sample detection
    // has finished. Meanwhile, the willing to abort is expressed in
    // this field.
    bool m_abortPending;

    WebCore::MediaSourceStreamTypeGStreamer m_streamType;
    RefPtr<WebCore::TrackPrivateBase> m_oldTrack;
    RefPtr<WebCore::TrackPrivateBase> m_track;
};

static gboolean mediaPlayerPrivateMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamerMSE* player)
{
    return player->handleMessage(message);
}

static void mediaPlayerPrivateSourceChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->sourceChanged();
}

static void mediaPlayerPrivateVideoSinkCapsChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->videoCapsChanged();
}

static void mediaPlayerPrivateVideoChangedCallback(GObject*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->videoChanged();
}

static void mediaPlayerPrivateAudioChangedCallback(GObject*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->audioChanged();
}

static void setAudioStreamPropertiesCallback(GstChildProxy*, GObject* object, gchar*,
    MediaPlayerPrivateGStreamerMSE* player)
{
    player->setAudioStreamProperties(object);
}

static void mediaPlayerPrivateTextChangedCallback(GObject*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->textChanged();
}

static GstFlowReturn mediaPlayerPrivateNewTextSampleCallback(GObject*, MediaPlayerPrivateGStreamerMSE* player)
{
    player->newTextSample();
    return GST_FLOW_OK;
}

void MediaPlayerPrivateGStreamerMSE::setAudioStreamProperties(GObject* object)
{
    if (g_strcmp0(G_OBJECT_TYPE_NAME(object), "GstPulseSink"))
        return;

    const char* role = m_player->client().mediaPlayerIsVideo() ? "video" : "music";
    GstStructure* structure = gst_structure_new("stream-properties", "media.role", G_TYPE_STRING, role, NULL);
    g_object_set(object, "stream-properties", structure, NULL);
    gst_structure_free(structure);
    GUniquePtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(object)));
    LOG_MEDIA_MESSAGE("Set media.role as %s at %s", role, elementName.get());
}

void MediaPlayerPrivateGStreamerMSE::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
         registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateGStreamerMSE>(player); },
            getSupportedTypes, supportsType, 0, 0, 0, supportsKeySystem);
}

bool initializeGStreamerAndRegisterWebKitMESElement()
{
    if (!initializeGStreamer())
        return false;

    registerWebKitGStreamerElements();

    GRefPtr<GstElementFactory> WebKitMediaSrcFactory = gst_element_factory_find("webkitmediasrc");
    if (!WebKitMediaSrcFactory)
        gst_element_register(0, "webkitmediasrc", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_SRC);
    return true;
}

bool MediaPlayerPrivateGStreamerMSE::isAvailable()
{
    if (!initializeGStreamerAndRegisterWebKitMESElement())
        return false;

    GRefPtr<GstElementFactory> factory = gst_element_factory_find("playbin");
    return factory;
}

MediaPlayerPrivateGStreamerMSE::MediaPlayerPrivateGStreamerMSE(MediaPlayer* player)
    : MediaPlayerPrivateGStreamerBase(player)
    , m_webKitMediaSrc(0)
    , m_seekTime(0)
    , m_changingRate(false)
    , m_endTime(numeric_limits<float>::infinity())
    , m_isStreaming(false)
    , m_mediaLocations(0)
    , m_mediaLocationCurrentIndex(0)
    , m_resetPipeline(false)
    , m_paused(true)
    , m_playbackRatePause(false)
    , m_seeking(false)
    , m_seekIsPending(false)
    , m_timeOfOverlappingSeek(-1)
    , m_canFallBackToLastFinishedSeekPosition(false)
    , m_buffering(false)
    , m_playbackRate(1)
    , m_lastPlaybackRate(1)
    , m_errorOccured(false)
    , m_mediaDuration(0)
    , m_downloadFinished(false)
    , m_fillTimer(*this, &MediaPlayerPrivateGStreamerMSE::fillTimerFired)
    , m_maxTimeLoaded(0)
    , m_bufferingPercentage(0)
    , m_preload(player->preload())
    , m_delayingLoad(false)
    , m_mediaDurationKnown(true)
    , m_maxTimeLoadedAtLastDidLoadingProgress(0)
    , m_volumeAndMuteInitialized(false)
    , m_hasVideo(false)
    , m_hasAudio(false)
    , m_audioTimerHandler("[WebKit] MediaPlayerPrivateGStreamerMSE::audioChanged", std::bind(&MediaPlayerPrivateGStreamerMSE::notifyPlayerOfAudio, this))
    , m_textTimerHandler("[WebKit] MediaPlayerPrivateGStreamerMSE::textChanged", std::bind(&MediaPlayerPrivateGStreamerMSE::notifyPlayerOfText, this))
    , m_videoTimerHandler("[WebKit] MediaPlayerPrivateGStreamerMSE::videoChanged", std::bind(&MediaPlayerPrivateGStreamerMSE::notifyPlayerOfVideo, this))
    , m_videoCapsTimerHandler("[WebKit] MediaPlayerPrivateGStreamerMSE::videoCapsChanged", std::bind(&MediaPlayerPrivateGStreamerMSE::notifyPlayerOfVideoCaps, this))
    , m_readyTimerHandler("[WebKit] mediaPlayerPrivateReadyStateTimeoutCallback", [this] { changePipelineState(GST_STATE_NULL); })
    , m_totalBytes(0)
    , m_preservesPitch(false)
    , m_cachedPosition(-1)
    , m_lastQuery(-1)
#if ENABLE(WEB_AUDIO)
    , m_audioSourceProvider(std::make_unique<AudioSourceProviderGStreamer>())
#endif
    , m_requestedState(GST_STATE_VOID_PENDING)
    , m_pendingAsyncOperations(nullptr)
    , m_seekCompleted(true)
{
    LOG_MEDIA_MESSAGE("%p", this);
}

MediaPlayerPrivateGStreamerMSE::~MediaPlayerPrivateGStreamerMSE()
{
    LOG_MEDIA_MESSAGE("destroying the player");

    for (HashMap<RefPtr<SourceBufferPrivateGStreamer>, RefPtr<AppendPipeline> >::iterator it = m_appendPipelinesMap.begin(); it != m_appendPipelinesMap.end(); ++it)
        it->value->clearPlayerPrivate();

    for (size_t i = 0; i < m_audioTracks.size(); ++i)
        m_audioTracks[i]->disconnect();

    for (size_t i = 0; i < m_textTracks.size(); ++i)
        m_textTracks[i]->disconnect();

    for (size_t i = 0; i < m_videoTracks.size(); ++i)
        m_videoTracks[i]->disconnect();

    if (m_fillTimer.isActive())
        m_fillTimer.stop();

    if (m_mediaLocations) {
        gst_structure_free(m_mediaLocations);
        m_mediaLocations = 0;
    }

    if (m_autoAudioSink)
        g_signal_handlers_disconnect_by_func(G_OBJECT(m_autoAudioSink.get()),
            reinterpret_cast<gpointer>(setAudioStreamPropertiesCallback), this);

    m_readyTimerHandler.cancel();
    if (m_missingPluginsCallback) {
        m_missingPluginsCallback->invalidate();
        m_missingPluginsCallback = nullptr;
    }

    // Ensure that neither this class nor the base class hold references to any sample
    // as in the change to NULL the decoder needs to be able to free the buffers
    clearSamples();

    if (m_videoSink) {
        GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_videoSink.get(), "sink"));
        g_signal_handlers_disconnect_by_func(videoSinkPad.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);
    }

    if (m_webKitMediaSrc) {
        webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_webKitMediaSrc.get()), 0);
        g_signal_handlers_disconnect_by_func(m_webKitMediaSrc.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_webKitMediaSrc.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_webKitMediaSrc.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);
    }

    if (m_pipeline) {
        GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateMessageCallback), this);
        gst_bus_remove_signal_watch(bus.get());

        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateSourceChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateNewTextSampleCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);

        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    }

    // Cancel pending mediaPlayerPrivateNotifyDurationChanged() delayed calls
    m_pendingAsyncOperationsLock.lock();
    while (m_pendingAsyncOperations) {
        g_source_remove(GPOINTER_TO_UINT(m_pendingAsyncOperations->data));
        m_pendingAsyncOperations = g_list_remove(m_pendingAsyncOperations, m_pendingAsyncOperations->data);
    }
    m_pendingAsyncOperationsLock.unlock();
}

void MediaPlayerPrivateGStreamerMSE::load(const String& urlString)
{
    if (!initializeGStreamerAndRegisterWebKitMESElement())
        return;

    if (!urlString.startsWith("mediasource")) {
        LOG_MEDIA_MESSAGE("Unsupported url: %s", urlString.utf8().data());
        return;
    }

    URL url(URL(), urlString);
    if (url.isBlankURL())
        return;

    // Clean out everything after file:// url path.
    String cleanURL(urlString);
    if (url.isLocalFile())
        cleanURL = cleanURL.substring(0, url.pathEnd());

    if (!m_pipeline) {
        createGSTPlayBin();
        m_playbackPipeline = PlaybackPipeline::create();
    }

    ASSERT(m_pipeline);

    m_url = URL(URL(), cleanURL);
    g_object_set(m_pipeline.get(), "uri", cleanURL.utf8().data(), nullptr);

    INFO_MEDIA_MESSAGE("Load %s", cleanURL.utf8().data());

    if (m_preload == MediaPlayer::None) {
        LOG_MEDIA_MESSAGE("Delaying load.");
        m_delayingLoad = true;
    }

    // Reset network and ready states. Those will be set properly once
    // the pipeline pre-rolled.
    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();
    m_readyState = MediaPlayer::HaveNothing;
    LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));

    m_player->readyStateChanged();
    m_volumeAndMuteInitialized = false;

    if (!m_delayingLoad)
        commitLoad();
}

void MediaPlayerPrivateGStreamerMSE::load(const String& url, MediaSourcePrivateClient* mediaSource)
{
    String mediasourceUri = String::format("mediasource%s", url.utf8().data());
    m_mediaSource = mediaSource;
    load(mediasourceUri);
}

#if ENABLE(MEDIA_STREAM)
void MediaPlayerPrivateGStreamerMSE::load(MediaStreamPrivate&)
{
    notImplemented();
}
#endif

void MediaPlayerPrivateGStreamerMSE::commitLoad()
{
    ASSERT(!m_delayingLoad);
    LOG_MEDIA_MESSAGE("Committing load.");

    // GStreamer needs to have the pipeline set to a paused state to
    // start providing anything useful.
    changePipelineState(GST_STATE_PAUSED);

    updateStates();
}

float MediaPlayerPrivateGStreamerMSE::playbackPosition() const
{
    if (m_isEndReached) {
        // Position queries on a null pipeline return 0. If we're at
        // the end of the stream the pipeline is null but we want to
        // report either the seek time or the duration because this is
        // what the Media element spec expects us to do.
        if (m_seeking)
            return m_seekTime;
        if (m_mediaDuration)
            return m_mediaDuration;
        return 0;
    }

    double now = WTF::currentTime();
    if (m_lastQuery > -1 && ((now - m_lastQuery) < 0.25) && (m_cachedPosition > -1))
        return m_cachedPosition;

    m_lastQuery = now;

    // Position is only available if no async state change is going on and the state is either paused or playing.
    gint64 position = GST_CLOCK_TIME_NONE;
    GstQuery* query= gst_query_new_position(GST_FORMAT_TIME);
    if (gst_element_query(m_pipeline.get(), query))
        gst_query_parse_position(query, 0, &position);

    float result = 0.0f;
    if (static_cast<GstClockTime>(position) != GST_CLOCK_TIME_NONE)
        result = static_cast<double>(position) / GST_SECOND;
    else if (m_canFallBackToLastFinishedSeekPosition)
        result = m_seekTime;

    LOG_MEDIA_MESSAGE("Position %" GST_TIME_FORMAT, GST_TIME_ARGS(position));

    gst_query_unref(query);
    m_cachedPosition = result;
    return result;
}

bool MediaPlayerPrivateGStreamerMSE::changePipelineState(GstState newState)
{
    ASSERT(m_pipeline);

    GstState currentState;
    GstState pending;

    gst_element_get_state(m_pipeline.get(), &currentState, &pending, 0);
    if (currentState == newState || pending == newState) {
        LOG_MEDIA_MESSAGE("Rejected state change to %s from %s with %s pending", gst_element_state_get_name(newState),
            gst_element_state_get_name(currentState), gst_element_state_get_name(pending));
        return true;
    }

    LOG_MEDIA_MESSAGE("Changing state change to %s from %s with %s pending", gst_element_state_get_name(newState),
        gst_element_state_get_name(currentState), gst_element_state_get_name(pending));

    GstStateChangeReturn setStateResult = gst_element_set_state(m_pipeline.get(), newState);
    GstState pausedOrPlaying = newState == GST_STATE_PLAYING ? GST_STATE_PAUSED : GST_STATE_PLAYING;
    if (currentState != pausedOrPlaying && setStateResult == GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    // Create a timer when entering the READY state so that we can free resources
    // if we stay for too long on READY.
    // Also lets remove the timer if we request a state change for any state other than READY.
    // See also https://bugs.webkit.org/show_bug.cgi?id=117354
    if (newState == GST_STATE_READY && !m_readyTimerHandler.isScheduled()) {
        m_readyTimerHandler.schedule(std::chrono::seconds(gReadyStateTimerInterval));
    } else if (newState != GST_STATE_READY && m_readyTimerHandler.isScheduled()) {
        m_readyTimerHandler.cancel();
    }

    return true;
}

void MediaPlayerPrivateGStreamerMSE::prepareToPlay()
{
    m_preload = MediaPlayer::Auto;
    if (m_delayingLoad) {
        m_delayingLoad = false;
        commitLoad();
    }
}

void MediaPlayerPrivateGStreamerMSE::play()
{
    if (!m_playbackRate) {
        m_playbackRatePause = true;
        return;
    }

    if (changePipelineState(GST_STATE_PLAYING)) {
        m_isEndReached = false;
        m_delayingLoad = false;
        m_preload = MediaPlayer::Auto;
        LOG_MEDIA_MESSAGE("Play");
    } else {
        loadingFailed(MediaPlayer::Empty);
    }
}

void MediaPlayerPrivateGStreamerMSE::pause()
{
    m_playbackRatePause = false;
    GstState currentState, pendingState;
    gst_element_get_state(m_pipeline.get(), &currentState, &pendingState, 0);
    if (currentState < GST_STATE_PAUSED && pendingState <= GST_STATE_PAUSED)
        return;

    if (changePipelineState(GST_STATE_PAUSED))
        INFO_MEDIA_MESSAGE("Pause");
    else
        loadingFailed(MediaPlayer::Empty);
}

float MediaPlayerPrivateGStreamerMSE::duration() const
{
    if (!m_pipeline)
        return 0.0f;

    if (m_errorOccured)
        return 0.0f;

    return m_mediaDuration;
}

float MediaPlayerPrivateGStreamerMSE::currentTime() const
{
    if (!m_pipeline) {
        LOG_MEDIA_MESSAGE("(NO PIPELINE) %f", 0.0f);
        return 0.0f;
    }

    if (m_errorOccured) {
        LOG_MEDIA_MESSAGE("(ERROR) %f", 0.0f);
        return 0.0f;
    }

    // Workaround for
    // https://bugzilla.gnome.org/show_bug.cgi?id=639941 In GStreamer
    // 0.10.35 basesink reports wrong duration in case of EOS and
    // negative playback rate. There's no upstream accepted patch for
    // this bug yet, hence this temporary workaround.
    if (m_isEndReached && m_playbackRate < 0) {
        LOG_MEDIA_MESSAGE("(END OR NEGATIVE) %f", 0.0f);
        return 0.0f;
    }

    float playpos = playbackPosition();
    // LOG_MEDIA_MESSAGE("(PLAYBACK POSITION) %f", playpos);
    return playpos;
}

void MediaPlayerPrivateGStreamerMSE::seek(float time)
{
    if (!m_pipeline)
        return;

    if (m_errorOccured)
        return;

    INFO_MEDIA_MESSAGE("[Seek] seek attempt to %f secs", time);

    // Avoid useless seeking.
    if (time == currentTime())
        return;

    if (isLiveStream())
        return;

    GstClockTime clockTime = toGstClockTime(time);
    INFO_MEDIA_MESSAGE("[Seek] seeking to %" GST_TIME_FORMAT " (%f)", GST_TIME_ARGS(clockTime), time);

    if (m_seeking) {
        m_timeOfOverlappingSeek = time;
        if (m_seekIsPending) {
            m_seekTime = time;
            return;
        }
    }

    GstState state;
    GstState newState;
    GstStateChangeReturn getStateResult = gst_element_get_state(m_pipeline.get(), &state, &newState, 0);
    if (getStateResult == GST_STATE_CHANGE_FAILURE || getStateResult == GST_STATE_CHANGE_NO_PREROLL) {
        LOG_MEDIA_MESSAGE("[Seek] cannot seek, current state change is %s", gst_element_state_change_return_get_name(getStateResult));
        return;
    }

    if (getStateResult == GST_STATE_CHANGE_ASYNC || state < GST_STATE_PAUSED
            || m_isEndReached) {
        CString reason = "Unknown reason";
        if (getStateResult == GST_STATE_CHANGE_ASYNC) reason = String::format("In async change %s --> %s", gst_element_state_get_name(state), gst_element_state_get_name(newState)).utf8();
        else if (state < GST_STATE_PAUSED) reason = "State less than PAUSED";
        else if (m_isEndReached) reason = "End reached";

        LOG_MEDIA_MESSAGE("Delaying the seek: %s", reason.data());
        m_seekIsPending = true;
        if (m_isEndReached) {
            LOG_MEDIA_MESSAGE("[Seek] reset pipeline");
            m_resetPipeline = true;
            if (!changePipelineState(GST_STATE_PAUSED))
                loadingFailed(MediaPlayer::Empty);
        }
    } else {
        LOG_MEDIA_MESSAGE("We can seek now");
        if (!doSeek(clockTime, m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE))) {
            LOG_MEDIA_MESSAGE("Seeking to %f failed", time);
            return;
        }
    }

    m_seeking = true;
    m_seekTime = time;
    m_isEndReached = false;
    LOG_MEDIA_MESSAGE("m_seeking=%s, m_seekTime=%f", m_seeking?"true":"false", m_seekTime);
}

static gboolean dumpPipeline(gpointer data)
{
    GstElement* pipeline = reinterpret_cast<GstElement*>(data);

    g_printerr("Dumping pipeline\n");
    CString dotFileName = "pipeline-dump";
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.data());

    return G_SOURCE_REMOVE;
}

void MediaPlayerPrivateGStreamerMSE::notifySeekNeedsData(const MediaTime& seekTime)
{
    LOG_MEDIA_MESSAGE("%s", "");

    // Reenqueue samples needed to resume playback in the new position
    m_mediaSource->seekToTime(seekTime);

    LOG_MEDIA_MESSAGE("MSE seek to %f finished", seekTime.toDouble());
}

bool MediaPlayerPrivateGStreamerMSE::doSeek(gint64 position, float rate, GstSeekFlags seekType)
{
    LOG_MEDIA_MESSAGE("%s", "");

    gint64 startTime, endTime;

    if (rate > 0) {
        startTime = position;
        endTime = GST_CLOCK_TIME_NONE;
    } else {
        startTime = 0;
        // If we are at beginning of media, start from the end to
        // avoid immediate EOS.
        if (position < 0)
            endTime = static_cast<gint64>(duration() * GST_SECOND);
        else
            endTime = position;
    }

    if (!rate)
        rate = 1.0;

    LOG_MEDIA_MESSAGE("Actual seek to %" GST_TIME_FORMAT ", end time:  %" GST_TIME_FORMAT ", rate: %f", GST_TIME_ARGS(startTime), GST_TIME_ARGS(endTime), rate);

    MediaTime time(MediaTime::createWithDouble(double(static_cast<double>(position) / GST_SECOND)));

    // DEBUG
    {
        float currentTime = 0.0f;
        gint64 pos = GST_CLOCK_TIME_NONE;
        GstQuery* query= gst_query_new_position(GST_FORMAT_TIME);
        if (gst_element_query(m_pipeline.get(), query))
            gst_query_parse_position(query, 0, &pos);
        currentTime = static_cast<double>(pos) / GST_SECOND;
        LOG_MEDIA_MESSAGE("seekTime=%f, currentTime=%f", time.toFloat(), currentTime);
    }

    // This will call notifySeekNeedsData() after some time to tell that the pipeline is ready for sample enqueuing.
    webkit_media_src_prepare_seek(WEBKIT_MEDIA_SRC(m_webKitMediaSrc.get()), time);

    // DEBUG
    dumpPipeline(m_pipeline.get());

    if (!gst_element_seek(m_pipeline.get(), rate, GST_FORMAT_TIME, seekType,
        GST_SEEK_TYPE_SET, startTime, GST_SEEK_TYPE_SET, endTime)) {
        LOG_MEDIA_MESSAGE("Returning false");
        return false;
    }

    // The samples will be enqueued in notifySeekNeedsData()
    LOG_MEDIA_MESSAGE("Returning true");
    return true;
}

void MediaPlayerPrivateGStreamerMSE::updatePlaybackRate()
{
    if (!m_changingRate)
        return;

    LOG_MEDIA_MESSAGE("%s", "Not implemented");

    // Not implemented
    return;
}

bool MediaPlayerPrivateGStreamerMSE::paused() const
{
    if (m_isEndReached) {
        LOG_MEDIA_MESSAGE("Ignoring pause at EOS");
        return true;
    }

    if (m_playbackRatePause)
        return false;

    GstState state;
    gst_element_get_state(m_pipeline.get(), &state, nullptr, 0);
    return state <= GST_STATE_PAUSED;
}

bool MediaPlayerPrivateGStreamerMSE::seeking() const
{
    return m_seeking && !m_seekCompleted;
}

void MediaPlayerPrivateGStreamerMSE::videoChanged()
{
    if (isMainThread())
        notifyPlayerOfVideo();
    else
        m_videoTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamerMSE::videoCapsChanged()
{
    m_videoCapsTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamerMSE::notifyPlayerOfVideo()
{
    if (!m_pipeline || !m_webKitMediaSrc)
        return;

    gint numTracks = 0;
    GstElement* element = m_webKitMediaSrc.get();
    g_object_get(element, "n-video", &numTracks, nullptr);

    LOG_MEDIA_MESSAGE("Stream has %d video tracks", numTracks);
    m_hasVideo = numTracks > 0;

    LOG_MEDIA_MESSAGE("Tracks managed by source element. Bailing out now.");
    m_player->client().mediaPlayerEngineUpdated(m_player);
}

void MediaPlayerPrivateGStreamerMSE::notifyPlayerOfVideoCaps()
{
    m_videoSize = IntSize();
    m_player->client().mediaPlayerEngineUpdated(m_player);
}

void MediaPlayerPrivateGStreamerMSE::audioChanged()
{
    if (isMainThread())
        notifyPlayerOfAudio();
    else
        m_audioTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamerMSE::notifyPlayerOfAudio()
{
    if (!m_pipeline || !m_webKitMediaSrc)
        return;

    gint numTracks = 0;
    GstElement* element = m_webKitMediaSrc.get();
    m_hasAudio = numTracks > 0;

    g_object_get(element, "n-audio", &numTracks, nullptr);

    LOG_MEDIA_MESSAGE("Stream has %d audio tracks", numTracks);
    m_hasAudio = numTracks > 0;

    LOG_MEDIA_MESSAGE("Tracks managed by source element. Bailing out now.");
    m_player->client().mediaPlayerEngineUpdated(m_player);
    return;
}

void MediaPlayerPrivateGStreamerMSE::textChanged()
{
    if (isMainThread())
        notifyPlayerOfText();
    else
        m_textTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamerMSE::notifyPlayerOfText()
{
    if (!m_pipeline || !m_webKitMediaSrc)
        return;

    gint numTracks = 0;
    GstElement* element = m_webKitMediaSrc.get();
    g_object_get(element, "n-text", &numTracks, nullptr);

    LOG_MEDIA_MESSAGE("Stream has %d text tracks", numTracks);

    LOG_MEDIA_MESSAGE("Tracks managed by source element. Bailing out now.");
}

void MediaPlayerPrivateGStreamerMSE::newTextSample()
{
    if (!m_textAppSink)
        return;

    GRefPtr<GstEvent> streamStartEvent = adoptGRef(
        gst_pad_get_sticky_event(m_textAppSinkPad.get(), GST_EVENT_STREAM_START, 0));

    GRefPtr<GstSample> sample;
    g_signal_emit_by_name(m_textAppSink.get(), "pull-sample", &sample.outPtr(), NULL);
    ASSERT(sample);

    if (streamStartEvent) {
        bool found = FALSE;
        const gchar* id;
        gst_event_parse_stream_start(streamStartEvent.get(), &id);
        for (size_t i = 0; i < m_textTracks.size(); ++i) {
            RefPtr<InbandTextTrackPrivateGStreamer> track = m_textTracks[i];
            if (track->streamId() == id) {
                track->handleSample(sample);
                found = true;
                break;
            }
        }
        if (!found)
            WARN_MEDIA_MESSAGE("Got sample with unknown stream ID.");
    } else
        WARN_MEDIA_MESSAGE("Unable to handle sample with no stream start event.");
}

// METRO FIXME: GStreamer mediaplayer manages the readystate on its own. We shouldn't change it manually.
void MediaPlayerPrivateGStreamerMSE::setReadyState(MediaPlayer::ReadyState state)
{
    // FIXME: early return here.
    if (state != m_readyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed manually from %u to %u", m_readyState, state);
        MediaPlayer::ReadyState oldReadyState = m_readyState;
        m_readyState = state;
        LOG_MEDIA_MESSAGE("m_readyState: %s -> %s", dumpReadyState(oldReadyState), dumpReadyState(m_readyState));

        GstState state;
        GstStateChangeReturn getStateResult = gst_element_get_state(m_pipeline.get(), &state, NULL, 250 * GST_NSECOND);
        bool isPlaying = (getStateResult == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PLAYING);

        if (m_readyState == MediaPlayer::HaveMetadata && oldReadyState > MediaPlayer::HaveMetadata && isPlaying) {
            LOG_MEDIA_MESSAGE("Changing pipeline to PAUSED...");
            // Not using changePipelineState() because we don't want the state to drop to GST_STATE_NULL ever.
            bool ok = gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED) == GST_STATE_CHANGE_SUCCESS;
            LOG_MEDIA_MESSAGE("Changing pipeline to PAUSED: %s", (ok)?"OK":"ERROR");
        }
        m_player->readyStateChanged();
    }
}

void MediaPlayerPrivateGStreamerMSE::waitForSeekCompleted()
{
    if (!m_seeking)
        return;

    LOG_MEDIA_MESSAGE("Waiting for MSE seek completed");
    m_seekCompleted = false;
}

void MediaPlayerPrivateGStreamerMSE::seekCompleted()
{
    LOG_MEDIA_MESSAGE("%s", "");

    if (m_seekCompleted)
        return;

    LOG_MEDIA_MESSAGE("MSE seek completed");
    m_seekCompleted = true;

    if (!seeking() && m_readyState >= MediaPlayer::HaveFutureData)
        gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);

    if (!m_seeking)
        m_player->timeChanged();
}

void MediaPlayerPrivateGStreamerMSE::setRate(float rate)
{
    notImplemented();
    return;

    // Higher rate causes crash.
    rate = clampTo(rate, -20.0, 20.0);

    // Avoid useless playback rate update.
    if (m_playbackRate == rate) {
        // and make sure that upper layers were notified if rate was set

        if (!m_changingRate && m_player->rate() != m_playbackRate)
            m_player->rateChanged();
        return;
    }

    if (isLiveStream()) {
        // notify upper layers that we cannot handle passed rate.
        m_changingRate = false;
        m_player->rateChanged();
        return;
    }

    GstState state;
    GstState pending;

    m_playbackRate = rate;
    m_changingRate = true;

    gst_element_get_state(m_pipeline.get(), &state, &pending, 0);

    if (!rate) {
        m_changingRate = false;
        m_playbackRatePause = true;
        if (state != GST_STATE_PAUSED && pending != GST_STATE_PAUSED)
            changePipelineState(GST_STATE_PAUSED);
        return;
    }

    if ((state != GST_STATE_PLAYING && state != GST_STATE_PAUSED)
        || (pending == GST_STATE_PAUSED))
        return;

    updatePlaybackRate();
}

double MediaPlayerPrivateGStreamerMSE::rate() const
{
    return m_playbackRate;
}

void MediaPlayerPrivateGStreamerMSE::setPreservesPitch(bool preservesPitch)
{
    m_preservesPitch = preservesPitch;
}

std::unique_ptr<PlatformTimeRanges> MediaPlayerPrivateGStreamerMSE::buffered() const
{
    return m_mediaSource->buffered();
}

gboolean MediaPlayerPrivateGStreamerMSE::handleMessage(GstMessage* message)
{
    GUniqueOutPtr<GError> err;
    GUniqueOutPtr<gchar> debug;
    MediaPlayer::NetworkState error;
    bool issueError = true;
    bool attemptNextLocation = false;
    const GstStructure* structure = gst_message_get_structure(message);
    GstState requestedState, currentState;

    m_canFallBackToLastFinishedSeekPosition = false;

    if (structure) {
        const gchar* messageTypeName = gst_structure_get_name(structure);

        // Redirect messages are sent from elements, like qtdemux, to
        // notify of the new location(s) of the media.
        if (!g_strcmp0(messageTypeName, "redirect")) {
            mediaLocationChanged(message);
            return TRUE;
        }
    }

    // We ignore state changes from internal elements. They are forwarded to playbin2 anyway.
    bool messageSourceIsPlaybin = GST_MESSAGE_SRC(message) == reinterpret_cast<GstObject*>(m_pipeline.get());

    LOG_MEDIA_MESSAGE("Message %s received from element %s", GST_MESSAGE_TYPE_NAME(message), GST_MESSAGE_SRC_NAME(message));

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
        if (m_resetPipeline)
            break;
        if (m_missingPluginsCallback)
            break;
        gst_message_parse_error(message, &err.outPtr(), &debug.outPtr());
        ERROR_MEDIA_MESSAGE("Error %d: %s (url=%s)", err->code, err->message, m_url.string().utf8().data());

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-video.error");

        error = MediaPlayer::Empty;
        if (err->code == GST_STREAM_ERROR_CODEC_NOT_FOUND
            || err->code == GST_STREAM_ERROR_WRONG_TYPE
            || err->code == GST_STREAM_ERROR_FAILED
            || err->code == GST_CORE_ERROR_MISSING_PLUGIN
            || err->code == GST_RESOURCE_ERROR_NOT_FOUND)
            error = MediaPlayer::FormatError;
        else if (err->domain == GST_STREAM_ERROR) {
            // Let the mediaPlayerClient handle the stream error, in
            // this case the HTMLMediaElement will emit a stalled
            // event.
            if (err->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
                ERROR_MEDIA_MESSAGE("Decode error, let the Media element emit a stalled event.");
                break;
            }
            error = MediaPlayer::DecodeError;
            attemptNextLocation = true;
        } else if (err->domain == GST_RESOURCE_ERROR)
            error = MediaPlayer::NetworkError;

        if (attemptNextLocation)
            issueError = !loadNextLocation();
        if (issueError)
            loadingFailed(error);
        break;
    case GST_MESSAGE_EOS:
        didEnd();
        break;
    case GST_MESSAGE_ASYNC_DONE:
        if (!messageSourceIsPlaybin || m_delayingLoad)
            break;
        asyncStateChangeDone();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        // DEBUG
        {
            GstState newState;
            gst_message_parse_state_changed(message, &currentState, &newState, 0);
            LOG_MEDIA_MESSAGE("State changed %s --> %s", gst_element_state_get_name(currentState), gst_element_state_get_name(newState));
        }
        if (!messageSourceIsPlaybin || m_delayingLoad) {
            LOG_MEDIA_MESSAGE("messageSourceIsPlaybin=%s, m_delayingLoad=%s", messageSourceIsPlaybin?"true":"false", m_delayingLoad?"true":"false");
            break;
        }
        updateStates();

        // Construct a filename for the graphviz dot file output.
        GstState newState;
        gst_message_parse_state_changed(message, &currentState, &newState, 0);
        CString dotFileName = String::format("webkit-video.%s_%s", gst_element_state_get_name(currentState), gst_element_state_get_name(newState)).utf8();
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.data());

        LOG_MEDIA_MESSAGE("Playbin changed %s --> %s", gst_element_state_get_name(currentState), gst_element_state_get_name(newState));

        break;
    }
    case GST_MESSAGE_BUFFERING:
        processBufferingStats(message);
        break;
    case GST_MESSAGE_REQUEST_STATE:
        gst_message_parse_request_state(message, &requestedState);
        gst_element_get_state(m_pipeline.get(), &currentState, nullptr, 250 * GST_NSECOND);
        if (requestedState < currentState) {
            GUniquePtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(message)));
            INFO_MEDIA_MESSAGE("Element %s requested state change to %s", elementName.get(),
                gst_element_state_get_name(requestedState));
            m_requestedState = requestedState;
            if (!changePipelineState(requestedState))
                loadingFailed(MediaPlayer::Empty);
        }
        break;
    case GST_MESSAGE_CLOCK_LOST:
        // This can only happen in PLAYING state and we should just
        // get a new clock by moving back to PAUSED and then to
        // PLAYING again.
        // This can happen if the stream that ends in a sink that
        // provides the current clock disappears, for example if
        // the audio sink provides the clock and the audio stream
        // is disabled. It also happens relatively often with
        // HTTP adaptive streams when switching between different
        // variants of a stream.
        gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED);
        gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
        break;
    case GST_MESSAGE_LATENCY:
        // Recalculate the latency, we don't need any special handling
        // here other than the GStreamer default.
        // This can happen if the latency of live elements changes, or
        // for one reason or another a new live element is added or
        // removed from the pipeline.
        gst_bin_recalculate_latency(GST_BIN(m_pipeline.get()));
        break;
    case GST_MESSAGE_ELEMENT:
        if (gst_is_missing_plugin_message(message)) {
            if (gst_install_plugins_supported()) {
                m_missingPluginsCallback = MediaPlayerRequestInstallMissingPluginsCallback::create([this](uint32_t result) {
                    m_missingPluginsCallback = nullptr;
                    if (result != GST_INSTALL_PLUGINS_SUCCESS)
                        return;

                    changePipelineState(GST_STATE_READY);
                    changePipelineState(GST_STATE_PAUSED);
                });
                GUniquePtr<char> detail(gst_missing_plugin_message_get_installer_detail(message));
                GUniquePtr<char> description(gst_missing_plugin_message_get_description(message));
                m_player->client().requestInstallMissingPlugins(String::fromUTF8(detail.get()), String::fromUTF8(description.get()), *m_missingPluginsCallback);
            }
        }
#if USE(GSTREAMER_MPEGTS)
        else if (GstMpegtsSection* section = gst_message_parse_mpegts_section(message)) {
            processMpegTsSection(section);
            gst_mpegts_section_unref(section);
        }

#endif
        else {
            const GstStructure* structure = gst_message_get_structure(message);
            if (gst_structure_has_name(structure, "adaptive-streaming-statistics") && gst_structure_has_field(structure, "fragment-download-time")) {
                GUniqueOutPtr<gchar> uri;
                GstClockTime time;
                gst_structure_get(structure, "uri", G_TYPE_STRING, &uri.outPtr(), "fragment-download-time", GST_TYPE_CLOCK_TIME, &time, nullptr);
                INFO_MEDIA_MESSAGE("Fragment %s download time %" GST_TIME_FORMAT, uri.get(), GST_TIME_ARGS(time));
            }
        }
        break;
    case GST_MESSAGE_TOC:
        processTableOfContents(message);
        break;
    default:
        LOG_MEDIA_MESSAGE("Unhandled GStreamer message type: %s",
                    GST_MESSAGE_TYPE_NAME(message));
        break;
    }
    return TRUE;
}

void MediaPlayerPrivateGStreamerMSE::processBufferingStats(GstMessage* message)
{
    m_buffering = true;
    gst_message_parse_buffering(message, &m_bufferingPercentage);

    LOG_MEDIA_MESSAGE("[Buffering] Buffering: %d%%.", m_bufferingPercentage);

    updateStates();
}

#if USE(GSTREAMER_MPEGTS)
void MediaPlayerPrivateGStreamerMSE::processMpegTsSection(GstMpegtsSection* section)
{
    ASSERT(section);

    if (section->section_type == GST_MPEGTS_SECTION_PMT) {
        const GstMpegtsPMT* pmt = gst_mpegts_section_get_pmt(section);
        m_metadataTracks.clear();
        for (guint i = 0; i < pmt->streams->len; ++i) {
            const GstMpegtsPMTStream* stream = static_cast<const GstMpegtsPMTStream*>(g_ptr_array_index(pmt->streams, i));
            if (stream->stream_type == 0x05 || stream->stream_type >= 0x80) {
                AtomicString pid = String::number(stream->pid);
                RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = InbandMetadataTextTrackPrivateGStreamer::create(
                    InbandTextTrackPrivate::Metadata, InbandTextTrackPrivate::Data, pid);

                // 4.7.10.12.2 Sourcing in-band text tracks
                // If the new text track's kind is metadata, then set the text track in-band metadata track dispatch
                // type as follows, based on the type of the media resource:
                // Let stream type be the value of the "stream_type" field describing the text track's type in the
                // file's program map section, interpreted as an 8-bit unsigned integer. Let length be the value of
                // the "ES_info_length" field for the track in the same part of the program map section, interpreted
                // as an integer as defined by the MPEG-2 specification. Let descriptor bytes be the length bytes
                // following the "ES_info_length" field. The text track in-band metadata track dispatch type must be
                // set to the concatenation of the stream type byte and the zero or more descriptor bytes bytes,
                // expressed in hexadecimal using uppercase ASCII hex digits.
                String inbandMetadataTrackDispatchType;
                appendUnsignedAsHexFixedSize(stream->stream_type, inbandMetadataTrackDispatchType, 2);
                for (guint j = 0; j < stream->descriptors->len; ++j) {
                    const GstMpegtsDescriptor* descriptor = static_cast<const GstMpegtsDescriptor*>(g_ptr_array_index(stream->descriptors, j));
                    for (guint k = 0; k < descriptor->length; ++k)
                        appendByteAsHex(descriptor->data[k], inbandMetadataTrackDispatchType);
                }
                track->setInBandMetadataTrackDispatchType(inbandMetadataTrackDispatchType);

                m_metadataTracks.add(pid, track);
                m_player->addTextTrack(track);
            }
        }
    } else {
        AtomicString pid = String::number(section->pid);
        RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = m_metadataTracks.get(pid);
        if (!track)
            return;

        GRefPtr<GBytes> data = gst_mpegts_section_get_data(section);
        gsize size;
        const void* bytes = g_bytes_get_data(data.get(), &size);

        track->addDataCue(MediaTime::createWithDouble(currentTimeDouble()), MediaTime::createWithDouble(currentTimeDouble()), bytes, size);
    }
}
#endif

void MediaPlayerPrivateGStreamerMSE::processTableOfContents(GstMessage* message)
{
    if (m_chaptersTrack)
        m_player->removeTextTrack(m_chaptersTrack);

    m_chaptersTrack = InbandMetadataTextTrackPrivateGStreamer::create(InbandTextTrackPrivate::Chapters, InbandTextTrackPrivate::Generic);
    m_player->addTextTrack(m_chaptersTrack);

    GRefPtr<GstToc> toc;
    gboolean updated;
    gst_message_parse_toc(message, &toc.outPtr(), &updated);
    ASSERT(toc);

    for (GList* i = gst_toc_get_entries(toc.get()); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), 0);
}

void MediaPlayerPrivateGStreamerMSE::processTableOfContentsEntry(GstTocEntry* entry, GstTocEntry* parent)
{
    UNUSED_PARAM(parent);
    ASSERT(entry);

    RefPtr<GenericCueData> cue = GenericCueData::create();

    gint64 start = -1, stop = -1;
    gst_toc_entry_get_start_stop_times(entry, &start, &stop);
    if (start != -1)
        cue->setStartTime(MediaTime(start, GST_SECOND));
    if (stop != -1)
        cue->setEndTime(MediaTime(stop, GST_SECOND));

    GstTagList* tags = gst_toc_entry_get_tags(entry);
    if (tags) {
        gchar* title =  0;
        gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
        if (title) {
            cue->setContent(title);
            g_free(title);
        }
    }

    m_chaptersTrack->addGenericCue(cue.release());

    for (GList* i = gst_toc_entry_get_sub_entries(entry); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), entry);
}

void MediaPlayerPrivateGStreamerMSE::fillTimerFired()
{
    GstQuery* query = gst_query_new_buffering(GST_FORMAT_PERCENT);

    if (!gst_element_query(m_pipeline.get(), query)) {
        gst_query_unref(query);
        return;
    }

    gint64 start, stop;
    gdouble fillStatus = 100.0;

    gst_query_parse_buffering_range(query, 0, &start, &stop, 0);
    gst_query_unref(query);

    if (stop != -1)
        fillStatus = 100.0 * stop / GST_FORMAT_PERCENT_MAX;

    LOG_MEDIA_MESSAGE("[Buffering] Download buffer filled up to %f%%", fillStatus);

    if (!m_mediaDuration)
        durationChanged();

    // Update maxTimeLoaded only if the media duration is
    // available. Otherwise we can't compute it.
    if (m_mediaDuration) {
        if (fillStatus == 100.0)
            m_maxTimeLoaded = m_mediaDuration;
        else
            m_maxTimeLoaded = static_cast<float>((fillStatus * m_mediaDuration) / 100.0);
        LOG_MEDIA_MESSAGE("[Buffering] Updated maxTimeLoaded: %f", m_maxTimeLoaded);
    }

    m_downloadFinished = fillStatus == 100.0;
    if (!m_downloadFinished) {
        updateStates();
        return;
    }

    // Media is now fully loaded. It will play even if network
    // connection is cut. Buffering is done, remove the fill source
    // from the main loop.
    m_fillTimer.stop();
    updateStates();
}

float MediaPlayerPrivateGStreamerMSE::maxTimeSeekable() const
{
    if (m_errorOccured)
        return 0.0f;

    LOG_MEDIA_MESSAGE("maxTimeSeekable");
    // infinite duration means live stream
    if (std::isinf(duration()))
        return 0.0f;

    return duration();
}

float MediaPlayerPrivateGStreamerMSE::maxTimeLoaded() const
{
    if (m_errorOccured)
        return 0.0f;

    float loaded = m_maxTimeLoaded;
    if (m_isEndReached && m_mediaDuration)
        loaded = m_mediaDuration;
    LOG_MEDIA_MESSAGE("maxTimeLoaded: %f", loaded);
    return loaded;
}

bool MediaPlayerPrivateGStreamerMSE::didLoadingProgress() const
{
    if (!m_pipeline || (!m_mediaSource && !totalBytes()) || !m_mediaDuration)
        return false;
    float currentMaxTimeLoaded = maxTimeLoaded();
    bool didLoadingProgress = currentMaxTimeLoaded != m_maxTimeLoadedAtLastDidLoadingProgress;
    m_maxTimeLoadedAtLastDidLoadingProgress = currentMaxTimeLoaded;
    LOG_MEDIA_MESSAGE("didLoadingProgress: %d", didLoadingProgress);
    return didLoadingProgress;
}

unsigned long long MediaPlayerPrivateGStreamerMSE::totalBytes() const
{
    if (m_errorOccured)
        return 0;

    if (m_totalBytes)
        return m_totalBytes;

    if (!m_webKitMediaSrc)
        return 0;

    GstFormat fmt = GST_FORMAT_BYTES;
    gint64 length = 0;
    if (gst_element_query_duration(m_webKitMediaSrc.get(), fmt, &length)) {
        INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
        m_totalBytes = static_cast<unsigned long long>(length);
        m_isStreaming = !length;
        return m_totalBytes;
    }

    // Fall back to querying the source pads manually.
    // See also https://bugzilla.gnome.org/show_bug.cgi?id=638749
    GstIterator* iter = gst_element_iterate_src_pads(m_webKitMediaSrc.get());
    bool done = false;
    while (!done) {
        GValue item = G_VALUE_INIT;
        switch (gst_iterator_next(iter, &item)) {
        case GST_ITERATOR_OK: {
            GstPad* pad = static_cast<GstPad*>(g_value_get_object(&item));
            gint64 padLength = 0;
            if (gst_pad_query_duration(pad, fmt, &padLength) && padLength > length)
                length = padLength;
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(iter);
            break;
        case GST_ITERATOR_ERROR:
            FALLTHROUGH;
        case GST_ITERATOR_DONE:
            done = true;
            break;
        }

        g_value_unset(&item);
    }

    gst_iterator_free(iter);

    INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
    m_totalBytes = static_cast<unsigned long long>(length);
    m_isStreaming = !length;
    return m_totalBytes;
}

void MediaPlayerPrivateGStreamerMSE::sourceChanged()
{
    m_webKitMediaSrc.clear();
    g_object_get(m_pipeline.get(), "source", &m_webKitMediaSrc.outPtr(), nullptr);

    g_assert(WEBKIT_IS_MEDIA_SRC(m_webKitMediaSrc.get()));

    m_playbackPipeline->setWebKitMediaSrc(WEBKIT_MEDIA_SRC(m_webKitMediaSrc.get()));

    MediaSourceGStreamer::open(m_mediaSource.get(), this);
    g_signal_connect(m_webKitMediaSrc.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
    g_signal_connect(m_webKitMediaSrc.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);
    g_signal_connect(m_webKitMediaSrc.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);
    webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_webKitMediaSrc.get()), this);
}

void MediaPlayerPrivateGStreamerMSE::cancelLoad()
{
    if (m_networkState < MediaPlayer::Loading || m_networkState == MediaPlayer::Loaded)
        return;

    if (m_pipeline)
        changePipelineState(GST_STATE_READY);
}

void MediaPlayerPrivateGStreamerMSE::asyncStateChangeDone()
{
    LOG_MEDIA_MESSAGE("%s", "");

    if (!m_pipeline || m_errorOccured)
        return;

    if (m_seeking) {
        if (m_seekIsPending)
            updateStates();
        else {
            LOG_MEDIA_MESSAGE("[Seek] seeked to %f", m_seekTime);
            m_seeking = false;
            LOG_MEDIA_MESSAGE("m_seeking=%s", m_seeking?"true":"false");
            m_cachedPosition = -1;
            if (m_timeOfOverlappingSeek != m_seekTime && m_timeOfOverlappingSeek != -1) {
                seek(m_timeOfOverlappingSeek);
                m_timeOfOverlappingSeek = -1;
                return;
            }
            m_timeOfOverlappingSeek = -1;

            // The pipeline can still have a pending state. In this case a position query will fail.
            // Right now we can use m_seekTime as a fallback.
            m_canFallBackToLastFinishedSeekPosition = true;
            timeChanged();
        }
    } else
        updateStates();
}

void MediaPlayerPrivateGStreamerMSE::updateStates()
{
    if (!m_pipeline)
        return;

    if (m_errorOccured)
        return;

    MediaPlayer::NetworkState oldNetworkState = m_networkState;
    MediaPlayer::ReadyState oldReadyState = m_readyState;
    GstState state;
    GstState pending;

    GstStateChangeReturn getStateResult = gst_element_get_state(m_pipeline.get(), &state, &pending, 250 * GST_NSECOND);

    bool shouldUpdatePlaybackState = false;
    switch (getStateResult) {
    case GST_STATE_CHANGE_SUCCESS: {
        LOG_MEDIA_MESSAGE("State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        // Do nothing if on EOS and state changed to READY to avoid recreating the player
        // on HTMLMediaElement and properly generate the video 'ended' event.
        if (m_isEndReached && state == GST_STATE_READY)
            break;

        if (state <= GST_STATE_READY) {
            m_resetPipeline = true;
            m_mediaDuration = 0;
        } else {
            m_resetPipeline = false;
            cacheDuration();
        }

        bool didBuffering = m_buffering;

        // Update ready and network states.
        switch (state) {
        case GST_STATE_NULL:
            m_readyState = MediaPlayer::HaveNothing;
            LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_READY:
            m_readyState = MediaPlayer::HaveMetadata;
            LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_PAUSED:
        case GST_STATE_PLAYING:
            if (m_seeking && !timeIsBuffered(m_seekTime)) {
                m_readyState = MediaPlayer::HaveMetadata;
                // TODO: NetworkState?
                LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
            } else if (m_buffering) {
                if (m_bufferingPercentage == 100) {
                    LOG_MEDIA_MESSAGE("[Buffering] Complete.");
                    m_buffering = false;
                    m_readyState = MediaPlayer::HaveEnoughData;
                    LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
                    m_networkState = m_downloadFinished ? MediaPlayer::Idle : MediaPlayer::Loading;
                } else {
                    m_readyState = MediaPlayer::HaveCurrentData;
                    LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
                    m_networkState = MediaPlayer::Loading;
                }
            } else if (m_downloadFinished) {
                m_readyState = MediaPlayer::HaveEnoughData;
                LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
                m_networkState = MediaPlayer::Loaded;
            } else {
                m_readyState = MediaPlayer::HaveFutureData;
                LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
                m_networkState = MediaPlayer::Loading;
            }

            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }

        // Sync states where needed.
        if (state == GST_STATE_PAUSED) {
            if (!m_volumeAndMuteInitialized) {
                notifyPlayerOfVolumeChange();
                notifyPlayerOfMute();
                m_volumeAndMuteInitialized = true;
            }

            if (didBuffering && !m_buffering && !m_paused && m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Restarting playback.");
                changePipelineState(GST_STATE_PLAYING);
            }
        } else if (state == GST_STATE_PLAYING) {
            m_paused = false;

            if ((m_buffering && !isLiveStream()) || !m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Pausing stream for buffering.");
                changePipelineState(GST_STATE_PAUSED);
            }
        } else
            m_paused = true;

        if (m_requestedState == GST_STATE_PAUSED && state == GST_STATE_PAUSED) {
            shouldUpdatePlaybackState = true;
            LOG_MEDIA_MESSAGE("Requested state change to %s was completed", gst_element_state_get_name(state));
        }

        break;
    }
    case GST_STATE_CHANGE_ASYNC:
        LOG_MEDIA_MESSAGE("Async: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change in progress.
        break;
    case GST_STATE_CHANGE_FAILURE:
        LOG_MEDIA_MESSAGE("Failure: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change failed
        return;
    case GST_STATE_CHANGE_NO_PREROLL:
        LOG_MEDIA_MESSAGE("No preroll: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        // Live pipelines go in PAUSED without prerolling.
        m_isStreaming = true;

        if (state == GST_STATE_READY) {
            m_readyState = MediaPlayer::HaveNothing;
            LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
        } else if (state == GST_STATE_PAUSED) {
            m_readyState = MediaPlayer::HaveEnoughData;
            LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
            m_paused = true;
        } else if (state == GST_STATE_PLAYING)
            m_paused = false;

        if (!m_paused && m_playbackRate)
            changePipelineState(GST_STATE_PLAYING);

        m_networkState = MediaPlayer::Loading;
        break;
    default:
        LOG_MEDIA_MESSAGE("Else : %d", getStateResult);
        break;
    }

    m_requestedState = GST_STATE_VOID_PENDING;

    if (shouldUpdatePlaybackState)
        m_player->playbackStateChanged();

    if (m_networkState != oldNetworkState) {
        LOG_MEDIA_MESSAGE("Network State Changed from %u to %u", oldNetworkState, m_networkState);
        m_player->networkStateChanged();
    }
    if (m_readyState != oldReadyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed from %u to %u", oldReadyState, m_readyState);
        m_player->readyStateChanged();
    }

    if (getStateResult == GST_STATE_CHANGE_SUCCESS && state >= GST_STATE_PAUSED) {
        updatePlaybackRate();
        if (m_seekIsPending && timeIsBuffered(m_seekTime)) {
            LOG_MEDIA_MESSAGE("[Seek] committing pending seek to %f", m_seekTime);
            m_seekIsPending = false;
            m_seeking = doSeek(toGstClockTime(m_seekTime), m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
            LOG_MEDIA_MESSAGE("m_seeking=%s", m_seeking?"true":"false");
            if (!m_seeking) {
                m_cachedPosition = -1;
                LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", m_seekTime);
            }
        }
    }
}

bool MediaPlayerPrivateGStreamerMSE::timeIsBuffered(float time)
{
    bool result = m_mediaSource && m_mediaSource->buffered()->contain(MediaTime::createWithFloat(time));
    LOG_MEDIA_MESSAGE("Time %f buffered? %s", time, result ? "aye" : "nope");
    return result;
}

void MediaPlayerPrivateGStreamerMSE::setMediaSourceClient(PassRefPtr<MediaSourceClientGStreamerMSE> client)
{
    m_mediaSourceClient = client;
}

RefPtr<MediaSourceClientGStreamerMSE> MediaPlayerPrivateGStreamerMSE::mediaSourceClient()
{
    return m_mediaSourceClient;
}

RefPtr<AppendPipeline> MediaPlayerPrivateGStreamerMSE::appendPipelineByTrackId(const AtomicString& trackId)
{
    if (trackId == AtomicString()) {
        LOG_MEDIA_MESSAGE("trackId is empty");
    }

    ASSERT(!(trackId.isNull() || trackId.isEmpty()));

    for (HashMap<RefPtr<SourceBufferPrivateGStreamer>, RefPtr<AppendPipeline> >::iterator it = m_appendPipelinesMap.begin(); it != m_appendPipelinesMap.end(); ++it)
        if (it->value->trackId() == trackId)
            return it->value;

    return RefPtr<AppendPipeline>(0);
}


void MediaPlayerPrivateGStreamerMSE::mediaLocationChanged(GstMessage* message)
{
    if (m_mediaLocations)
        gst_structure_free(m_mediaLocations);

    const GstStructure* structure = gst_message_get_structure(message);
    if (structure) {
        // This structure can contain:
        // - both a new-location string and embedded locations structure
        // - or only a new-location string.
        m_mediaLocations = gst_structure_copy(structure);
        const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");

        if (locations)
            m_mediaLocationCurrentIndex = static_cast<int>(gst_value_list_get_size(locations)) -1;

        loadNextLocation();
    }
}

bool MediaPlayerPrivateGStreamerMSE::loadNextLocation()
{
    if (!m_mediaLocations)
        return false;

    const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");
    const gchar* newLocation = 0;

    if (!locations) {
        // Fallback on new-location string.
        newLocation = gst_structure_get_string(m_mediaLocations, "new-location");
        if (!newLocation)
            return false;
    }

    if (!newLocation) {
        if (m_mediaLocationCurrentIndex < 0) {
            m_mediaLocations = 0;
            return false;
        }

        const GValue* location = gst_value_list_get_value(locations,
                                                          m_mediaLocationCurrentIndex);
        const GstStructure* structure = gst_value_get_structure(location);

        if (!structure) {
            m_mediaLocationCurrentIndex--;
            return false;
        }

        newLocation = gst_structure_get_string(structure, "new-location");
    }

    if (newLocation) {
        // Found a candidate. new-location is not always an absolute url
        // though. We need to take the base of the current url and
        // append the value of new-location to it.
        URL baseUrl = gst_uri_is_valid(newLocation) ? URL() : m_url;
        URL newUrl = URL(baseUrl, newLocation);

        RefPtr<SecurityOrigin> securityOrigin = SecurityOrigin::create(m_url);
        if (securityOrigin->canRequest(newUrl)) {
            INFO_MEDIA_MESSAGE("New media url: %s", newUrl.string().utf8().data());

            // Reset player states.
            m_networkState = MediaPlayer::Loading;
            m_player->networkStateChanged();
            m_readyState = MediaPlayer::HaveNothing;
            LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
            m_player->readyStateChanged();

            // Reset pipeline state.
            m_resetPipeline = true;
            changePipelineState(GST_STATE_READY);

            GstState state;
            gst_element_get_state(m_pipeline.get(), &state, nullptr, 0);
            if (state <= GST_STATE_READY) {
                // Set the new uri and start playing.
                g_object_set(m_pipeline.get(), "uri", newUrl.string().utf8().data(), nullptr);
                m_url = newUrl;
                changePipelineState(GST_STATE_PLAYING);
                return true;
            }
        } else
            INFO_MEDIA_MESSAGE("Not allowed to load new media location: %s", newUrl.string().utf8().data());
    }
    m_mediaLocationCurrentIndex--;
    return false;
}

void MediaPlayerPrivateGStreamerMSE::loadStateChanged()
{
    updateStates();
}

void MediaPlayerPrivateGStreamerMSE::timeChanged()
{
    updateStates();
    m_player->timeChanged();
}

void MediaPlayerPrivateGStreamerMSE::didEnd()
{
    // Synchronize position and duration values to not confuse the
    // HTMLMediaElement. In some cases like reverse playback the
    // position is not always reported as 0 for instance.
    float now = currentTime();
    if (now > 0 && now <= duration() && m_mediaDuration != now) {
        m_mediaDurationKnown = true;
        m_mediaDuration = now;
        m_player->durationChanged();
    }

    m_isEndReached = true;
    timeChanged();

    if (!m_player->client().mediaPlayerIsLooping()) {
        m_paused = true;
        changePipelineState(GST_STATE_READY);
        m_downloadFinished = false;
    }
}

void MediaPlayerPrivateGStreamerMSE::cacheDuration()
{
    if (m_mediaDuration || !m_mediaDurationKnown)
        return;

    float newDuration = duration();
    if (std::isinf(newDuration)) {
        // Only pretend that duration is not available if the the query failed in a stable pipeline state.
        GstState state;
        if (gst_element_get_state(m_pipeline.get(), &state, nullptr, 0) == GST_STATE_CHANGE_SUCCESS && state > GST_STATE_READY)
            m_mediaDurationKnown = false;
        return;
    }

    m_mediaDuration = newDuration;
}

void MediaPlayerPrivateGStreamerMSE::notifyDurationChanged()
{
    m_pendingAsyncOperationsLock.lock();
    ASSERT(m_pendingAsyncOperations);
    m_pendingAsyncOperations = g_list_remove(m_pendingAsyncOperations, m_pendingAsyncOperations->data);
    m_pendingAsyncOperationsLock.unlock();

    durationChanged();
}

void MediaPlayerPrivateGStreamerMSE::durationChanged()
{
    float previousDuration = m_mediaDuration;

    if (!m_mediaSourceClient) {
        LOG_MEDIA_MESSAGE("m_mediaSourceClient is null, not doing anything");
        return;
    }
    m_mediaDuration = m_mediaSourceClient->duration().toFloat();

    LOG_MEDIA_MESSAGE("previous=%f, new=%f", previousDuration, m_mediaDuration);
    cacheDuration();
    // Avoid emiting durationchanged in the case where the previous
    // duration was 0 because that case is already handled by the
    // HTMLMediaElement.
    if (m_mediaDuration != previousDuration) {
        LOG_MEDIA_MESSAGE("Notifying player and WebKitMediaSrc");
        m_player->durationChanged();
        m_playbackPipeline->notifyDurationChanged();
    }
}

void MediaPlayerPrivateGStreamerMSE::loadingFailed(MediaPlayer::NetworkState error)
{
    m_errorOccured = true;
    if (m_networkState != error) {
        m_networkState = error;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        LOG_MEDIA_MESSAGE("m_readyState=%s", dumpReadyState(m_readyState));
        m_player->readyStateChanged();
    }

    // Loading failed, remove ready timer.
    m_readyTimerHandler.cancel();
}

static HashSet<String> mimeTypeCache()
{
    initializeGStreamerAndRegisterWebKitMESElement();

    DEPRECATED_DEFINE_STATIC_LOCAL(HashSet<String>, cache, ());
    static bool typeListInitialized = false;

    if (typeListInitialized)
        return cache;

    const char* mimeTypes[] = {
        "audio/mp4",
        "video/mp4"
    };

    for (unsigned i = 0; i < (sizeof(mimeTypes) / sizeof(*mimeTypes)); ++i)
        cache.add(String(mimeTypes[i]));

    typeListInitialized = true;
    return cache;
}

void MediaPlayerPrivateGStreamerMSE::getSupportedTypes(HashSet<String>& types)
{
    types = mimeTypeCache();
}

void MediaPlayerPrivateGStreamerMSE::trackDetected(RefPtr<AppendPipeline> ap, RefPtr<WebCore::TrackPrivateBase> oldTrack, RefPtr<WebCore::TrackPrivateBase> newTrack)
{
    ASSERT (ap->track() == newTrack);

    LOG_MEDIA_MESSAGE("%s", newTrack->id().string().latin1().data());

    if (!oldTrack)
        m_playbackPipeline->attachTrack(ap->sourceBufferPrivate(), newTrack, ap->appSinkCaps());
    else
        m_playbackPipeline->reattachTrack(ap->sourceBufferPrivate(), newTrack, ap->appSinkCaps());
}

MediaPlayer::SupportsType MediaPlayerPrivateGStreamerMSE::supportsType(const MediaEngineSupportParameters& parameters)
{
    MediaPlayer::SupportsType result = MediaPlayer::IsNotSupported;
    if (!parameters.isMediaSource)
        return result;

    // Disable VPX/Opus on MSE for now, mp4/avc1 seems way more reliable currently.
    if (parameters.type.endsWith("webm"))
        return result;

    // Youtube TV provides empty types for some videos and we want to be selected as best media engine for them.
    if (parameters.type.isNull() || parameters.type.isEmpty()) {
        result = MediaPlayer::MayBeSupported;
        return result;
    }

    // spec says we should not return "probably" if the codecs string is empty
    if (mimeTypeCache().contains(parameters.type))
        result = parameters.codecs.isEmpty() ? MediaPlayer::MayBeSupported : MediaPlayer::IsSupported;

    return extendedSupportsType(parameters, result);
}

void MediaPlayerPrivateGStreamerMSE::setPreload(MediaPlayer::Preload preload)
{
    if (preload == MediaPlayer::Auto && isLiveStream())
        return;

    m_preload = preload;

    if (m_delayingLoad && m_preload != MediaPlayer::None) {
        m_delayingLoad = false;
        commitLoad();
    }
}

GstElement* MediaPlayerPrivateGStreamerMSE::createAudioSink()
{
    m_autoAudioSink = gst_element_factory_make("autoaudiosink", 0);
    g_signal_connect(m_autoAudioSink.get(), "child-added", G_CALLBACK(setAudioStreamPropertiesCallback), this);

    GstElement* audioSinkBin;

    if (webkitGstCheckVersion(1, 4, 2)) {
#if ENABLE(WEB_AUDIO)
        audioSinkBin = gst_bin_new("audio-sink");
        m_audioSourceProvider->configureAudioBin(audioSinkBin, nullptr);
        return audioSinkBin;
#else
        return m_autoAudioSink.get();
#endif
    }

    // Construct audio sink only if pitch preserving is enabled.
    // If GStreamer 1.4.2 is used the audio-filter playbin property is used instead.
    if (m_preservesPitch) {
        GstElement* scale = gst_element_factory_make("scaletempo", nullptr);
        if (!scale) {
            GST_WARNING("Failed to create scaletempo");
            return m_autoAudioSink.get();
        }

        audioSinkBin = gst_bin_new("audio-sink");
        gst_bin_add(GST_BIN(audioSinkBin), scale);
        GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(scale, "sink"));
        gst_element_add_pad(audioSinkBin, gst_ghost_pad_new("sink", pad.get()));

#if ENABLE(WEB_AUDIO)
        m_audioSourceProvider->configureAudioBin(audioSinkBin, scale);
#else
        GstElement* convert = gst_element_factory_make("audioconvert", nullptr);
        GstElement* resample = gst_element_factory_make("audioresample", nullptr);

        gst_bin_add_many(GST_BIN(audioSinkBin), convert, resample, m_autoAudioSink.get(), nullptr);

        if (!gst_element_link_many(scale, convert, resample, m_autoAudioSink.get(), nullptr)) {
            GST_WARNING("Failed to link audio sink elements");
            gst_object_unref(audioSinkBin);
            return m_autoAudioSink.get();
        }
#endif
        return audioSinkBin;
    }

#if ENABLE(WEB_AUDIO)
    audioSinkBin = gst_bin_new("audio-sink");
    m_audioSourceProvider->configureAudioBin(audioSinkBin, nullptr);
    return audioSinkBin;
#endif
    ASSERT_NOT_REACHED();
    return 0;
}

GstElement* MediaPlayerPrivateGStreamerMSE::audioSink() const
{
    GstElement* sink;
    g_object_get(m_pipeline.get(), "audio-sink", &sink, nullptr);
    return sink;

    GRefPtr<GstElement> playsink = adoptGRef(gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "playsink"));
    if (playsink) {
        // The default value (0) means "send events to all the sinks", instead
        // of "only to the first that returns true". This is needed for MSE seek.
        g_object_set(G_OBJECT(playsink.get()), "send-event-mode", 0, NULL);
    }
}

void MediaPlayerPrivateGStreamerMSE::configurePlaySink()
{
    GRefPtr<GstElement> playsink = adoptGRef(gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "playsink"));
    if (playsink) {
        // The default value (0) means "send events to all the sinks", instead
        // of "only to the first that returns true". This is needed for MSE seek.
        g_object_set(G_OBJECT(playsink.get()), "send-event-mode", 0, NULL);
    }
}

void MediaPlayerPrivateGStreamerMSE::createGSTPlayBin()
{
    ASSERT(!m_pipeline);

    // gst_element_factory_make() returns a floating reference so
    // we should not adopt.
    setPipeline(gst_element_factory_make("playbin", "play"));
    setStreamVolumeElement(GST_STREAM_VOLUME(m_pipeline.get()));

    unsigned flagText = getGstPlayFlag("text");
    unsigned flagAudio = getGstPlayFlag("audio");
    unsigned flagVideo = getGstPlayFlag("video");
    unsigned flagNativeVideo = getGstPlayFlag("native-video");
    g_object_set(m_pipeline.get(), "flags", flagText | flagAudio | flagVideo | flagNativeVideo, nullptr);

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_add_signal_watch(bus.get());
    g_signal_connect(bus.get(), "message", G_CALLBACK(mediaPlayerPrivateMessageCallback), this);

    g_object_set(m_pipeline.get(), "mute", m_player->muted(), nullptr);

    g_signal_connect(m_pipeline.get(), "notify::source", G_CALLBACK(mediaPlayerPrivateSourceChangedCallback), this);

    // If we load a MediaSource later, we will also listen the signals from
    // WebKitMediaSrc, which will be connected later in sourceChanged()
    // METRO FIXME: In that case, we shouldn't listen to these signals coming from playbin, or the callbacks will be called twice.
    g_signal_connect(m_pipeline.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
    g_signal_connect(m_pipeline.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);
    if (webkitGstCheckVersion(1, 1, 2)) {
        g_signal_connect(m_pipeline.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);

        GstElement* textCombiner = webkitTextCombinerNew();
        ASSERT(textCombiner);
        g_object_set(m_pipeline.get(), "text-stream-combiner", textCombiner, nullptr);

        m_textAppSink = webkitTextSinkNew();
        ASSERT(m_textAppSink);

        m_textAppSinkPad = adoptGRef(gst_element_get_static_pad(m_textAppSink.get(), "sink"));
        ASSERT(m_textAppSinkPad);

        g_object_set(m_textAppSink.get(), "emit-signals", true, "enable-last-sample", false, "caps", gst_caps_new_empty_simple("text/vtt"), NULL);
        g_signal_connect(m_textAppSink.get(), "new-sample", G_CALLBACK(mediaPlayerPrivateNewTextSampleCallback), this);

        g_object_set(m_pipeline.get(), "text-sink", m_textAppSink.get(), NULL);
    }

    g_object_set(m_pipeline.get(), "video-sink", createVideoSink(), "audio-sink", createAudioSink(), nullptr);
    configurePlaySink();

    // On 1.4.2 and newer we use the audio-filter property instead.
    // See https://bugzilla.gnome.org/show_bug.cgi?id=735748 for
    // the reason for using >= 1.4.2 instead of >= 1.4.0.
    if (m_preservesPitch && webkitGstCheckVersion(1, 4, 2)) {
        GstElement* scale = gst_element_factory_make("scaletempo", 0);

        if (!scale)
            GST_WARNING("Failed to create scaletempo");
        else
            g_object_set(m_pipeline.get(), "audio-filter", scale, nullptr);
    }

    GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_videoSink.get(), "sink"));
    if (videoSinkPad)
        g_signal_connect(videoSinkPad.get(), "notify::caps", G_CALLBACK(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);
}

void MediaPlayerPrivateGStreamerMSE::simulateAudioInterruption()
{
    GstMessage* message = gst_message_new_request_state(GST_OBJECT(m_pipeline.get()), GST_STATE_PAUSED);
    gst_element_post_message(m_pipeline.get(), message);
}

bool MediaPlayerPrivateGStreamerMSE::didPassCORSAccessCheck() const
{
    if (WEBKIT_IS_WEB_SRC(m_webKitMediaSrc.get()))
        return webKitSrcPassedCORSAccessCheck(WEBKIT_WEB_SRC(m_webKitMediaSrc.get()));
    return false;
}

bool MediaPlayerPrivateGStreamerMSE::canSaveMediaData() const
{
    if (isLiveStream())
        return false;

    if (m_url.isLocalFile())
        return true;

    if (m_url.protocolIsInHTTPFamily())
        return true;

    return false;
}

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
void MediaPlayerPrivateGStreamerMSE::dispatchDecryptionKey(GstBuffer* buffer)
{
    for (HashMap<RefPtr<SourceBufferPrivateGStreamer>, RefPtr<AppendPipeline> >::iterator it = m_appendPipelinesMap.begin(); it != m_appendPipelinesMap.end(); ++it) {
        gst_element_send_event(it->value->pipeline(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
            gst_structure_new("drm-cipher", "key", GST_TYPE_BUFFER, buffer, nullptr)));
        it->value->setAppendStage(AppendPipeline::AppendStage::Ongoing);
    }
}
#endif

#if USE(DXDRM)
void MediaPlayerPrivateGStreamerMSE::emitSession()
{
    for (HashMap<RefPtr<SourceBufferPrivateGStreamer>, RefPtr<AppendPipeline> >::iterator it = m_appendPipelinesMap.begin(); it != m_appendPipelinesMap.end(); ++it) {
        gst_element_send_event(it->value->pipeline(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
           gst_structure_new("dxdrm-session", "session", G_TYPE_POINTER, dxdrmSession(), nullptr)));
        it->value->setAppendStage(AppendPipeline::AppendStage::Ongoing);
    }
}
#endif

class GStreamerMediaDescription : public MediaDescription {
private:
    GstCaps* m_caps;
public:
    static PassRefPtr<GStreamerMediaDescription> create(GstCaps* caps)
    {
        return adoptRef(new GStreamerMediaDescription(caps));
    }

    virtual ~GStreamerMediaDescription()
    {
        gst_caps_unref(m_caps);
    }

    AtomicString codec() const override
    {
        gchar* description = gst_pb_utils_get_codec_description(m_caps);
        String codecName(description);

        // Report "H.264 (Main Profile)" and "H.264 (High Profile)" just as
        // "H.264" to allow changes between both variants go unnoticed to the
        // SourceBuffer layer.
        if (codecName.startsWith("H.264")) {
            size_t braceStart = codecName.find(" (");
            size_t braceEnd = codecName.find(")");
            if (braceStart != notFound && braceEnd != notFound)
                codecName.remove(braceStart, braceEnd-braceStart);
        }
        AtomicString simpleCodecName(codecName);
        g_free(description);

        return simpleCodecName;
    }

    bool isVideo() const override
    {
        GstStructure* s = gst_caps_get_structure(m_caps, 0);
        const gchar* name = gst_structure_get_name(s);

#if GST_CHECK_VERSION(1, 5, 3)
        if (!g_strcmp0(name, "application/x-cenc"))
            return g_str_has_prefix(gst_structure_get_string(s, "original-media-type"), "video/");
#endif
        return g_str_has_prefix(name, "video/");
    }

    bool isAudio() const override
    {
        GstStructure* s = gst_caps_get_structure(m_caps, 0);
        const gchar* name = gst_structure_get_name(s);

#if GST_CHECK_VERSION(1, 5, 3)
        if (!g_strcmp0(name, "application/x-cenc"))
            return g_str_has_prefix(gst_structure_get_string(s, "original-media-type"), "audio/");
#endif
        return g_str_has_prefix(name, "audio/");
    }

    bool isText() const override
    {
        // TODO
        return false;
    }

private:
    GStreamerMediaDescription(GstCaps* caps)
        : MediaDescription()
        , m_caps(gst_caps_ref(caps))
    {
    }
};

// class GStreamerMediaSample : public MediaSample
GStreamerMediaSample::GStreamerMediaSample(GstSample* sample, const FloatSize& presentationSize, const AtomicString& trackID)
    : MediaSample()
    , m_pts(MediaTime::zeroTime())
    , m_dts(MediaTime::zeroTime())
    , m_duration(MediaTime::zeroTime())
    , m_trackID(trackID)
    , m_size(0)
    , m_sample(0)
    , m_presentationSize(presentationSize)
    , m_flags(MediaSample::IsSync)
{

    if (!sample)
        return;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer)
        return;

    if (GST_BUFFER_PTS_IS_VALID(buffer))
        m_pts = MediaTime(GST_BUFFER_PTS(buffer), GST_SECOND);
    if (GST_BUFFER_DTS_IS_VALID(buffer))
        m_dts = MediaTime(GST_BUFFER_DTS(buffer), GST_SECOND);
    if (GST_BUFFER_DURATION_IS_VALID(buffer))
        m_duration = MediaTime(GST_BUFFER_DURATION(buffer), GST_SECOND);
    m_size = gst_buffer_get_size(buffer);
    m_sample = gst_sample_ref(sample);

    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
        m_flags = MediaSample::None;

    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY))
        m_flags = (MediaSample::SampleFlags) (m_flags | MediaSample::NonDisplaying);
}

PassRefPtr<GStreamerMediaSample> GStreamerMediaSample::create(GstSample* sample, const FloatSize& presentationSize, const AtomicString& trackID)
{
    return adoptRef(new GStreamerMediaSample(sample, presentationSize, trackID));
}

PassRefPtr<GStreamerMediaSample> GStreamerMediaSample::createFakeSample(GstCaps* caps, MediaTime pts, MediaTime dts, MediaTime duration, const FloatSize& presentationSize, const AtomicString& trackID)
{
    GstSample* sample = gst_sample_new(0, gst_caps_ref(caps), 0, 0);
    GStreamerMediaSample* s = new GStreamerMediaSample(sample, presentationSize, trackID);
    s->m_pts = pts;
    s->m_dts = dts;
    s->m_duration = duration;
    s->m_flags = MediaSample::NonDisplaying;
    return adoptRef(s);
}

GStreamerMediaSample::~GStreamerMediaSample()
{
    if (m_sample)
        gst_sample_unref(m_sample);
}

// Auxiliar to pass several parameters to appendPipelineAppSinkNewSampleMainThread().
class NewSampleInfo
{
public:
    NewSampleInfo(GstSample* sample, AppendPipeline* appendPipeline)
    {
        m_sample = gst_sample_ref(sample);
        m_ap = appendPipeline;
    }
    virtual ~NewSampleInfo()
    {
        gst_sample_unref(m_sample);
    }

    GstSample* sample() { return m_sample; }
    RefPtr<AppendPipeline> ap() { return m_ap; }

private:
    GstSample* m_sample;
    RefPtr<AppendPipeline> m_ap;
};

// Auxiliar to pass several parameters to appendPipelineAppSinkDemuxerPadAddedMainThread().
class PadInfo
{
public:
    PadInfo(GstPad* demuxerSrcPad, AppendPipeline* appendPipeline)
    {
        m_demuxerSrcPad = GST_PAD(gst_object_ref(demuxerSrcPad));
        m_ap = appendPipeline;
        g_mutex_init(&m_mutex);
        g_cond_init(&m_condition);
    }
    virtual ~PadInfo()
    {
        g_cond_signal(&m_condition);
        g_cond_clear(&m_condition);
        g_mutex_clear(&m_mutex);
        gst_object_unref(m_demuxerSrcPad);
    }

    void lock()
    {
        g_mutex_lock(&m_mutex);
    }

    void unlock()
    {
        g_mutex_unlock(&m_mutex);
    }

    void wait()
    {
        g_cond_wait(&m_condition, &m_mutex);
    }

    void signal()
    {
        g_cond_signal(&m_condition);
    }

    GstPad* demuxerSrcPad() { return m_demuxerSrcPad; }
    RefPtr<AppendPipeline> ap() { return m_ap; }

private:
    GstPad* m_demuxerSrcPad;
    RefPtr<AppendPipeline> m_ap;
    GMutex m_mutex;
    GCond m_condition;
};

static const char* dumpAppendStage(AppendPipeline::AppendStage appendStage)
{
    switch (appendStage) {
    case AppendPipeline::AppendStage::Invalid:
        return "Invalid";
    case AppendPipeline::AppendStage::NotStarted:
        return "NotStarted";
    case AppendPipeline::AppendStage::Ongoing:
        return "Ongoing";
    case AppendPipeline::AppendStage::KeyNegotiation:
        return "KeyNegotiation";
    case AppendPipeline::AppendStage::NoDataToDecode:
        return "NoDataToDecode";
    case AppendPipeline::AppendStage::Sampling:
        return "Sampling";
    case AppendPipeline::AppendStage::LastSample:
        return "LastSample";
    case AppendPipeline::AppendStage::Aborting:
        return "Aborting";
    default:
        return "?";
    }
}

static void appendPipelineDemuxerPadAdded(GstElement*, GstPad*, AppendPipeline*);
static void appendPipelineDemuxerPadRemoved(GstElement*, GstPad*, AppendPipeline*);
static gboolean appendPipelineDemuxerConnectToAppSinkMainThread(PadInfo*);
static gboolean appendPipelineDemuxerDisconnectFromAppSinkMainThread(PadInfo*);
static void appendPipelineAppSinkCapsChanged(GObject*, GParamSpec*, AppendPipeline*);
static GstFlowReturn appendPipelineAppSinkNewSample(GstElement*, AppendPipeline*);
static gboolean appendPipelineAppSinkNewSampleMainThread(NewSampleInfo*);
static void appendPipelineAppSinkEOS(GstElement*, AppendPipeline*);
static gboolean appendPipelineAppSinkEOSMainThread(AppendPipeline* ap);
static gboolean appendPipelineNoDataToDecodeTimeout(AppendPipeline* ap);
static gboolean appendPipelineLastSampleTimeout(AppendPipeline* ap);

static void appendPipelineElementMessageCallback(GstBus*, GstMessage* message, AppendPipeline* ap)
{
    ap->handleElementMessage(message);
}

AppendPipeline::AppendPipeline(PassRefPtr<MediaSourceClientGStreamerMSE> mediaSourceClient, PassRefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate, MediaPlayerPrivateGStreamerMSE* playerPrivate)
    : m_mediaSourceClient(mediaSourceClient)
    , m_sourceBufferPrivate(sourceBufferPrivate)
    , m_playerPrivate(playerPrivate)
    , m_id(0)
    , m_appSinkCaps(NULL)
    , m_demuxerSrcPadCaps(NULL)
    , m_noDataToDecodeTimeoutTag(0)
    , m_lastSampleTimeoutTag(0)
    , m_appendStage(NotStarted)
    , m_abortPending(false)
    , m_streamType(Unknown)
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("%p", this);

    // TODO: give a name to the pipeline, maybe related with the track it's managing.
    // The track name is still unknown at this time, though.
    m_pipeline = gst_pipeline_new(NULL);

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline)));
    gst_bus_enable_sync_message_emission(bus.get());
    g_signal_connect(bus.get(), "sync-message::element", G_CALLBACK(appendPipelineElementMessageCallback), this);

    g_mutex_init(&m_newSampleMutex);
    g_cond_init(&m_newSampleCondition);

    m_decryptor = NULL;
    m_appsrc = gst_element_factory_make("appsrc", NULL);
    m_typefind = gst_element_factory_make("typefind", NULL);
    m_qtdemux = gst_element_factory_make("qtdemux", NULL);
    m_appsink = gst_element_factory_make("appsink", NULL);
    gst_app_sink_set_emit_signals(GST_APP_SINK(m_appsink), TRUE);
    gst_base_sink_set_sync(GST_BASE_SINK(m_appsink), FALSE);

    GRefPtr<GstPad> appSinkPad = gst_element_get_static_pad(m_appsink, "sink");
    g_signal_connect(appSinkPad.get(), "notify::caps", G_CALLBACK(appendPipelineAppSinkCapsChanged), this);

    // These signals won't be connected outside of the lifetime of "this".
    g_signal_connect(m_qtdemux, "pad-added", G_CALLBACK(appendPipelineDemuxerPadAdded), this);
    g_signal_connect(m_qtdemux, "pad-removed", G_CALLBACK(appendPipelineDemuxerPadRemoved), this);
    g_signal_connect(m_appsink, "new-sample", G_CALLBACK(appendPipelineAppSinkNewSample), this);
    g_signal_connect(m_appsink, "eos", G_CALLBACK(appendPipelineAppSinkEOS), this);

    // Add_many will take ownership of a reference. Request one ref more for ourselves.
    gst_object_ref(m_appsrc);
    gst_object_ref(m_typefind);
    gst_object_ref(m_qtdemux);
    gst_object_ref(m_appsink);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_typefind, m_qtdemux, NULL);
    gst_element_link_many(m_appsrc, m_typefind, m_qtdemux, NULL);

    gst_element_set_state(m_pipeline, GST_STATE_READY);
};

AppendPipeline::~AppendPipeline()
{
    g_assert(WTF::isMainThread());

    g_cond_signal(&m_newSampleCondition);
    g_cond_clear(&m_newSampleCondition);
    g_mutex_clear(&m_newSampleMutex);

    LOG_MEDIA_MESSAGE("%p", this);
    if (m_noDataToDecodeTimeoutTag) {
        LOG_MEDIA_MESSAGE("m_noDataToDecodeTimeoutTag=%u", m_noDataToDecodeTimeoutTag);
        // TODO: Maybe notify appendComplete here?
        g_source_remove(m_noDataToDecodeTimeoutTag);
        m_noDataToDecodeTimeoutTag = 0;
    }

    if (m_lastSampleTimeoutTag) {
        LOG_MEDIA_MESSAGE("m_lastSampleTimeoutTag=%u", m_lastSampleTimeoutTag);
        // TODO: Maybe notify appendComplete here?
        g_source_remove(m_lastSampleTimeoutTag);
        m_lastSampleTimeoutTag = 0;
    }

    if (m_pipeline) {
        GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline)));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(appendPipelineElementMessageCallback), this);
        gst_bus_disable_sync_message_emission(bus.get());

        gst_element_set_state (m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = NULL;
    }

    if (m_appsrc) {
        gst_object_unref(m_appsrc);
        m_appsrc = NULL;
    }

    if (m_typefind) {
        gst_object_unref(m_typefind);
        m_typefind = NULL;
    }

    if (m_qtdemux) {
        g_signal_handlers_disconnect_by_func(m_qtdemux, (gpointer)appendPipelineDemuxerPadAdded, this);
        g_signal_handlers_disconnect_by_func(m_qtdemux, (gpointer)appendPipelineDemuxerPadRemoved, this);

        gst_object_unref(m_qtdemux);
        m_qtdemux = NULL;
    }

    // Should have been freed by disconnectFromAppSink()
    g_assert(!m_decryptor);

    if (m_appsink) {
        GRefPtr<GstPad> appSinkPad = gst_element_get_static_pad(m_appsink, "sink");
        g_signal_handlers_disconnect_by_func(appSinkPad.get(), (gpointer)appendPipelineAppSinkCapsChanged, this);

        g_signal_handlers_disconnect_by_func(m_appsink, (gpointer)appendPipelineAppSinkNewSample, this);
        g_signal_handlers_disconnect_by_func(m_appsink, (gpointer)appendPipelineAppSinkEOS, this);

        gst_object_unref(m_appsink);
        m_appsink = NULL;
    }

    if (m_appSinkCaps) {
        gst_caps_unref(m_appSinkCaps);
        m_appSinkCaps = NULL;
    }

    if (m_demuxerSrcPadCaps) {
        gst_caps_unref(m_demuxerSrcPadCaps);
        m_demuxerSrcPadCaps = NULL;
    }

    m_playerPrivate = nullptr;
};

void AppendPipeline::clearPlayerPrivate()
{
    g_assert(WTF::isMainThread());
    LOG_MEDIA_MESSAGE("%s", "");

    g_mutex_lock(&m_newSampleMutex);
    // Make sure that AppendPipeline won't process more data from now on and
    // instruct handleNewSample to abort itself from now on as well.
    setAppendStage(Invalid);

    // Awake any pending handleNewSample operation in the streaming thread.
    g_cond_signal(&m_newSampleCondition);
    g_mutex_unlock(&m_newSampleMutex);

    // And now that no handleNewSample operations will remain stalled waiting
    // for the main thread, stop the pipeline.
    if (m_pipeline)
        gst_element_set_state (m_pipeline, GST_STATE_NULL);

    m_playerPrivate = nullptr;
}

void AppendPipeline::handleElementMessage(GstMessage* message)
{
    g_assert(WTF::isMainThread());

    const GstStructure* structure = gst_message_get_structure(message);
    if (gst_structure_has_name(structure, "drm-key-needed"))
        setAppendStage(AppendPipeline::AppendStage::KeyNegotiation);

    // MediaPlayerPrivateGStreamerBase will take care of setting up encryption.
    if (m_playerPrivate)
        m_playerPrivate->handleElementMessage(message);
}

gint AppendPipeline::id()
{
    g_assert(WTF::isMainThread());

    static gint totalAudio = 0;
    static gint totalVideo = 0;
    static gint totalText = 0;

    if (m_id)
        return m_id;

    switch (m_streamType) {
    case Audio:
        totalAudio++;
        m_id = totalAudio;
        break;
    case Video:
        totalVideo++;
        m_id = totalVideo;
        break;
    case Text:
        totalText++;
        m_id = totalText;
        break;
    case Unknown:
        FALLTHROUGH;
    case Invalid:
        LOG_MEDIA_MESSAGE("Trying to get id for a pipeline of Unknown/Invalid type");
        g_assert_not_reached();
        break;
    }

    LOG_MEDIA_MESSAGE("streamType=%d, id=%d", static_cast<int>(m_streamType), m_id);

    return m_id;
}

void AppendPipeline::setAppendStage(AppendStage newAppendStage)
{
    g_assert(WTF::isMainThread());
    // Valid transitions:
    // NotStarted-->Ongoing-->NoDataToDecode-->NotStarted
    //           |         |                `->Aborting-->NotStarted
    //           |         `->Sampling-···->Sampling-->LastSample-->NotStarted
    //           |         |                                     `->Aborting-->NotStarted
    //           |         `->KeyNegotiation-->Ongoing-->[...]
    //           `->Aborting-->NotStarted
    AppendStage oldAppendStage = m_appendStage;
    AppendStage nextAppendStage = Invalid;

    bool ok = false;

    if (oldAppendStage != newAppendStage)
        LOG_MEDIA_MESSAGE("%s --> %s", dumpAppendStage(oldAppendStage), dumpAppendStage(newAppendStage));

    switch (oldAppendStage) {
    case NotStarted:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case Ongoing:
            ok = true;
            gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
            m_noDataToDecodeTimeoutTag = g_timeout_add(s_noDataToDecodeTimeoutMsec, GSourceFunc(appendPipelineNoDataToDecodeTimeout), this);
            break;
        case NotStarted:
            ok = true;
            break;
        case Aborting:
            ok = true;
            nextAppendStage = NotStarted;
            break;
        case Invalid:
            ok = true;
            break;
        default:
            break;
        }
        break;
    case KeyNegotiation:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case Ongoing:
            ok = true;
            m_noDataToDecodeTimeoutTag = g_timeout_add(s_noDataToDecodeTimeoutMsec, GSourceFunc(appendPipelineNoDataToDecodeTimeout), this);
            break;
        case Invalid:
            ok = true;
            if (m_noDataToDecodeTimeoutTag) {
                g_source_remove(m_noDataToDecodeTimeoutTag);
                m_noDataToDecodeTimeoutTag = 0;
            }
            if (m_lastSampleTimeoutTag) {
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            break;
        default:
            break;
        }
        break;
    case Ongoing:
        g_assert(m_noDataToDecodeTimeoutTag != 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case KeyNegotiation:
            ok = true;
            if (m_noDataToDecodeTimeoutTag) {
                g_source_remove(m_noDataToDecodeTimeoutTag);
                m_noDataToDecodeTimeoutTag = 0;
            }
            break;
        case NoDataToDecode:
            ok = true;
            if (m_noDataToDecodeTimeoutTag) {
                g_source_remove(m_noDataToDecodeTimeoutTag);
                m_noDataToDecodeTimeoutTag = 0;
            }
            m_mediaSourceClient->didReceiveAllPendingSamples(m_sourceBufferPrivate.get());
            if (m_abortPending)
                nextAppendStage = Aborting;
            else
                nextAppendStage = NotStarted;
            break;
        case Sampling:
            ok = true;
            if (m_noDataToDecodeTimeoutTag) {
                g_source_remove(m_noDataToDecodeTimeoutTag);
                m_noDataToDecodeTimeoutTag = 0;
            }

            if (m_lastSampleTimeoutTag) {
                LOG_MEDIA_MESSAGE("lastSampleTimeoutTag already exists while transitioning Ongoing-->Sampling");
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            m_lastSampleTimeoutTag = g_timeout_add(s_lastSampleTimeoutMsec, GSourceFunc(appendPipelineLastSampleTimeout), this);
            break;
        case Invalid:
            ok = true;
            if (m_noDataToDecodeTimeoutTag) {
                g_source_remove(m_noDataToDecodeTimeoutTag);
                m_noDataToDecodeTimeoutTag = 0;
            }
            if (m_lastSampleTimeoutTag) {
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            break;
        default:
            break;
        }
        break;
    case NoDataToDecode:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case NotStarted:
            ok = true;
            break;
        case Aborting:
            ok = true;
            nextAppendStage = NotStarted;
            break;
        case Invalid:
            ok = true;
            break;
        default:
            break;
        }
        break;
    case Sampling:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag != 0);
        switch (newAppendStage) {
        case Sampling:
            ok = true;
            if (m_lastSampleTimeoutTag) {
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            m_lastSampleTimeoutTag = g_timeout_add(s_lastSampleTimeoutMsec, GSourceFunc(appendPipelineLastSampleTimeout), this);
            break;
        case LastSample:
            ok = true;
            if (m_lastSampleTimeoutTag) {
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            m_mediaSourceClient->didReceiveAllPendingSamples(m_sourceBufferPrivate.get());
            if (m_abortPending)
                nextAppendStage = Aborting;
            else
                nextAppendStage = NotStarted;
            break;
        case Invalid:
            ok = true;
            if (m_lastSampleTimeoutTag) {
                g_source_remove(m_lastSampleTimeoutTag);
                m_lastSampleTimeoutTag = 0;
            }
            break;
        default:
            break;
        }
        break;
    case LastSample:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case NotStarted:
            ok = true;
            break;
        case Aborting:
            ok = true;
            nextAppendStage = NotStarted;
            break;
        case Invalid:
            ok = true;
            break;
        default:
            break;
        }
        break;
    case Aborting:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        switch (newAppendStage) {
        case NotStarted:
            ok = true;
            resetPipeline();
            m_abortPending = false;
            break;
        case Invalid:
            ok = true;
            break;
        default:
            break;
        }
        break;
    case Invalid:
        g_assert(m_noDataToDecodeTimeoutTag == 0);
        g_assert(m_lastSampleTimeoutTag == 0);
        break;
    }

    if (ok)
        m_appendStage = newAppendStage;
    else {
        ERROR_MEDIA_MESSAGE("Invalid append stage transition %s --> %s", dumpAppendStage(oldAppendStage), dumpAppendStage(newAppendStage));
    }

    g_assert(ok);

    if (nextAppendStage != Invalid)
        setAppendStage(nextAppendStage);
}

// Takes ownership of caps.
void AppendPipeline::parseDemuxerCaps(GstCaps* demuxerSrcPadCaps)
{
    g_assert(WTF::isMainThread());

    if (m_demuxerSrcPadCaps)
        gst_caps_unref(m_demuxerSrcPadCaps);
    m_demuxerSrcPadCaps = demuxerSrcPadCaps;

    GstStructure* s = gst_caps_get_structure(m_demuxerSrcPadCaps, 0);
    const gchar* structureName = gst_structure_get_name(s);
    GstVideoInfo info;
    bool sizeConfigured = false;

    m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Unknown;

#if GST_CHECK_VERSION(1, 5, 3)
    if (gst_structure_has_name(s, "application/x-cenc")) {
        const gchar* originalMediaType = gst_structure_get_string(s, "original-media-type");

        // Any previous decriptor should have been removed from the pipeline by disconnectFromAppSinkFromStreamingThread()
        g_assert(!m_decryptor);

        m_decryptor = WebCore::createGstDecryptor(gst_structure_get_string(s, "protection-system"));
        if (!m_decryptor) {
            ERROR_MEDIA_MESSAGE("decryptor not found for caps: %" GST_PTR_FORMAT, m_demuxerSrcPadCaps);
            return;
        }

        if (g_str_has_prefix(originalMediaType, "video/")) {
            int width = 0;
            int height = 0;
            float finalHeight = 0;

            gst_structure_get_int(s, "width", &width);
            if (gst_structure_get_int(s, "height", &height)) {
                gint par_n = 1;
                gint par_d = 1;

                gst_structure_get_fraction(s, "pixel-aspect-ratio", &par_n, &par_d);
                finalHeight = height * ((float) par_d / (float) par_n);
            }

            m_presentationSize = WebCore::FloatSize(width, finalHeight);
            m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Video;
        } else {
            m_presentationSize = WebCore::FloatSize();
            if (g_str_has_prefix(originalMediaType, "audio/"))
                m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Audio;
            else if (g_str_has_prefix(originalMediaType, "text/"))
                m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Text;
        }
        sizeConfigured = true;
    }
#endif

    if (!sizeConfigured) {
        if (g_str_has_prefix(structureName, "video/") && gst_video_info_from_caps(&info, demuxerSrcPadCaps)) {
            float width, height;

            width = info.width;
            height = info.height * ((float) info.par_d / (float) info.par_n);

            m_presentationSize = WebCore::FloatSize(width, height);
            m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Video;
        } else {
            m_presentationSize = WebCore::FloatSize();
            if (g_str_has_prefix(structureName, "audio/"))
                m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Audio;
            else if (g_str_has_prefix(structureName, "text/"))
                m_streamType = WebCore::MediaSourceStreamTypeGStreamer::Text;
        }
    }
}

void AppendPipeline::appSinkCapsChanged()
{
    g_assert(WTF::isMainThread());

    if (!m_appsink)
        return;

    GRefPtr<GstPad> pad = gst_element_get_static_pad(m_appsink, "sink");
    GstCaps* caps = gst_pad_get_current_caps(pad.get());

    if (!caps)
        return;

    // Transfer caps ownership to m_appSinkCaps.
    if (gst_caps_replace(&m_appSinkCaps, caps)) {
        if (m_playerPrivate)
            m_playerPrivate->trackDetected(this, m_oldTrack, m_track);
        didReceiveInitializationSegment();
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    }
}

void AppendPipeline::appSinkNewSample(GstSample* sample)
{
    g_assert(WTF::isMainThread());

    g_mutex_lock(&m_newSampleMutex);

    // Ignore samples if we're not expecting them. Refuse processing if we're in Invalid state.
    if (!(m_appendStage == Ongoing || m_appendStage == Sampling)) {
        LOG_MEDIA_MESSAGE("Unexpected sample, stage=%s", dumpAppendStage(m_appendStage));
        m_flowReturn = GST_FLOW_ERROR;
        g_cond_signal(&m_newSampleCondition);
        g_mutex_unlock(&m_newSampleMutex);
        return;
    }

    RefPtr<GStreamerMediaSample> mediaSample = WebCore::GStreamerMediaSample::create(sample, m_presentationSize, trackId());

    LOG_MEDIA_MESSAGE("trackId=%s PTS=%f presentationSize=%.0fx%.0f", mediaSample->trackID().string().utf8().data(), mediaSample->presentationTime().toFloat(), mediaSample->presentationSize().width(), mediaSample->presentationSize().height());

    // If we're beyond the duration, ignore this sample and the remaining ones.
    MediaTime duration = m_mediaSourceClient->duration();
    if (duration.isValid() && !duration.indefiniteTime() && mediaSample->presentationTime() > duration) {
        LOG_MEDIA_MESSAGE("Detected sample (%f) beyond the duration (%f), declaring LastSample", mediaSample->presentationTime().toFloat(), duration.toFloat());
        setAppendStage(LastSample);
        m_flowReturn = GST_FLOW_OK;
        g_cond_signal(&m_newSampleCondition);
        g_mutex_unlock(&m_newSampleMutex);
        return;
    }

    MediaTime timestampOffset(MediaTime::createWithDouble(m_sourceBufferPrivate->timestampOffset()));

    // Add a fake sample if a gap is detected before the first sample
    if (mediaSample->presentationTime() >= timestampOffset &&
        mediaSample->presentationTime() <= timestampOffset + MediaTime::createWithDouble(0.1)) {
        LOG_MEDIA_MESSAGE("Adding fake sample");
        RefPtr<WebCore::GStreamerMediaSample> fakeSample = WebCore::GStreamerMediaSample::createFakeSample(
                gst_sample_get_caps(sample), timestampOffset, mediaSample->decodeTime(), mediaSample->presentationTime() - timestampOffset, mediaSample->presentationSize(),
                mediaSample->trackID());
        m_sourceBufferPrivate->didReceiveSample(fakeSample);
    }

    m_sourceBufferPrivate->didReceiveSample(mediaSample);
    setAppendStage(Sampling);
    m_flowReturn = GST_FLOW_OK;
    g_cond_signal(&m_newSampleCondition);
    g_mutex_unlock(&m_newSampleMutex);
}

void AppendPipeline::appSinkEOS()
{
    g_assert(WTF::isMainThread());

    switch (m_appendStage) {
    // Ignored. Operation completion will be managed by the Aborting->NotStarted transition.
    case Aborting:
        return;
    // Finish Ongoing and Sampling stages.
    case Ongoing:
        setAppendStage(NoDataToDecode);
        break;
    case Sampling:
        setAppendStage(LastSample);
        break;
    default:
        LOG_MEDIA_MESSAGE("Unexpected EOS");
        break;
    }
}

void AppendPipeline::didReceiveInitializationSegment()
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("%s", "");

    WebCore::SourceBufferPrivateClient::InitializationSegment initializationSegment;

    LOG_MEDIA_MESSAGE("Nofifying SourceBuffer for track %s", m_track->id().string().utf8().data());
    initializationSegment.duration = m_mediaSourceClient->duration();
    switch (m_streamType) {
    case Audio:
        {
            WebCore::SourceBufferPrivateClient::InitializationSegment::AudioTrackInformation info;
            info.track = static_cast<AudioTrackPrivateGStreamer*>(m_track.get());
            info.description = WebCore::GStreamerMediaDescription::create(m_demuxerSrcPadCaps);
            initializationSegment.audioTracks.append(info);
        }
        break;
    case Video:
        {
            WebCore::SourceBufferPrivateClient::InitializationSegment::VideoTrackInformation info;
            info.track = static_cast<VideoTrackPrivateGStreamer*>(m_track.get());
            info.description = WebCore::GStreamerMediaDescription::create(m_demuxerSrcPadCaps);
            initializationSegment.videoTracks.append(info);
        }
        break;
    default:
        LOG_MEDIA_MESSAGE("Unsupported or unknown stream type");
        ASSERT_NOT_REACHED();
        break;
    }

    m_mediaSourceClient->didReceiveInitializationSegment(m_sourceBufferPrivate.get(), initializationSegment);
}

AtomicString AppendPipeline::trackId()
{
    g_assert(WTF::isMainThread());

    if (!m_track)
        return AtomicString();

    return m_track->id();
}

void AppendPipeline::resetPipeline()
{
    g_assert(WTF::isMainThread());
    LOG_MEDIA_MESSAGE("%s", "");
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    {
        static int i = 0;
        WTF::String  dotFileName = String::format("reset-pipeline-%d", ++i);
        gst_debug_bin_to_dot_file(GST_BIN(m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.utf8().data());
    }

}

void AppendPipeline::abort()
{
    g_assert(WTF::isMainThread());
    LOG_MEDIA_MESSAGE("%s", "");

    // Abort already ongoing.
    if (m_abortPending)
        return;

    m_abortPending = true;
    if (m_appendStage == NotStarted)
        setAppendStage(Aborting);
    // Else, the automatic stage transitions will take care when the ongoing append finishes.
}


GstFlowReturn AppendPipeline::handleNewSample(GstElement* appsink)
{
    LOG_MEDIA_MESSAGE("thread %d", WTF::currentThread());

    bool invalid;

    g_mutex_lock(&m_newSampleMutex);
    invalid = !m_playerPrivate || m_appendStage == Invalid;
    g_mutex_unlock(&m_newSampleMutex);

    if (invalid) {
        LOG_MEDIA_MESSAGE("AppendPipeline has been disabled, ignoring this sample");
        return GST_FLOW_ERROR;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));

    if (WTF::isMainThread()) {
        appSinkNewSample(sample);
    } else {
        g_mutex_lock(&m_newSampleMutex);
        if (!(!m_playerPrivate || m_appendStage == Invalid)) {
            NewSampleInfo* info = new NewSampleInfo(sample, this);
            g_timeout_add(0, GSourceFunc(appendPipelineAppSinkNewSampleMainThread), info);
            g_cond_wait(&m_newSampleCondition, &m_newSampleMutex);
            // We've been awaken because the sample was processed or because of
            // an exceptional condition (entered in Invalid state, destructor, etc.)
            // We can't reliably delete info here, appendPipelineAppSinkNewSampleMainThread will do it.
        }
        g_mutex_unlock(&m_newSampleMutex);
    }
    gst_sample_unref(sample);
    return m_flowReturn;
}

void AppendPipeline::connectToAppSinkFromAnyThread(GstPad* demuxerSrcPad)
{
    if (!m_appsink)
        return;

    LOG_MEDIA_MESSAGE("%s", "");

    GRefPtr<GstPad> sinkSinkPad = gst_element_get_static_pad(m_appsink, "sink");

    // Only one Stream per demuxer is supported.
    g_assert(!gst_pad_is_linked(sinkSinkPad.get()));

    if (WTF::isMainThread()) {
        connectToAppSink(demuxerSrcPad);
    } else {
        // Call connectToAppSink() in the main thread and wait.
        PadInfo* info = new PadInfo(demuxerSrcPad, this);
        info->lock();
        g_timeout_add(0, GSourceFunc(appendPipelineDemuxerConnectToAppSinkMainThread), info);
        info->wait();
        info->unlock();
        delete info;
    }

    // Must be done in the thread we were called from (usually streaming thread).
    bool isData;

    switch (m_streamType) {
    case WebCore::MediaSourceStreamTypeGStreamer::Audio:
    case WebCore::MediaSourceStreamTypeGStreamer::Video:
    case WebCore::MediaSourceStreamTypeGStreamer::Text:
        isData = true;
        break;
    default:
        isData = false;
        break;
    }

    if (isData) {
        LOG_MEDIA_MESSAGE("Encrypted stream: %s", m_decryptor ? "yes" : "no");
        // FIXME: Only add appsink one time. This method can be called several times.
        gst_bin_add(GST_BIN(m_pipeline), m_appsink);
        if (m_decryptor) {
            gst_object_ref(m_decryptor);
            gst_bin_add(GST_BIN(m_pipeline), m_decryptor);
            GRefPtr<GstPad> decryptorSrcPad = gst_element_get_static_pad(m_decryptor, "src");
            GRefPtr<GstPad> decryptorSinkPad = gst_element_get_static_pad(m_decryptor, "sink");
            gst_pad_link(demuxerSrcPad, decryptorSinkPad.get());
            gst_pad_link(decryptorSrcPad.get(), sinkSinkPad.get());
            gst_element_sync_state_with_parent(m_appsink);
            gst_element_sync_state_with_parent(m_decryptor);
        } else {
            gst_pad_link(demuxerSrcPad, sinkSinkPad.get());
            gst_element_sync_state_with_parent(m_appsink);
            //gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        }
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    }
}

void AppendPipeline::connectToAppSink(GstPad* demuxerSrcPad)
{
    g_assert(WTF::isMainThread());
    LOG_MEDIA_MESSAGE("%s", "");

    GRefPtr<GstPad> sinkSinkPad = gst_element_get_static_pad(m_appsink, "sink");

    // Only one Stream per demuxer is supported.
    g_assert(!gst_pad_is_linked(sinkSinkPad.get()));

    // TODO: Use RefPtr
    GstCaps* caps = gst_pad_get_current_caps(GST_PAD(demuxerSrcPad));

    {
        gchar* strcaps = gst_caps_to_string(caps);
        LOG_MEDIA_MESSAGE("%s", strcaps);
        g_free(strcaps);
    }

    if (!caps)
        return;

    m_oldTrack = m_track;

    // May create m_decryptor
    parseDemuxerCaps(gst_caps_ref(caps));

    switch (m_streamType) {
    case WebCore::MediaSourceStreamTypeGStreamer::Audio:
        if (m_playerPrivate)
            m_track = WebCore::AudioTrackPrivateGStreamer::create(m_playerPrivate->pipeline(), id(), sinkSinkPad.get());
        break;
    case WebCore::MediaSourceStreamTypeGStreamer::Video:
        if (m_playerPrivate)
            m_track = WebCore::VideoTrackPrivateGStreamer::create(m_playerPrivate->pipeline(), id(), sinkSinkPad.get());
        break;
    case WebCore::MediaSourceStreamTypeGStreamer::Text:
        m_track = WebCore::InbandTextTrackPrivateGStreamer::create(id(), sinkSinkPad.get());
        break;
    default:
        // No useful data, but notify anyway to complete the append operation (webKitMediaSrcLastSampleTimeout is cancelled and won't notify in this case)
        LOG_MEDIA_MESSAGE("(no data)");
        m_mediaSourceClient->didReceiveAllPendingSamples(m_sourceBufferPrivate.get());
        break;
    }

    gst_caps_unref(caps);
}

void AppendPipeline::disconnectFromAppSinkFromAnyThread()
{
    LOG_MEDIA_MESSAGE("%s", "");

    // Must be done in the thread we were called from (usually streaming thread).
    if (m_decryptor) {
        gst_element_unlink(m_decryptor, m_appsink);
        gst_element_unlink(m_qtdemux, m_decryptor);
        gst_element_set_state(m_decryptor, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline), m_decryptor);
    } else
        gst_element_unlink(m_qtdemux, m_appsink);

    // Call disconnectFromAppSink() in the main thread and wait.
    // TODO: Optimize this and call only when there's m_decryptor. By now I call it always to keep code symmetry.
    if (WTF::isMainThread()) {
        disconnectFromAppSink();
    } else {
        PadInfo* info = new PadInfo(NULL, this);
        info->lock();
        g_timeout_add(0, GSourceFunc(appendPipelineDemuxerDisconnectFromAppSinkMainThread), info);
        info->wait();
        info->unlock();
        delete info;
    }
}

void AppendPipeline::disconnectFromAppSink()
{
    g_assert(WTF::isMainThread());
    LOG_MEDIA_MESSAGE("%s", "");

    if (m_decryptor) {
        gst_object_unref(m_decryptor);
        m_decryptor = NULL;
    }
}

static gboolean appSinkCapsChangedFromMainThread(gpointer data)
{
    AppendPipeline* ap = reinterpret_cast<AppendPipeline*>(data);
    ap->appSinkCapsChanged();
    ap->deref();
    return G_SOURCE_REMOVE;
}

static void appendPipelineAppSinkCapsChanged(GObject*, GParamSpec*, AppendPipeline* ap)
{
    ap->ref();
    g_timeout_add(0, appSinkCapsChangedFromMainThread, ap);
}

static void appendPipelineDemuxerPadAdded(GstElement*, GstPad* demuxerSrcPad, AppendPipeline* ap)
{
    ap->connectToAppSinkFromAnyThread(demuxerSrcPad);
}

static gboolean appendPipelineDemuxerConnectToAppSinkMainThread(PadInfo* info)
{
    info->lock();
    info->ap()->connectToAppSink(info->demuxerSrcPad());
    info->signal();
    info->unlock();
    return G_SOURCE_REMOVE;
}

static gboolean appendPipelineDemuxerDisconnectFromAppSinkMainThread(PadInfo* info)
{
    info->lock();
    info->ap()->disconnectFromAppSink();
    info->signal();
    info->unlock();
    return G_SOURCE_REMOVE;
}

static void appendPipelineDemuxerPadRemoved(GstElement*, GstPad*, AppendPipeline* ap)
{
    ap->disconnectFromAppSinkFromAnyThread();
}

static GstFlowReturn appendPipelineAppSinkNewSample(GstElement* appsink, AppendPipeline* ap)
{
    return ap->handleNewSample(appsink);
}

static gboolean appendPipelineAppSinkNewSampleMainThread(NewSampleInfo* info)
{
    info->ap()->appSinkNewSample(info->sample());
    delete info;

    return G_SOURCE_REMOVE;
}

static void appendPipelineAppSinkEOS(GstElement*, AppendPipeline* ap)
{
    if (WTF::isMainThread())
        ap->appSinkEOS();
    else {
        ap->ref();
        g_timeout_add(0, GSourceFunc(appendPipelineAppSinkEOSMainThread), ap);
    }

    LOG_MEDIA_MESSAGE("%s main thread", (WTF::isMainThread())?"IS":"NOT");
}

static gboolean appendPipelineAppSinkEOSMainThread(AppendPipeline* ap)
{
    ap->appSinkEOS();
    ap->deref();
    return G_SOURCE_REMOVE;
}

static gboolean appendPipelineNoDataToDecodeTimeout(AppendPipeline* ap)
{
    LOG_MEDIA_MESSAGE("%s", "");
    if (ap->appendStage()==AppendPipeline::AppendStage::Invalid)
        return G_SOURCE_REMOVE;

    ap->setAppendStage(AppendPipeline::NoDataToDecode);
    return G_SOURCE_REMOVE;
}

static gboolean appendPipelineLastSampleTimeout(AppendPipeline* ap)
{
    LOG_MEDIA_MESSAGE("%s", "");
    if (ap->appendStage()==AppendPipeline::AppendStage::Invalid)
        return G_SOURCE_REMOVE;

    ap->setAppendStage(AppendPipeline::LastSample);
    return G_SOURCE_REMOVE;
}

PassRefPtr<MediaSourceClientGStreamerMSE> MediaSourceClientGStreamerMSE::create(MediaPlayerPrivateGStreamerMSE* playerPrivate)
{
    g_assert(WTF::isMainThread());

    // return adoptRef(new MediaSourceClientGStreamerMSE(playerPrivate));
    // No adoptRef because the ownership has already been transferred to MediaPlayerPrivateGStreamerMSE
    RefPtr<MediaSourceClientGStreamerMSE> client(adoptRef(new MediaSourceClientGStreamerMSE(playerPrivate)));
    playerPrivate->setMediaSourceClient(client);
    return client;
}

MediaSourceClientGStreamerMSE::MediaSourceClientGStreamerMSE(MediaPlayerPrivateGStreamerMSE* playerPrivate)
    : m_duration(MediaTime::invalidTime())
{
    g_assert(WTF::isMainThread());

    m_playerPrivate = playerPrivate;
}

MediaSourceClientGStreamerMSE::~MediaSourceClientGStreamerMSE()
{
    g_assert(WTF::isMainThread());

    // TODO: cancel m_noDataToDecodeTimeoutTag if active and perform appendComplete()
}

MediaSourcePrivate::AddStatus MediaSourceClientGStreamerMSE::addSourceBuffer(RefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate, const ContentType&)
{
    g_assert(WTF::isMainThread());

    if (!m_playerPrivate)
        return MediaSourcePrivate::AddStatus::NotSupported;

    RefPtr<AppendPipeline> ap = adoptRef(new AppendPipeline(this, sourceBufferPrivate, m_playerPrivate));
    LOG_MEDIA_MESSAGE("this=%p sourceBuffer=%p ap=%p", this, sourceBufferPrivate.get(), ap.get());
    m_playerPrivate->m_appendPipelinesMap.add(sourceBufferPrivate, ap);

    ASSERT(m_playerPrivate->m_playbackPipeline);

    return m_playerPrivate->m_playbackPipeline->addSourceBuffer(sourceBufferPrivate);
}

MediaTime MediaSourceClientGStreamerMSE::duration()
{
    g_assert(WTF::isMainThread());

    return m_duration;
}

void MediaSourceClientGStreamerMSE::durationChanged(const MediaTime& duration)
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("duration=%f", duration.toFloat());

    if (!duration.isValid() || duration.isPositiveInfinite() || duration.isNegativeInfinite())
        return;

    m_duration = duration;
    if (m_playerPrivate)
        m_playerPrivate->durationChanged();
}

void MediaSourceClientGStreamerMSE::abort(PassRefPtr<SourceBufferPrivateGStreamer> prpSourceBufferPrivate)
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("%s", "");

    if (!m_playerPrivate)
        return;

    RefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate = prpSourceBufferPrivate;
    RefPtr<AppendPipeline> ap = m_playerPrivate->m_appendPipelinesMap.get(sourceBufferPrivate);
    ap->abort();
}

bool MediaSourceClientGStreamerMSE::append(PassRefPtr<SourceBufferPrivateGStreamer> prpSourceBufferPrivate, const unsigned char* data, unsigned length)
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("%u bytes", length);

    if (!m_playerPrivate)
        return false;

    RefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate = prpSourceBufferPrivate;
    RefPtr<AppendPipeline> ap = m_playerPrivate->m_appendPipelinesMap.get(sourceBufferPrivate);
    GstBuffer* buffer = gst_buffer_new_and_alloc(length);
    gst_buffer_fill(buffer, 0, data, length);
    ap->setAppendStage(AppendPipeline::Ongoing);

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(ap->appsrc()), buffer);
    return (ret == GST_FLOW_OK);
}

void MediaSourceClientGStreamerMSE::markEndOfStream(MediaSourcePrivate::EndOfStreamStatus)
{
    g_assert(WTF::isMainThread());

    // TODO
}

void MediaSourceClientGStreamerMSE::removedFromMediaSource(RefPtr<SourceBufferPrivateGStreamer> sourceBufferPrivate)
{
    g_assert(WTF::isMainThread());

    if (!m_playerPrivate)
        return;

    RefPtr<AppendPipeline> ap = m_playerPrivate->m_appendPipelinesMap.get(sourceBufferPrivate);
    ap->clearPlayerPrivate();
    m_playerPrivate->m_appendPipelinesMap.remove(sourceBufferPrivate);
    // AppendPipeline destructor will take care of cleaning up when appropriate.

    ASSERT(m_playerPrivate->m_playbackPipeline);

    m_playerPrivate->m_playbackPipeline->removeSourceBuffer(sourceBufferPrivate);
}

void MediaSourceClientGStreamerMSE::flushAndEnqueueNonDisplayingSamples(Vector<RefPtr<MediaSample> > samples)
{
    g_assert(WTF::isMainThread());

    if (!m_playerPrivate)
        return;

    m_playerPrivate->m_playbackPipeline->flushAndEnqueueNonDisplayingSamples(samples);
}

void MediaSourceClientGStreamerMSE::enqueueSample(PassRefPtr<MediaSample> prpSample)
{
    g_assert(WTF::isMainThread());

    if (!m_playerPrivate)
        return;

    m_playerPrivate->m_playbackPipeline->enqueueSample(prpSample);
}

void MediaSourceClientGStreamerMSE::didReceiveInitializationSegment(SourceBufferPrivateGStreamer* sourceBuffer, const SourceBufferPrivateClient::InitializationSegment& initializationSegment)
{
    g_assert(WTF::isMainThread());

    sourceBuffer->didReceiveInitializationSegment(initializationSegment);
}

void MediaSourceClientGStreamerMSE::didReceiveAllPendingSamples(SourceBufferPrivateGStreamer* sourceBuffer)
{
    g_assert(WTF::isMainThread());

    LOG_MEDIA_MESSAGE("%s", "");
    sourceBuffer->didReceiveAllPendingSamples();
}

GRefPtr<WebKitMediaSrc> MediaSourceClientGStreamerMSE::webKitMediaSrc()
{
    g_assert(WTF::isMainThread());

    if (!m_playerPrivate)
        return GRefPtr<WebKitMediaSrc>(NULL);

    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(m_playerPrivate->m_webKitMediaSrc.get());

    ASSERT(WEBKIT_IS_MEDIA_SRC(source));

    return GRefPtr<WebKitMediaSrc>(source);
}

void MediaSourceClientGStreamerMSE::clearPlayerPrivate()
{
    g_assert(WTF::isMainThread());

    m_playerPrivate = nullptr;
}

} // namespace WebCore

#endif // USE(GSTREAMER)
