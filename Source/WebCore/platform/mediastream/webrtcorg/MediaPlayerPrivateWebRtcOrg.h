#ifndef MediaPlayerPrivateWebRtcOrg_h
#define MediaPlayerPrivateWebRtcOrg_h

#include "MediaPlayerPrivate.h"
#include "IntRect.h"
#include "FloatRect.h"

#include "webrtc/video_frame.h"
#include "webrtc/media/base/videosinkinterface.h"

#include "RealtimeMediaSourceCenterWebRtcOrg.h"

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperPlatformLayerProxy.h"
#endif

namespace WebCore {

class MediaPlayerPrivateWebRtcOrg : public MediaPlayerPrivateInterface
//	, public WRTCInt::RTCVideoRendererClient
    , public RealtimeMediaSource::Observer
	, public rtc::VideoSinkInterface<cricket::VideoFrame>
#if USE(COORDINATED_GRAPHICS_THREADED)
    , public TextureMapperPlatformLayerProxyProvider
#endif
{
public:
    explicit MediaPlayerPrivateWebRtcOrg(MediaPlayer*);
    ~MediaPlayerPrivateWebRtcOrg();

    static void registerMediaEngine(MediaEngineRegistrar);

    bool hasVideo() const override { return true; }
    bool hasAudio() const override { return true; }

    void load(const String&) override { }
#if ENABLE(MEDIA_SOURCE)
    void load(const String&, MediaSourcePrivateClient*) override { }
#endif
#if ENABLE(MEDIA_STREAM)
    void load(MediaStreamPrivate&) override;
#endif
    void commitLoad() { }
    void cancelLoad() override { }

    void play() override;
    void pause() override;
    bool paused() const override { return m_paused; }
    bool seeking() const override { return false; }
    std::unique_ptr<PlatformTimeRanges> buffered() const override { return std::make_unique<PlatformTimeRanges>(); }
    bool didLoadingProgress() const override { return false; }
    void setVolume(float) override { }
    float volume() const override { return 0; }
    bool supportsMuting() const override { return true; }
    void setMuted(bool) override { }
    void setVisible(bool) override;
    void setSize(const IntSize&) override;
    void setPosition(const IntPoint&) override;
    void paint(GraphicsContext&, const FloatRect&) override { }

    MediaPlayer::NetworkState networkState() const override {return MediaPlayer::Empty; }
    MediaPlayer::ReadyState readyState() const { return MediaPlayer::HaveEnoughData; }

    FloatSize naturalSize() const override;

    bool supportsAcceleratedRendering() const override { return true; }

#if USE(COORDINATED_GRAPHICS_THREADED)
    PlatformLayer* platformLayer() const override { return const_cast<MediaPlayerPrivateWebRtcOrg*>(this); }
    RefPtr<TextureMapperPlatformLayerProxy> proxy() const override { return m_platformLayerProxy.copyRef(); }
    void swapBuffersIfNeeded() override { }
#else
    PlatformLayer* platformLayer() const override { return nullptr; }
#endif

    // RealtimeMediaSource::Observer
    void sourceStopped() override;
    void sourceMutedChanged() override;
    void sourceSettingsChanged() override;
    bool preventSourceFromStopping() override { return false; }

    // WRTCInt::VideoPlayerClient
    void renderFrame();
    void punchHole(int width, int height);
	// webrtc::VideoSinkInterface
	void OnFrame(const cricket::VideoFrame& frame) override;

  private:
    static void getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);
    void updateVideoRectangle();
    void tryAttachRenderer();
    void removeRenderer();

#if USE(COORDINATED_GRAPHICS_THREADED)
    void pushTextureToCompositor(RefPtr<Image> frame);
    RefPtr<TextureMapperPlatformLayerProxy> m_platformLayerProxy;
    RefPtr<GraphicsContext3D> m_context3D;
    Condition m_drawCondition;
    Lock m_drawMutex;
#endif
//    std::unique_ptr<WRTCInt::RTCVideoRenderer> m_rtcRenderer;

    MediaPlayer* m_player;
    IntSize m_size;
    IntPoint m_position;
    MediaStreamPrivate* m_stream {nullptr};
	webrtc::MediaStreamInterface* m_rtcStream;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> m_renderedTrack;
	std::unique_ptr<uint8_t[]> m_imageBuffer;
    int m_width {0};
    int m_height {0};

    bool m_paused {true};
};

}
#endif
