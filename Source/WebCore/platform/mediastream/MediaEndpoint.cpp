/*
 *
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM)
#include "MediaEndpoint.h"

namespace WebCore {

static std::unique_ptr<MediaEndpoint> createMediaEndpoint(MediaEndpointClient*)
{
    return nullptr;
}

CreateMediaEndpoint MediaEndpoint::create = createMediaEndpoint;

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
