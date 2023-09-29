/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2018 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NicosiaGCGLLayer.h"

#if ENABLE(WEBGL) && USE(NICOSIA) && USE(TEXTURE_MAPPER) && !USE(ANGLE)

#if USE(COORDINATED_GRAPHICS)
#include "TextureMapperGL.h"
#include "TextureMapperPlatformLayerBuffer.h"
#include "TextureMapperPlatformLayerProxyGL.h"
#endif

#include "GLContext.h"

namespace Nicosia {

using namespace WebCore;

#if USE(FORCE_WEBGL_BUFFER_SYNC)

#if HAVE(OPENGL_4) || HAVE(OPENGL_ES_3)
#define HAS_GL_SYNC_API
#endif // HAVE(OPENGL_4) || HAVE(OPENGL_ES_3)
class BufferGLSync : public TextureMapperPlatformLayerBuffer::UnmanagedBufferDataHolder
{
public:
    BufferGLSync() {
#if defined(HAS_GL_SYNC_API)
        m_sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
#else
        // Just call glFinish() if sync api is not available
        glFinish();
#endif
    }

#if defined(HAS_GL_SYNC_API)
    void waitForCPUSync() override {
        if (!m_sync)
            return;
        glWaitSync(m_sync, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(m_sync);
        m_sync = nullptr;
    }
    virtual ~BufferGLSync() {
        if (!m_sync)
            return;
        glDeleteSync(m_sync);
    }
private:
    GLsync m_sync = nullptr;
#endif
};
#endif // USE(FORCE_WEBGL_BUFFER_SYNC)

static std::unique_ptr<GLContext> s_windowContext;

static void terminateWindowContext()
{
    s_windowContext = nullptr;
}

std::unique_ptr<GCGLLayer> GCGLLayer::create(WebCore::GraphicsContextGLOpenGL& context)
{
    auto attributes = context.contextAttributes();

    if (attributes.renderTarget == GraphicsContextGLRenderTarget::Offscreen) {
        if (auto glContext = GLContext::createOffscreenContext(&PlatformDisplay::sharedDisplayForCompositing()))
            return makeUnique<GCGLLayer>(context, WTFMove(glContext));
    } else {
        if (!s_windowContext) {
            s_windowContext = GLContext::createContextForWindow(reinterpret_cast<GLNativeWindowType>(attributes.nativeWindowID), &PlatformDisplay::sharedDisplayForCompositing());
            std::atexit(terminateWindowContext);
        }
        if (s_windowContext)
            return makeUnique<GCGLLayer>(context);
    }

    return nullptr;
}

GCGLLayer::GCGLLayer(GraphicsContextGLOpenGL& context, std::unique_ptr<WebCore::GLContext>&& glContext)
    : m_context(context)
    , m_glContext(WTFMove(glContext))
    , m_contentLayer(Nicosia::ContentLayer::create(Nicosia::ContentLayerTextureMapperImpl::createFactory(*this)))
{
}

GCGLLayer::GCGLLayer(GraphicsContextGLOpenGL& context)
    : m_context(context)
    , m_contentLayer(Nicosia::ContentLayer::create(Nicosia::ContentLayerTextureMapperImpl::createFactory(*this)))
{
}

GCGLLayer::~GCGLLayer()
{
    downcast<ContentLayerTextureMapperImpl>(m_contentLayer->impl()).invalidateClient();
}

bool GCGLLayer::makeContextCurrent()
{
    if (m_context.contextAttributes().renderTarget == GraphicsContextGLRenderTarget::HostWindow) {
        ASSERT(s_windowContext);
        return s_windowContext->makeContextCurrent();
    }

    ASSERT(m_glContext);
    return m_glContext->makeContextCurrent();
}

GCGLContext GCGLLayer::platformContext() const
{
    if (m_context.contextAttributes().renderTarget == GraphicsContextGLRenderTarget::HostWindow) {
        ASSERT(s_windowContext);
        return s_windowContext->platformContext();
    }

    ASSERT(m_glContext);
    return m_glContext->platformContext();
}

void GCGLLayer::swapBuffersIfNeeded()
{
    if (m_context.contextAttributes().renderTarget == GraphicsContextGLRenderTarget::HostWindow) {
        ASSERT(s_windowContext);
        s_windowContext->swapBuffers();
        return;
    }

#if USE(COORDINATED_GRAPHICS)
    if (m_context.layerComposited())
        return;

    m_context.prepareTexture();
    IntSize textureSize(m_context.m_currentWidth, m_context.m_currentHeight);

    TextureMapperGL::Flags flags = TextureMapperGL::ShouldFlipTexture;
    if (m_context.contextAttributes().alpha)
        flags |= TextureMapperGL::ShouldBlend;

    {
        auto& proxy = downcast<Nicosia::ContentLayerTextureMapperImpl>(m_contentLayer->impl()).proxy();
        Locker locker { proxy.lock() };
        ASSERT(is<TextureMapperPlatformLayerProxyGL>(proxy));
        auto buffer = makeUnique<TextureMapperPlatformLayerBuffer>(m_context.m_compositorTexture, textureSize, flags, m_context.m_internalColorFormat);
#if USE(FORCE_WEBGL_BUFFER_SYNC)
        buffer->setUnmanagedBufferDataHolder(makeUnique<BufferGLSync>());
#endif // USE(FORCE_WEBGL_BUFFER_SYNC)
        downcast<TextureMapperPlatformLayerProxyGL>(proxy).pushNextBuffer(WTFMove(buffer));
    }

    m_context.markLayerComposited();
#endif
}

} // namespace Nicosia

#endif // ENABLE(WEBGL) && USE(NICOSIA) && USE(TEXTURE_MAPPER)
