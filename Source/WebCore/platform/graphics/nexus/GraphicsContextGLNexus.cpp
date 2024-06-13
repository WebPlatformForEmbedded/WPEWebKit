#include "config.h"
#include "GraphicsContextGLNexus.h"

#if ENABLE(WEBGL) && USE(TEXTURE_MAPPER) && USE(NEXUS)

#include "ANGLEHeaders.h"
#include "Logging.h"
#include "NicosiaGCGLANGLELayer.h"
#include "PlatformLayerDisplayDelegate.h"
#include "PixelBuffer.h"

#if ENABLE(MEDIA_STREAM) || ENABLE(WEB_CODECS)
#include "VideoFrame.h"
#endif

#include <nexus_platform.h>

namespace WebCore {

RefPtr<GraphicsContextGLNexus> GraphicsContextGLNexus::create(GraphicsContextGLAttributes&& attributes)
{
    auto context = adoptRef(*new GraphicsContextGLNexus(WTFMove(attributes)));
    if (!context->initialize())
        return nullptr;
    LOG(WebGL, "Successfully initialized Nexus context %p", context.ptr());
    return context;
}

GraphicsContextGLNexus::GraphicsContextGLNexus(GraphicsContextGLAttributes&& attributes)
    : GraphicsContextGLANGLE(WTFMove(attributes))
{
}

GraphicsContextGLNexus::~GraphicsContextGLNexus()
{
}

RefPtr<GraphicsLayerContentsDisplayDelegate> GraphicsContextGLNexus::layerContentsDisplayDelegate()
{
    return m_layerContentsDisplayDelegate.copyRef();
}

#if ENABLE(MEDIA_STREAM) || ENABLE(WEB_CODECS)
RefPtr<VideoFrame> GraphicsContextGLGBM::surfaceBufferToVideoFrame(SurfaceBuffer)
{
    return { };
}
#endif // ENABLE(MEDIA_STREAM) || ENABLE(WEB_CODECS)

#if ENABLE(VIDEO)
bool GraphicsContextGLNexus::copyTextureFromMedia(MediaPlayer&, PlatformGLObject, GCGLenum, GCGLint, GCGLenum, GCGLenum, GCGLenum, bool, bool)
{
    return false;
}
#endif // ENABLE(VIDEO)

RefPtr<PixelBuffer> GraphicsContextGLNexus::readCompositedResults()
{
    return { };
}

void GraphicsContextGLNexus::setContextVisibility(bool)
{
}

void GraphicsContextGLNexus::prepareForDisplay()
{
    if (!makeContextCurrent())
        return;

    prepareTexture();
    GL_Flush();
}

bool GraphicsContextGLNexus::platformInitializeContext()
{
    m_isForWebGL2 = contextAttributes().isWebGL2;

    const char* clientExtensions = EGL_QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    LOG(WebGL, "clientExtensions: %s\n", clientExtensions);

    Vector<EGLint> displayAttributes {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_EGL_ANGLE,
        EGL_PLATFORM_ANGLE_NATIVE_PLATFORM_TYPE_ANGLE, /* EGL_PLATFORM_NEXUS_BRCM */ 0x32F0,
        EGL_NONE,
    };

    m_displayObj = EGL_GetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttributes.data());
    if (m_displayObj == EGL_NO_DISPLAY)
        return false;

    EGLint majorVersion, minorVersion;
    if (EGL_Initialize(m_displayObj, &majorVersion, &minorVersion) == EGL_FALSE) {
        LOG(WebGL, "EGLDisplay Initialization failed.");
        return false;
    }
    LOG(WebGL, "ANGLE initialised Major: %d Minor: %d", majorVersion, minorVersion);

    const char* displayExtensions = EGL_QueryString(m_displayObj, EGL_EXTENSIONS);
    LOG(WebGL, "Extensions: %s\n", displayExtensions);

    EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLint numberConfigsReturned = 0;
    EGL_ChooseConfig(m_displayObj, configAttributes, &m_configObj, 1, &numberConfigsReturned);
    if (numberConfigsReturned != 1) {
        LOG(WebGL, "EGLConfig Initialization failed.");
        return false;
    }
    LOG(WebGL, "Got EGLConfig");

    EGL_BindAPI(EGL_OPENGL_ES_API);
    if (EGL_GetError() != EGL_SUCCESS) {
        LOG(WebGL, "Unable to bind to OPENGL_ES_API");
        return false;
    }

    Vector<EGLint> eglContextAttributes;
    if (m_isForWebGL2) {
        eglContextAttributes.append(EGL_CONTEXT_CLIENT_VERSION);
        eglContextAttributes.append(3);
    } else {
        eglContextAttributes.append(EGL_CONTEXT_CLIENT_VERSION);
        eglContextAttributes.append(2);
        // ANGLE will upgrade the context to ES3 automatically unless this is specified.
        eglContextAttributes.append(EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE);
        eglContextAttributes.append(EGL_FALSE);
    }
    eglContextAttributes.append(EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE);
    eglContextAttributes.append(EGL_TRUE);
    // WebGL requires that all resources are cleared at creation.
    eglContextAttributes.append(EGL_ROBUST_RESOURCE_INITIALIZATION_ANGLE);
    eglContextAttributes.append(EGL_TRUE);
    // WebGL doesn't allow client arrays.
    eglContextAttributes.append(EGL_CONTEXT_CLIENT_ARRAYS_ENABLED_ANGLE);
    eglContextAttributes.append(EGL_FALSE);
    // WebGL doesn't allow implicit creation of objects on bind.
    eglContextAttributes.append(EGL_CONTEXT_BIND_GENERATES_RESOURCE_CHROMIUM);
    eglContextAttributes.append(EGL_FALSE);

