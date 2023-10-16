#ifndef VKCONSTS_H
#define VKCONSTS_H

#include "config.h"
#include "DOMWindowProperty.h"
#include <wtf/RefCounted.h>

namespace WebCore {

class VkConsts final : public RefCounted<VkConsts>, public DOMWindowProperty {
public:
    static Ref<VkConsts> create(DOMWindow& window) { return adoptRef(*new VkConsts(window)); }

private:
    explicit VkConsts(DOMWindow&);
};
} // namespace WebCore

#endif // VKCONSTS_H
