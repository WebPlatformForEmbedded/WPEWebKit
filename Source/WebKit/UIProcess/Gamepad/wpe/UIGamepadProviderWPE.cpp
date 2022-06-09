#include "config.h"

#if ENABLE(GAMEPAD)

#include "UIGamepadProvider.h"
#include "WPEView.h"

#include <WebCore/WPEGamepadProvider.h>

using namespace WebCore;

namespace WebKit {

void UIGamepadProvider::platformSetDefaultGamepadProvider()
{
    GamepadProvider::setSharedProvider(WPEGamepadProvider::singleton());
}

WebPageProxy* UIGamepadProvider::platformWebPageProxyForGamepadInput()
{
    return WKWPE::View::platformWebPageProxyForGamepadInput();
}

void UIGamepadProvider::platformStopMonitoringInput()
{
    WPEGamepadProvider::singleton().platformStopMonitoringInput();
}

void UIGamepadProvider::platformStartMonitoringInput()
{
    WPEGamepadProvider::singleton().platformStartMonitoringInput();
}

}

#endif // ENABLE(GAMEPAD)
