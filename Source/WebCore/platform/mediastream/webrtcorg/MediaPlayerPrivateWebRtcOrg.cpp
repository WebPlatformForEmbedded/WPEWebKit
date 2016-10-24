#include "config.h"
#include "MediaPlayerPrivateWebRtcOrg.h"
#include "RealtimeMediaSourceCenterWebRtcOrg.h"

#include <iostream>

#include <wtf/HashSet.h>
#include <wtf/text/WTFString.h>
#include <wtf/NeverDestroyed.h>

#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperGL.h"
#include "TextureMapperPlatformLayerBuffer.h"
#endif

#include "BitmapImage.h"
#include "BitmapTextureGL.h"
#include "FloatRect.h"

#include <cairo.h>

#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "TimeRanges.h"
#include "MediaStreamPrivate.h"

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectionfactory.h"

#include "webrtc/base/common.h"
#include "webrtc/base/nullsocketserver.h"
#include "webrtc/base/refcount.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/media/base/videosourceinterface.h"
#include "webrtc/video_frame.h"

namespace {

class ConditionNotifier {
public:
    ConditionNotifier(Lock& lock, Condition& condition)
        : m_locker(lock), m_condition(condition)
    {
    }
    ~ConditionNotifier()
    {
        m_condition.notifyOne();
    }
private:
    LockHolder m_locker;
    Condition& m_condition;
};

}  // namespace

using namespace rtc;
using namespace cricket;

typedef struct VideoSinkWants VSW;

namespace WebCore {
//using namespace rtc;

void MediaPlayerPrivateWebRtcOrg::getSupportedTypes(HashSet<String, ASCIICaseInsensitiveHash>& types)
{
    static NeverDestroyed<HashSet<String, ASCIICaseInsensitiveHash>> cache;
    types = cache;
}

MediaPlayer::SupportsType MediaPlayerPrivateWebRtcOrg::supportsType(const MediaEngineSupportParameters& parameters)
{
    if (parameters.isMediaStream)
        return MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

void MediaPlayerPrivateWebRtcOrg::registerMediaEngine(MediaEngineRegistrar registrar)
{
    registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateWebRtcOrg>(player); },
            getSupportedTypes, supportsType, 0, 0, 0,
              [](const String&, const String&) { return false; });
}

MediaPlayerPrivateWebRtcOrg::MediaPlayerPrivateWebRtcOrg(MediaPlayer* player)
    : m_player(player)
    , m_size(320, 240)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    m_platformLayerProxy = adoptRef(new TextureMapperPlatformLayerProxy());
    LockHolder locker(m_platformLayerProxy->lock());
    m_platformLayerProxy->pushNextBuffer(
        std::make_unique<TextureMapperPlatformLayerBuffer>(0, m_size, TextureMapperGL::ShouldOverwriteRect));
#endif
}

MediaPlayerPrivateWebRtcOrg::~MediaPlayerPrivateWebRtcOrg()
{
    removeRenderer();
    m_player = 0;
}

void MediaPlayerPrivateWebRtcOrg::load(MediaStreamPrivate& stream)
{
    m_stream = &stream;
    m_player->readyStateChanged();
}

FloatSize MediaPlayerPrivateWebRtcOrg::naturalSize() const
{
    if (!m_stream)
        return FloatSize();
    return m_stream->intrinsicSize();
}

void MediaPlayerPrivateWebRtcOrg::setSize(const IntSize& size)
{
    if (size == m_size)
        return;
    m_size = size;
    updateVideoRectangle();
}

void MediaPlayerPrivateWebRtcOrg::setPosition(const IntPoint& position)
{
    if (position == m_position)
        return;
    m_position = position;
    updateVideoRectangle();
}

void MediaPlayerPrivateWebRtcOrg::play()
{
    if (!m_stream || !m_stream->isProducingData())
        return;
    m_paused = false;
    tryAttachRenderer();
}

void MediaPlayerPrivateWebRtcOrg::pause()
{
    m_paused = true;
    removeRenderer();
}

void MediaPlayerPrivateWebRtcOrg::setVisible(bool visible)
{
    if (visible)
        tryAttachRenderer();
    else
        removeRenderer();
}

void MediaPlayerPrivateWebRtcOrg::updateVideoRectangle()
{
//    if (m_rtcRenderer)
//        m_rtcRenderer->setVideoRectangle(m_position.x(), m_position.y(), m_size.width(), m_size.height());
}

void MediaPlayerPrivateWebRtcOrg::tryAttachRenderer()
{
//    if (m_rtcRenderer)
//        return;
	if (m_rtcStream) {
		return;
	}

    if (!m_stream || !m_stream->isProducingData())
        return;

    if (m_paused || !m_player->visible())
        return;

    MediaStreamTrackPrivate *videoTrack = m_stream->activeVideoTrack();
    if (!videoTrack)
        return;

    RealtimeVideoSourceWebRtcOrg& videoSource = static_cast<RealtimeVideoSourceWebRtcOrg&>(videoTrack->source());
	m_rtcStream = videoSource.rtcStream();
	ASSERT(m_rtcStream);
	m_renderedTrack = m_rtcStream->GetVideoTracks().at(0);
	if (m_renderedTrack) {
		VSW vsw;
		m_renderedTrack->AddOrUpdateSink(this, vsw);
	}
//    m_rtcRenderer.reset(getRTCMediaSourceCenter().createVideoRenderer(videoSource.rtcStream(), this));
    videoSource.addObserver(this);

    updateVideoRectangle();
}

