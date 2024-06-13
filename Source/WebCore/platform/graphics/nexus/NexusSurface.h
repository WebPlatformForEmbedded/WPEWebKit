#pragma once

#if USE(NEXUS)

#include "IntSize.h"
#include <nexus_surface.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class NexusSurface : public ThreadSafeRefCounted<NexusSurface> {
public:
    NexusSurface(NEXUS_SurfaceHandle, const IntSize&);
    ~NexusSurface();

    NEXUS_SurfaceHandle handle() const { return m_handle; }
    const IntSize& size() const { return m_size; }

private:
    NEXUS_SurfaceHandle m_handle;
    IntSize m_size;
};

} // namespace WebCore

#endif // USE(NEXUS)
