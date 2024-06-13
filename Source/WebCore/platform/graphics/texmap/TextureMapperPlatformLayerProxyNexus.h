#pragma once

#include "TextureMapperPlatformLayerProxy.h"

#if USE(COORDINATED_GRAPHICS)

#include "NexusSurface.h"
#include "TextureMapperFlags.h"
#include "TextureMapperGLHeaders.h"
#include "TextureMapperPlatformLayer.h"
#include <wtf/OptionSet.h>

namespace WebCore {

class TextureMapperPlatformLayerProxyNexus final : public TextureMapperPlatformLayerProxy {
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit TextureMapperPlatformLayerProxyNexus(ContentType);
    virtual ~TextureMapperPlatformLayerProxyNexus();

    bool isNexusBased() const override { return true; }

    void activateOnCompositingThread(Compositor*, TextureMapperLayer*) override;
    void invalidate() override;
    void swapBuffer() override;

    void presentSurface(RefPtr<NexusSurface>&&, OptionSet<TextureMapperFlags>);

    struct NexusSurfaceLayer : public ThreadSafeRefCounted<NexusSurfaceLayer>, public TextureMapperPlatformLayer {
        WTF_MAKE_FAST_ALLOCATED;
    public:
        NexusSurfaceLayer(RefPtr<NexusSurface>&&, OptionSet<TextureMapperFlags>);

        void paintToTextureMapper(TextureMapper&, const FloatRect&, const TransformationMatrix& modelViewMatrix = { }, float opacity = 1.0) final;

        const RefPtr<NexusSurface>& nexusSurface() const { return m_nexusSurface; }

    private:
        friend class TextureMapperPlatformLayerProxyNexus;

        RefPtr<NexusSurface> m_nexusSurface;
        OptionSet<TextureMapperFlags> m_flags;

        struct GLData;
        std::unique_ptr<GLData> m_glData;
    };

private:
#if ASSERT_ENABLED
    RefPtr<Thread> m_compositorThread;
#endif

    RefPtr<NexusSurfaceLayer> m_pendingLayer;
    RefPtr<NexusSurfaceLayer> m_committedLayer;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_TEXTUREMAPPER_PLATFORMLAYERPROXY(TextureMapperPlatformLayerProxyNexus, isNexusBased());

#endif // USE(COORDINATED_GRAPHICS)
