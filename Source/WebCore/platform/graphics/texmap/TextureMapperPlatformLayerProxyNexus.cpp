#include "config.h"
#include "TextureMapperPlatformLayerProxyNexus.h"

#if USE(COORDINATED_GRAPHICS)

#include "PlatformDisplay.h"
#include "TextureMapperLayer.h"

#if USE(LIBEPOXY)
#include <epoxy/egl.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace WebCore {

struct TextureMapperPlatformLayerProxyNexus::NexusSurfaceLayer::GLData {
    WTF_MAKE_STRUCT_FAST_ALLOCATED;

    GLData(NEXUS_SurfaceHandle);
    ~GLData();

    EGLImageKHR image { EGL_NO_IMAGE_KHR };
    GLuint texture { 0 };
};

TextureMapperPlatformLayerProxyNexus::NexusSurfaceLayer::GLData::GLData(NEXUS_SurfaceHandle surface)
{
    auto& platformDisplay = PlatformDisplay::sharedDisplayForCompositing();

    std::array<EGLint, 1> attributes { EGL_NONE };
    image = eglCreateImageKHR(platformDisplay.eglDisplay(), EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)surface, attributes.data());

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    glBindTexture(GL_TEXTURE_2D, 0);
}

TextureMapperPlatformLayerProxyNexus::NexusSurfaceLayer::GLData::~GLData()
{
    auto& platformDisplay = PlatformDisplay::sharedDisplayForCompositing();
    if (image != EGL_NO_IMAGE_KHR)
        eglDestroyImageKHR(platformDisplay.eglDisplay(), image);
    image = EGL_NO_IMAGE_KHR;

    if (texture)
        glDeleteTextures(1, &texture);
    texture = 0;
}

TextureMapperPlatformLayerProxyNexus::TextureMapperPlatformLayerProxyNexus(ContentType contentType)
    : TextureMapperPlatformLayerProxy(contentType)
{
}

TextureMapperPlatformLayerProxyNexus::~TextureMapperPlatformLayerProxyNexus()
{
}

void TextureMapperPlatformLayerProxyNexus::activateOnCompositingThread(Compositor* compositor, TextureMapperLayer* targetLayer)
{
#if ASSERT_ENABLED
    if (!m_compositorThread)
        m_compositorThread = &Thread::current();
#endif
    ASSERT(m_compositorThread == &Thread::current());
    ASSERT(compositor);
    ASSERT(targetLayer);

    {
        Locker locker { m_lock };
        m_compositor = compositor;
        m_targetLayer = targetLayer;
    }
}

void TextureMapperPlatformLayerProxyNexus::invalidate()
{
    ASSERT(m_compositorThread == &Thread::current());
#if ASSERT_ENABLED
    m_compositorThread = nullptr;
#endif

    Locker locker { m_lock };

    m_pendingLayer = nullptr;
    m_committedLayer = nullptr;

    m_compositor = nullptr;
    m_targetLayer = nullptr;
}

void TextureMapperPlatformLayerProxyNexus::swapBuffer()
{
    Locker locker { m_lock };
    if (!m_targetLayer || !m_pendingLayer)
        return;

    auto previousLayer = WTFMove(m_committedLayer);
    m_committedLayer = WTFMove(m_pendingLayer);
    m_targetLayer->setContentsLayer(m_committedLayer.get());

    previousLayer = nullptr;

    if (!m_committedLayer->m_glData)
        m_committedLayer->m_glData = makeUnique<NexusSurfaceLayer::GLData>(m_committedLayer->m_nexusSurface->handle());
}

void TextureMapperPlatformLayerProxyNexus::presentSurface(RefPtr<NexusSurface>&& surface, OptionSet<TextureMapperFlags> flags)
{
    ASSERT(m_lock.isHeld());

    if (m_committedLayer && m_committedLayer->nexusSurface() == surface)
        m_pendingLayer = m_committedLayer.copyRef();
    else
        m_pendingLayer = adoptRef(*new NexusSurfaceLayer(WTFMove(surface), flags));

    if (m_compositor)
        m_compositor->onNewBufferAvailable();
}

TextureMapperPlatformLayerProxyNexus::NexusSurfaceLayer::NexusSurfaceLayer(RefPtr<NexusSurface>&& surface, OptionSet<TextureMapperFlags> flags)
    : m_nexusSurface(surface)
    , m_flags(flags)
{
}

void TextureMapperPlatformLayerProxyNexus::NexusSurfaceLayer::paintToTextureMapper(TextureMapper& textureMapper, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    if (!m_glData || m_glData->image == EGL_NO_IMAGE_KHR)
        return;

    textureMapper.drawTexture(m_glData->texture, m_flags, targetRect, modelViewMatrix, opacity);
}

} // namespace WebCore

#endif // USE(COORDINATED_GRAPHICS)
