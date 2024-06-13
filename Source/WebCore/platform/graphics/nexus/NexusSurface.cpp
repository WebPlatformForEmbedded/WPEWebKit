#include "config.h"
#include "NexusSurface.h"

#if USE(NEXUS)

namespace WebCore {

NexusSurface::NexusSurface(NEXUS_SurfaceHandle handle, const IntSize& size)
    : m_handle(handle)
    , m_size(size)
{ }

NexusSurface::~NexusSurface()
{
    if (m_handle)
        NEXUS_Surface_Destroy(m_handle);
    m_handle = nullptr;
}

} // namespace WebCore

#endif // USE(NEXUS)
