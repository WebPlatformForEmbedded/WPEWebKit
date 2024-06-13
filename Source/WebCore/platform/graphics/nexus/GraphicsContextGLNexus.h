
#pragma once

#if ENABLE(WEBGL) && USE(TEXTURE_MAPPER) && USE(NEXUS)

#include "GraphicsContextGLANGLE.h"
#include "NexusSurface.h"
#include <memory>

namespace Nicosia {
class GCGLANGLELayer;
}

namespace WebCore {

class GraphicsContextGLNexus : public GraphicsContextGLANGLE {
public:
    static RefPtr<GraphicsContextGLNexus> create(GraphicsContextGLAttributes&&);
    virtual ~GraphicsContextGLNexus();

    // GraphicsContextGL overrides
    RefPtr<GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() final;

#if ENABLE(MEDIA_STREAM) || ENABLE(WEB_CODECS)
    RefPtr<VideoFrame> surfaceBufferToVideoFrame(SurfaceBuffer) override;
#endif
#if ENABLE(VIDEO)
    bool copyTextureFromMedia(MediaPlayer&, PlatformGLObject texture, GCGLenum target, GCGLint level, GCGLenum internalFormat, GCGLenum format, GCGLenum type, bool premultiplyAlpha, bool flipY) override;
#endif
    RefPtr<PixelBuffer> readCompositedResults() final;

    void setContextVisibility(bool) override;
    void prepareForDisplay() override;

    // GraphicsContextGLANGLE overrides
    bool platformInitializeContext() override;
    bool platformInitialize() override;

    bool reshapeDrawingBuffer() override;
#if ENABLE(WEBXR)
    bool createFoveation(IntSize, IntSize, IntSize, std::span<const GCGLfloat>, std::span<const GCGLfloat>, std::span<const GCGLfloat>) override;
    void enableFoveation(GCGLuint) override;
    void disableFoveation() override;
#endif

    const RefPtr<NexusSurface>& nexusSurface() const { return m_nexusSurface; }

protected:
    explicit GraphicsContextGLNexus(GraphicsContextGLAttributes&&);

private:
    std::unique_ptr<Nicosia::GCGLANGLELayer> m_nicosiaLayer;
    RefPtr<GraphicsLayerContentsDisplayDelegate> m_layerContentsDisplayDelegate;

    RefPtr<NexusSurface> m_nexusSurface;
};

} // namespace WebCore

#endif
