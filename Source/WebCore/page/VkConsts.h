#ifndef VKCONSTS_H
#define VKCONSTS_H

#include "config.h"
#include "LocalDOMWindowProperty.h"
#include <wtf/RefCounted.h>

namespace WebCore {

class VkConsts final : public RefCounted<VkConsts>, public LocalDOMWindowProperty {
public:
    static Ref<VkConsts> create(LocalDOMWindow& window) { return adoptRef(*new VkConsts(window)); }

private:
    explicit VkConsts(LocalDOMWindow&);
};
} // namespace WebCore

#endif // VKCONSTS_H