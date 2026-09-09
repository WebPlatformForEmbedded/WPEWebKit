/*
 * Copyright (C) 2020 Igalia S.L. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ManetteGamepad.h"

#if ENABLE(GAMEPAD) && OS(LINUX)

#ifndef MANETTE_CHECK_VERSION
#define MANETTE_CHECK_VERSION(major,minor,micro) 0
#endif

#include "ManetteGamepadProvider.h"
#include <linux/input-event-codes.h>
#include <wtf/HexNumber.h>
#include <wtf/text/CString.h>

namespace WebCore {

#if MANETTE_CHECK_VERSION(1, 0, 0)
static ManetteGamepad::StandardGamepadAxis toStandardGamepadAxis(ManetteAxis axis)
{
    switch (axis) {
    case MANETTE_AXIS_LEFT_X:
        return ManetteGamepad::StandardGamepadAxis::LeftStickX;
    case MANETTE_AXIS_LEFT_Y:
        return ManetteGamepad::StandardGamepadAxis::LeftStickY;
    case MANETTE_AXIS_RIGHT_X:
        return ManetteGamepad::StandardGamepadAxis::RightStickX;
    case MANETTE_AXIS_RIGHT_Y:
        return ManetteGamepad::StandardGamepadAxis::RightStickY;
    default:
        break;
    }
    return ManetteGamepad::StandardGamepadAxis::Unknown;
}
#else
static ManetteGamepad::StandardGamepadAxis toStandardGamepadAxis(uint16_t axis)
{
    switch (axis) {
    case ABS_X:
        return ManetteGamepad::StandardGamepadAxis::LeftStickX;
    case ABS_Y:
        return ManetteGamepad::StandardGamepadAxis::LeftStickY;
    case ABS_RX:
        return ManetteGamepad::StandardGamepadAxis::RightStickX;
    case ABS_RY:
        return ManetteGamepad::StandardGamepadAxis::RightStickY;
    default:
        break;
    }
    return ManetteGamepad::StandardGamepadAxis::Unknown;
}
#endif

#if MANETTE_CHECK_VERSION(1, 0, 0)
static void onAbsoluteAxisChanged(ManetteDevice* device, ManetteAxis axis, double value, ManetteGamepad* gamepad)
{
    if (axis == MANETTE_AXIS_LEFT_TRIGGER) {
        gamepad->analogButtonChanged(device, ManetteGamepad::StandardGamepadButton::LeftTrigger, value);
        return;
    }
    if (axis == MANETTE_AXIS_RIGHT_TRIGGER) {
        gamepad->analogButtonChanged(device, ManetteGamepad::StandardGamepadButton::RightTrigger, value);
        return;
    }

    gamepad->absoluteAxisChanged(device, toStandardGamepadAxis(axis), value);
}
#else
static void onAbsoluteAxisEvent(ManetteDevice* device, ManetteEvent* event, ManetteGamepad* gamepad)
{
    uint16_t axis;
    double value;
    if (!manette_event_get_absolute(event, &axis, &value))
        return;

    gamepad->absoluteAxisChanged(device, toStandardGamepadAxis(axis), value);
}
#endif

#if MANETTE_CHECK_VERSION(1, 0, 0)
static ManetteGamepad::StandardGamepadButton toStandardGamepadButton(ManetteButton button)
{
    switch (button) {
    case MANETTE_BUTTON_SOUTH:
        return ManetteGamepad::StandardGamepadButton::A;
    case MANETTE_BUTTON_EAST:
        return ManetteGamepad::StandardGamepadButton::B;
    case MANETTE_BUTTON_WEST:
        return ManetteGamepad::StandardGamepadButton::X;
    case MANETTE_BUTTON_NORTH:
        return ManetteGamepad::StandardGamepadButton::Y;
    case MANETTE_BUTTON_LEFT_SHOULDER:
        return ManetteGamepad::StandardGamepadButton::LeftShoulder;
    case MANETTE_BUTTON_RIGHT_SHOULDER:
        return ManetteGamepad::StandardGamepadButton::RightShoulder;
    case MANETTE_BUTTON_SELECT:
        return ManetteGamepad::StandardGamepadButton::Select;
    case MANETTE_BUTTON_START:
        return ManetteGamepad::StandardGamepadButton::Start;
    case MANETTE_BUTTON_LEFT_STICK:
        return ManetteGamepad::StandardGamepadButton::LeftStick;
    case MANETTE_BUTTON_RIGHT_STICK:
        return ManetteGamepad::StandardGamepadButton::RightStick;
    case MANETTE_BUTTON_DPAD_UP:
        return ManetteGamepad::StandardGamepadButton::DPadUp;
    case MANETTE_BUTTON_DPAD_DOWN:
        return ManetteGamepad::StandardGamepadButton::DPadDown;
    case MANETTE_BUTTON_DPAD_LEFT:
        return ManetteGamepad::StandardGamepadButton::DPadLeft;
    case MANETTE_BUTTON_DPAD_RIGHT:
        return ManetteGamepad::StandardGamepadButton::DPadRight;
    case MANETTE_BUTTON_MODE:
        return ManetteGamepad::StandardGamepadButton::Mode;
    default:
        break;
    }
    return ManetteGamepad::StandardGamepadButton::Unknown;
}
#else
static ManetteGamepad::StandardGamepadButton toStandardGamepadButton(uint16_t manetteButton)
{
    switch (manetteButton) {
    case BTN_A:
        return ManetteGamepad::StandardGamepadButton::A;
    case BTN_B:
        return ManetteGamepad::StandardGamepadButton::B;
    case BTN_X:
        return ManetteGamepad::StandardGamepadButton::X;
    case BTN_Y:
        return ManetteGamepad::StandardGamepadButton::Y;
    case BTN_TL:
        return ManetteGamepad::StandardGamepadButton::LeftShoulder;
    case BTN_TR:
        return ManetteGamepad::StandardGamepadButton::RightShoulder;
    case BTN_TL2:
        return ManetteGamepad::StandardGamepadButton::LeftTrigger;
    case BTN_TR2:
        return ManetteGamepad::StandardGamepadButton::RightTrigger;
    case BTN_SELECT:
        return ManetteGamepad::StandardGamepadButton::Select;
    case BTN_START:
        return ManetteGamepad::StandardGamepadButton::Start;
    case BTN_THUMBL:
        return ManetteGamepad::StandardGamepadButton::LeftStick;
    case BTN_THUMBR:
        return ManetteGamepad::StandardGamepadButton::RightStick;
    case BTN_DPAD_UP:
        return ManetteGamepad::StandardGamepadButton::DPadUp;
    case BTN_DPAD_DOWN:
        return ManetteGamepad::StandardGamepadButton::DPadDown;
    case BTN_DPAD_LEFT:
        return ManetteGamepad::StandardGamepadButton::DPadLeft;
    case BTN_DPAD_RIGHT:
        return ManetteGamepad::StandardGamepadButton::DPadRight;
    case BTN_MODE:
        return ManetteGamepad::StandardGamepadButton::Mode;
    default:
        break;
    }
    return ManetteGamepad::StandardGamepadButton::Unknown;
}
#endif

#if MANETTE_CHECK_VERSION(1, 0, 0)
static void onButtonPressed(ManetteDevice* device, ManetteButton button, ManetteGamepad* gamepad)
{
    gamepad->buttonPressedOrReleased(device, toStandardGamepadButton(button), true);
}

static void onButtonReleased(ManetteDevice* device, ManetteButton button, ManetteGamepad* gamepad)
{
    gamepad->buttonPressedOrReleased(device, toStandardGamepadButton(button), false);
}
#else
static void onButtonPressEvent(ManetteDevice* device, ManetteEvent* event, ManetteGamepad* gamepad)
{
    uint16_t button;
    if (!manette_event_get_button(event, &button))
        return;

    gamepad->buttonPressedOrReleased(device, toStandardGamepadButton(button), true);
}

static void onButtonReleaseEvent(ManetteDevice* device, ManetteEvent* event, ManetteGamepad* gamepad)
{
    uint16_t button;
    if (!manette_event_get_button(event, &button))
        return;

    gamepad->buttonPressedOrReleased(device, toStandardGamepadButton(button), false);
}
#endif

ManetteGamepad::ManetteGamepad(ManetteDevice* device, unsigned index)
    : PlatformGamepad(index)
    , m_device(device)
    , m_effectDelayTimer(RunLoop::current(), this, &ManetteGamepad::effectDelayTimerFired)
    , m_effectDurationTimer(RunLoop::current(), this, &ManetteGamepad::effectDurationTimerFired)
{
    ASSERT(index < 4);

    m_connectTime = m_lastUpdateTime = MonotonicTime::now();

    m_id = String::fromUTF8(manette_device_get_name(m_device.get()));
    m_mapping = String::fromUTF8("standard");

    m_axisValues.resize(static_cast<size_t>(StandardGamepadAxis::Count));
    for (auto& value : m_axisValues)
        value.setValue(0.0);

    m_buttonValues.resize(static_cast<size_t>(StandardGamepadButton::Count));
    for (auto& value : m_buttonValues)
        value.setValue(0.0);

    if (manette_device_has_rumble(m_device.get()))
        m_supportedEffectTypes.add(GamepadHapticEffectType::DualRumble);

#if MANETTE_CHECK_VERSION(1, 0, 0)
    g_signal_connect(device, "button-pressed", G_CALLBACK(onButtonPressed), this);
    g_signal_connect(device, "button-released", G_CALLBACK(onButtonReleased), this);
    g_signal_connect(device, "absolute-axis-changed", G_CALLBACK(onAbsoluteAxisChanged), this);
#else
    g_signal_connect(device, "button-press-event", G_CALLBACK(onButtonPressEvent), this);
    g_signal_connect(device, "button-release-event", G_CALLBACK(onButtonReleaseEvent), this);
    g_signal_connect(device, "absolute-axis-event", G_CALLBACK(onAbsoluteAxisEvent), this);
#endif
}

ManetteGamepad::~ManetteGamepad()
{
    g_signal_handlers_disconnect_by_data(m_device.get(), this);
}

void ManetteGamepad::buttonPressedOrReleased(ManetteDevice*, StandardGamepadButton button, bool pressed)
{
    if (button == StandardGamepadButton::Unknown)
        return;

    m_lastUpdateTime = MonotonicTime::now();
    m_buttonValues[static_cast<int>(button)].setValue(pressed ? 1.0 : 0.0);

    ManetteGamepadProvider::singleton().gamepadHadInput(*this, pressed ? ManetteGamepadProvider::ShouldMakeGamepadsVisible::Yes : ManetteGamepadProvider::ShouldMakeGamepadsVisible::No);
}

void ManetteGamepad::absoluteAxisChanged(ManetteDevice*, StandardGamepadAxis axis, double value)
{
    if (axis == StandardGamepadAxis::Unknown)
        return;

    m_lastUpdateTime = MonotonicTime::now();
    m_axisValues[static_cast<int>(axis)].setValue(value);

    ManetteGamepadProvider::singleton().gamepadHadInput(*this, ManetteGamepadProvider::ShouldMakeGamepadsVisible::Yes);
}

void ManetteGamepad::analogButtonChanged(ManetteDevice*, StandardGamepadButton button, double value)
{
    if (button == StandardGamepadButton::Unknown)
        return;

    m_lastUpdateTime = MonotonicTime::now();
    m_buttonValues[static_cast<int>(button)].setValue(clampTo(value, 0.0, 1.0));

    ManetteGamepadProvider::singleton().gamepadHadInput(*this, ManetteGamepadProvider::ShouldMakeGamepadsVisible::Yes);
}

void ManetteGamepad::playEffect(GamepadHapticEffectType type, const GamepadEffectParameters& parameters, CompletionHandler<void(bool)>&& completionHandler)
{
    if (!m_supportedEffectTypes.contains(type))
        return completionHandler(false);

    if (m_effectCompletionHandler)
        stopEffects({ });

    m_effectCompletionHandler = WTFMove(completionHandler);
    if (parameters.startDelay) {
        m_pendingEffectParameters = parameters;
        m_effectDelayTimer.startOneShot(Seconds::fromMilliseconds(parameters.startDelay));
        return;
    }

    startRumble(parameters);
}

void ManetteGamepad::stopEffects(CompletionHandler<void()>&& completionHandler)
{
    m_effectDelayTimer.stop();
    m_effectDurationTimer.stop();
    if (m_effectCompletionHandler)
        m_effectCompletionHandler(false);

    manette_device_rumble(m_device.get(), 0, 0, 0);

    if (completionHandler)
        completionHandler();
}

void ManetteGamepad::effectDelayTimerFired()
{
    startRumble(std::exchange(m_pendingEffectParameters, { }));
}

void ManetteGamepad::startRumble(const GamepadEffectParameters& parameters)
{
#if MANETTE_CHECK_VERSION(0, 2, 13)
    manette_device_rumble(m_device.get(), parameters.strongMagnitude, parameters.weakMagnitude, static_cast<guint>(parameters.duration));
#else
    manette_device_rumble(m_device.get(), parameters.strongMagnitude * G_MAXUINT16, parameters.weakMagnitude * G_MAXUINT16, static_cast<guint>(parameters.duration));
#endif

    if (parameters.duration)
        m_effectDurationTimer.startOneShot(Seconds::fromMilliseconds(parameters.duration));
    else
        m_effectCompletionHandler(true);
}

void ManetteGamepad::effectDurationTimerFired()
{
    m_effectCompletionHandler(true);
}

} // namespace WebCore

#endif // ENABLE(GAMEPAD) && OS(LINUX)
