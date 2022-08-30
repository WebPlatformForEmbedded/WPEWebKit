#include "config.h"

#if ENABLE(GAMEPAD)

#include <wpe/wpe.h>

#include "WPEGamepad.h"
#include "WPEGamepadProvider.h"

namespace WebCore {

WPEGamepad::WPEGamepad(struct wpe_gamepad_provider* provider, uint32_t gamepad_id, unsigned index)
    : PlatformGamepad(index)
{
    m_connectTime = m_lastUpdateTime = MonotonicTime::now();
    m_gamepad = wpe_gamepad_create(provider, gamepad_id);

    if (m_gamepad) {
        static struct wpe_gamepad_client s_client = {
            // button_values_changed
            [](struct wpe_gamepad* gamepad, void *data) {
                auto& self = *reinterpret_cast<WPEGamepad*>(data);
                self.updateButtonValues(gamepad);
            },
            // axis_values_changed
            [] (struct wpe_gamepad* gamepad, void *data) {
                auto& self = *reinterpret_cast<WPEGamepad*>(data);
                self.updateAxisValues(gamepad);
            }
        };
        wpe_gamepad_set_client(m_gamepad, &s_client, this);

        m_id = wpe_gamepad_get_device_name(m_gamepad);

        m_buttonValues.resize(wpe_gamepad_get_button_count(m_gamepad));
        m_buttonValues.fill(0);

        m_axisValues.resize(wpe_gamepad_get_axis_count(m_gamepad));
        m_axisValues.fill(0);
    }
}

WPEGamepad::~WPEGamepad()
{
    if (m_gamepad) {
        wpe_gamepad_set_client(m_gamepad, nullptr, nullptr);
        wpe_gamepad_destroy(m_gamepad);
        m_gamepad = nullptr;
    }
}

void WPEGamepad::updateButtonValues(struct wpe_gamepad* gamepad)
{
    if (gamepad != m_gamepad)
        return;

    wpe_gamepad_copy_button_values(gamepad, m_buttonValues.data(), m_buttonValues.size());
    m_lastUpdateTime = MonotonicTime::now();

    WPEGamepadProvider::singleton().scheduleInputNotification(gamepad, true);
}

void WPEGamepad::updateAxisValues(struct wpe_gamepad* gamepad)
{
    if (gamepad != m_gamepad)
        return;

    wpe_gamepad_copy_axis_values(gamepad, m_axisValues.data(), m_axisValues.size());
    m_lastUpdateTime = MonotonicTime::now();

    WPEGamepadProvider::singleton().scheduleInputNotification(gamepad);
}


} // namespace WebCore

#endif // ENABLE(GAMEPAD)