void MediaPlayerPrivateWebRtcOrg::OnFrame(const cricket::VideoFrame& frame) 
{
    if (frame.width() <= 0 || frame.height() <= 0) {
    	m_width = m_height = 0;
        m_imageBuffer.reset();
        return;
    }
    if (frame.width() != m_width || frame.height() != m_height) {
    	m_width = frame.width();
        m_height = frame.height();
        m_imageBuffer.reset(new uint8_t[m_width * m_width * 4]);
    }
    size_t size = m_width * m_width * 4;
    size_t converted_size = frame.ConvertToRgbBuffer(
    cricket::FOURCC_ARGB, m_imageBuffer.get(), size, m_width * 4);
    if (converted_size != 0 && size >= converted_size) {
    	renderFrame();
    } else {
    	std::cout << "Failed to convert to RGB buffer";
    }
}

void MediaPlayerPrivateWebRtcOrg::removeRenderer()
{
    if (!m_rtcStream)
        return;

    //m_rtcRenderer.reset();
	m_rtcStream = NULL;

    MediaStreamTrackPrivate *videoTrack = m_stream ? m_stream->activeVideoTrack() : nullptr;
    if (videoTrack) {
        RealtimeVideoSourceWebRtcOrg& videoSource = static_cast<RealtimeVideoSourceWebRtcOrg&>(videoTrack->source());
        videoSource.removeObserver(this);
    }
	if (m_renderedTrack) {
		m_renderedTrack->RemoveSink(this);
		m_renderedTrack = nullptr;
	}
}

void MediaPlayerPrivateWebRtcOrg::renderFrame()
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    cairo_format_t cairoFormat = CAIRO_FORMAT_ARGB32;
    int stride = cairo_format_stride_for_width (cairoFormat, m_width);
    //ASSERT(byteCount >= (m_height * stride));
    RefPtr<cairo_surface_t> surface = adoptRef(
        cairo_image_surface_create_for_data(
            (unsigned char*)m_imageBuffer.get(), cairoFormat, m_width, m_height, stride));
    ASSERT(cairo_surface_status(surface.get()) == CAIRO_STATUS_SUCCESS);
    RefPtr<BitmapImage> frame = BitmapImage::create(WTFMove(surface));
    LockHolder lock(m_drawMutex);
    bool succeeded = m_platformLayerProxy->scheduleUpdateOnCompositorThread([this, frame] {
        ConditionNotifier notifier(m_drawMutex, m_drawCondition);
        pushTextureToCompositor(frame);
    });
    if (succeeded) {
        m_drawCondition.wait(m_drawMutex);
    } else {
        printf("***Error: scheduleUpdateOnCompositorThread failed\n");
    }
#endif
}

void MediaPlayerPrivateWebRtcOrg::punchHole(int width, int height)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    LockHolder lock(m_drawMutex);
    bool succeeded = m_platformLayerProxy->scheduleUpdateOnCompositorThread([this, width, height] {
        ConditionNotifier notifier(m_drawMutex, m_drawCondition);
        LockHolder holder(m_platformLayerProxy->lock());
        m_platformLayerProxy->pushNextBuffer(
            std::make_unique<TextureMapperPlatformLayerBuffer>(0, IntSize(width, height), TextureMapperGL::ShouldOverwriteRect));
    });
    if (succeeded) {
        m_drawCondition.wait(m_drawMutex);
    } else {
        printf("***Error: scheduleUpdateOnCompositorThread failed\n");
    }
#endif
}

#if USE(COORDINATED_GRAPHICS_THREADED)
void MediaPlayerPrivateWebRtcOrg::pushTextureToCompositor(RefPtr<Image> frame)
{
    LockHolder holder(m_platformLayerProxy->lock());
    if (!m_platformLayerProxy->isActive()) {
        printf("***Error: platformLayerProxy is not ready yet\n");
        return;
    }
    IntSize size(frame->width(), frame->height());
    std::unique_ptr<TextureMapperPlatformLayerBuffer> buffer =
        m_platformLayerProxy->getAvailableBuffer(size, GraphicsContext3D::DONT_CARE);
    if (UNLIKELY(!buffer)) {
        if (UNLIKELY(!m_context3D))
            m_context3D = GraphicsContext3D::create(
                GraphicsContext3D::Attributes(), nullptr, GraphicsContext3D::RenderToCurrentGLContext);
        RefPtr<BitmapTexture> texture = adoptRef(new BitmapTextureGL(m_context3D));
        texture->reset(size, BitmapTexture::SupportsAlpha);
        buffer = std::make_unique<TextureMapperPlatformLayerBuffer>(WTFMove(texture));
    }
    IntRect rect(IntPoint(),size);
    buffer->textureGL().updateContents(
        frame.get(), rect, /*offset*/ IntPoint(),
        BitmapTexture::UpdateContentsFlag::UpdateCanModifyOriginalImageData);
    m_platformLayerProxy->pushNextBuffer(WTFMove(buffer));
}
#endif

void MediaPlayerPrivateWebRtcOrg::sourceStopped()
{
    removeRenderer();
}

void MediaPlayerPrivateWebRtcOrg::sourceMutedChanged()
{
    // Ignored
}

void MediaPlayerPrivateWebRtcOrg::sourceSettingsChanged()
{
    // Ignored
}

}
