/*
 * Copyright (C) 2015 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if USE(COORDINATED_GRAPHICS)

#include <wtf/Function.h>
#include <wtf/Lock.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class IntSize;
class TextureMapperGL;
class TextureMapperLayer;
class TextureMapperPlatformLayerBuffer;

class TextureMapperPlatformLayerProxy : public ThreadSafeRefCounted<TextureMapperPlatformLayerProxy> {
    WTF_MAKE_FAST_ALLOCATED();
public:
    class Compositor {
    public:
        virtual void onNewBufferAvailable() = 0;
    };

    TextureMapperPlatformLayerProxy();
    virtual ~TextureMapperPlatformLayerProxy();

    virtual bool isGLBased() const { return false; }
    virtual bool isDMABufBased() const { return false; }
    virtual bool isEmpty() const = 0;

    Lock& lock() WTF_RETURNS_LOCK(m_lock) { return m_lock; }
    bool isActive();

    virtual void activateOnCompositingThread(Compositor*, TextureMapperLayer*) = 0;
    virtual void invalidate() = 0;
    virtual void swapBuffer() = 0;

protected:
#if ENABLE(VIDEO) && USE(GSTREAMER)
    // layerAttachedCallback is invoked on the compositing thread whenever a new (non-null)
    // target layer is attached, e.g. because the previous layer was destroyed and recreated,
    // which happens whenever the render tree is rebuilt, such as when a page is restored from
    // the back/forward cache. This lets the owning media player re-deliver its current frame so
    // the new layer isn't left blank until the next decoded sample arrives (which, for a paused
    // player, may never happen).
    TextureMapperPlatformLayerProxy(Function<void()>&& layerAttachedCallback);
#endif

    Lock m_lock;
    Compositor* m_compositor { nullptr };
    TextureMapperLayer* m_targetLayer { nullptr };
#if ENABLE(VIDEO) && USE(GSTREAMER)
    Function<void()> m_layerAttachedCallback;
#endif
};

} // namespace WebCore

#define SPECIALIZE_TYPE_TRAITS_TEXTUREMAPPER_PLATFORMLAYERPROXY(ToClassName, predicate) \
    SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::ToClassName) \
    static bool isType(const WebCore::TextureMapperPlatformLayerProxy& proxy) { return proxy.predicate; } \
    SPECIALIZE_TYPE_TRAITS_END()

#endif // USE(COORDINATED_GRAPHICS)
