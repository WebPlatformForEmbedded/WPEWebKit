#pragma once

#if ENABLE(GAMEPAD)

#include <WebCore/PlatformGamepad.h>

struct wpe_gamepad;
struct wpe_gamepad_provider;

namespace WebCore {

class WPEGamepad : public PlatformGamepad {
public:
    WPEGamepad(struct wpe_gamepad_provider* provider, uint32_t gamepad_id, unsigned index);
    virtual ~WPEGamepad();

    const Vector<double>& axisValues() const final { return m_axisValues; }
    const Vector<double>& buttonValues() const final { return m_buttonValues; }

private:
    void updateButtonValues(struct wpe_gamepad*);
    void updateAxisValues(struct wpe_gamepad*);

    Vector<double> m_buttonValues;
    Vector<double> m_axisValues;

    struct wpe_gamepad* m_gamepad { nullptr };
};

} // namespace WebCore

#endif // ENABLE(GAMEPAD)
