#pragma once

#if ENABLE(GAMEPAD)
#include <wtf/Vector.h>
#include <wtf/HashMap.h>

#include "Timer.h"
#include "GamepadProvider.h"
#include "WPEGamepad.h"

struct wpe_gamepad_provider;
struct wpe_view_backend;

namespace WebCore {

class WPEGamepadProvider : public GamepadProvider {
    WTF_MAKE_NONCOPYABLE(WPEGamepadProvider);
    friend class NeverDestroyed<WPEGamepadProvider>;
public:
    WEBCORE_EXPORT static WPEGamepadProvider& singleton();

    WEBCORE_EXPORT void startMonitoringGamepads(GamepadProviderClient&) final;
    WEBCORE_EXPORT void stopMonitoringGamepads(GamepadProviderClient&) final;
    const Vector<PlatformGamepad*>& platformGamepads() final { return m_gamepadVector; }

    WEBCORE_EXPORT void platformStopMonitoringInput();
    WEBCORE_EXPORT void platformStartMonitoringInput();
    void scheduleInputNotification(struct wpe_gamepad* gamepad, bool shouldMakeGamepadsVisibile = false);

    WEBCORE_EXPORT struct wpe_view_backend* viewForGamepadInput();
private:
    WPEGamepadProvider();

    void gamepadConnected(uint32_t id);
    void gamepadDiconnected(uint32_t id);

    void connectionDelayTimerFired();
    void inputNotificationTimerFired();

    Vector<PlatformGamepad*> m_gamepadVector;
    HashMap<uint32_t, std::unique_ptr<WPEGamepad>> m_gamepadMap;
    Timer m_connectionDelayTimer;
    Timer m_inputNotificationTimer;
    bool m_shouldDispatchCallbacks { false };

    struct wpe_gamepad_provider* m_provider { nullptr };
    struct wpe_gamepad* m_lastActiveGamepad { nullptr };
};

} // namespace WebCore

#endif // ENABLE(GAMEPAD)