    if (strstr(displayExtensions, "EGL_ANGLE_power_preference")) {
        eglContextAttributes.append(EGL_POWER_PREFERENCE_ANGLE);
        // EGL_LOW_POWER_ANGLE is the default. Change to
        // EGL_HIGH_POWER_ANGLE if desired.
        eglContextAttributes.append(EGL_LOW_POWER_ANGLE);
    }
    eglContextAttributes.append(EGL_NONE);

    auto sharingContext = EGL_NO_CONTEXT;

    m_contextObj = EGL_CreateContext(m_displayObj, m_configObj, sharingContext, eglContextAttributes.data());
    if (m_contextObj == EGL_NO_CONTEXT) {
        LOG(WebGL, "EGLContext Initialization failed.");
        return false;
    }
    if (!makeContextCurrent()) {
        LOG(WebGL, "ANGLE makeContextCurrent failed.");
        return false;
    }
    LOG(WebGL, "Got EGLContext");
    return true;
}

bool GraphicsContextGLNexus::platformInitialize()
{
    m_nicosiaLayer = makeUnique<Nicosia::GCGLANGLELayer>(*this);
    m_layerContentsDisplayDelegate = PlatformLayerDisplayDelegate::create(&m_nicosiaLayer->contentLayer());

    bool success = makeContextCurrent();
    ASSERT_UNUSED(success, success);

    // We require this extension to render into the dmabuf-backed EGLImage.
    RELEASE_ASSERT(supportsExtension("GL_OES_EGL_image"_s));
    GL_RequestExtensionANGLE("GL_OES_EGL_image");

    validateAttributes();
    auto attributes = contextAttributes(); // They may have changed during validation.

    GLenum textureTarget = drawingBufferTextureTarget();
    // Create a texture to render into.
    GL_GenTextures(1, &m_texture);
    GL_BindTexture(textureTarget, m_texture);
    GL_TexParameterf(textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL_TexParameterf(textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL_TexParameteri(textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL_TexParameteri(textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL_BindTexture(textureTarget, 0);

    // Create an FBO.
    GL_GenFramebuffers(1, &m_fbo);
    GL_BindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Create a multisample FBO.
    ASSERT(m_state.boundReadFBO == m_state.boundDrawFBO);
    if (attributes.antialias) {
        GL_GenFramebuffers(1, &m_multisampleFBO);
        GL_BindFramebuffer(GL_FRAMEBUFFER, m_multisampleFBO);
        m_state.boundDrawFBO = m_state.boundReadFBO = m_multisampleFBO;
        GL_GenRenderbuffers(1, &m_multisampleColorBuffer);
        if (attributes.stencil || attributes.depth)
            GL_GenRenderbuffers(1, &m_multisampleDepthStencilBuffer);
    } else {
        // Bind canvas FBO.
        GL_BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        m_state.boundDrawFBO = m_state.boundReadFBO = m_fbo;
        if (attributes.stencil || attributes.depth)
            GL_GenRenderbuffers(1, &m_depthStencilBuffer);
    }

    return GraphicsContextGLANGLE::platformInitialize();
}

bool GraphicsContextGLNexus::reshapeDrawingBuffer()
{
    auto size = getInternalFramebufferSize();

    NEXUS_SurfaceCreateSettings surfSettings;
    NEXUS_Surface_GetDefaultCreateSettings(&surfSettings);
    surfSettings.compatibility.graphicsv3d = true;
    surfSettings.width = size.width();
    surfSettings.height = size.height();
    surfSettings.pixelFormat = NEXUS_PixelFormat_eA8_B8_G8_R8;
    surfSettings.heap = NEXUS_Platform_GetFramebufferHeap(NEXUS_OFFSCREEN_SURFACE);

    NEXUS_SurfaceHandle surfaceHandle = NEXUS_Surface_Create(&surfSettings);

    m_nexusSurface = adoptRef(*new NexusSurface(surfaceHandle, size));

    std::array<EGLint, 5> attributes {
            EGL_WIDTH, EGLint(size.width()),
            EGL_HEIGHT, EGLint(size.height()),
            EGL_NONE
        };
    EGLImageKHR image = EGL_CreateImageKHR(platformDisplay(), EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, m_nexusSurface->handle(), attributes.data());

    auto [textureTarget, textureBinding] = drawingBufferTextureBindingPoint();
    ScopedRestoreTextureBinding restoreBinding(textureBinding, textureTarget, textureTarget != TEXTURE_RECTANGLE_ARB);

    GL_BindTexture(textureTarget, m_texture);
    GL_EGLImageTargetTexture2DOES(textureTarget, image);
    GL_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget, m_texture, 0);

    return true;
}

#if ENABLE(WEBXR)
bool GraphicsContextGLNexus::createFoveation(IntSize, IntSize, IntSize, std::span<const GCGLfloat>, std::span<const GCGLfloat>, std::span<const GCGLfloat>)
{
    return false;
}
void GraphicsContextGLNexus::enableFoveation(GCGLuint)
{
}
void GraphicsContextGLNexus::disableFoveation()
{
}
#endif

} // namespace WebCore

#endif // ENABLE(WEBGL) && USE(TEXTURE_MAPPER) && USE(NEXUS)
