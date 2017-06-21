/*
 * Copyright (C) 2017 Garmin Ltd. All Rights Reserved
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

#pragma once

#include "Editor.h"
#include "EditorState.h"
#include "TextInput.h"

#include <list>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/RefCounted.h>

struct wl_display;
struct wl_seat;
struct wl_surface;
struct wl_keyboard;
struct zwp_text_input_manager_v1;
struct zwp_text_input_v1;

struct wl_registry_listener;
struct wl_seat_listener;
struct wl_keyboard_listener;
struct zwp_text_input_v1_listener;

namespace WebCore {
class Color;
};

namespace WebKit {

class WebPageProxy;

class TextInputWayland: public TextInput {
public:
    struct KeyboardModifiers {
        bool shift { false };
        bool control { false };
        bool mod1 { false };
    };

    enum Style {
        StyleDefault,
        StyleNone,
        StyleActive,
        StyleInactive,
        StyleHighlight,
        StyleUnderline,
        StyleSelection,
        StyleIncorrect
    };

    class Client {
    public:
        virtual void handleTextInputKeySym(WebPageProxy& page, uint32_t serial, uint32_t time, uint32_t sym, bool pressed, const KeyboardModifiers& mods) = 0;
        virtual bool styleTextInput(Style s, WebCore::Color& color, bool& thick)
        {
            return s != StyleDefault && s != StyleNone;
        }
    };

    static Ref<TextInputWayland> create(WebPageProxy& page, Client* client, struct wl_display* display, struct wl_surface* surface)
    {
        return adoptRef(*new TextInputWayland(page, client, display, surface));
    }

    virtual ~TextInputWayland();

    virtual void enable(bool) override;
    virtual void editorStateChanged() override;

private:
    struct Seat {
        Seat(TextInputWayland* input, struct wl_seat* seat)
            : m_input(input), m_seat(seat)
            {}
        ~Seat();

        TextInputWayland* m_input;
        struct wl_seat* m_seat;
        String m_name { "" };
        uint32_t m_capabilities { 0 };
        struct wl_keyboard* m_keyboard { nullptr };
    };

    struct EditorState {
        uint32_t serial { 0 };
        bool isInPasswordField { false };
        String surroundingText { "" };
        int32_t selectionStart { -1 };
        int32_t selectionEnd { -1 };
        WebCore::IntRect caretRectAtStart;
    };

    TextInputWayland(WebPageProxy&, Client*, struct wl_display*, struct wl_surface*);

    Client* m_client;
    struct wl_display* m_display;
    struct wl_surface* m_surface;
    struct wl_registry* m_registry { nullptr };
    struct zwp_text_input_manager_v1* m_inputManager { nullptr };
    struct zwp_text_input_v1* m_input { nullptr };
    bool m_enabled { false };
    bool m_selectionWasNone { false };
    bool m_canCommit { false };
    bool m_hasKeyboardFocus { false };
    bool m_isActive { false };
    bool m_panelShown { false };
    bool m_isEditable { false };
    uint32_t m_lastSerial { 0 };

    EditorState m_editorState;

    std::list<Seat*> m_seats;
    Seat* m_targetSeat { nullptr };

    int64_t m_preeditCursorPosition;
    Vector<WebCore::CompositionUnderline> m_preeditStyle;
    String m_commitString;
    int64_t m_commitCursorPosition;
    int64_t m_commitAnchorPosition;
    int32_t m_deleteIndex;
    uint32_t m_deleteLength;

    uint32_t m_shiftMask { 0 };
    uint32_t m_controlMask { 0 };
    uint32_t m_mod1Mask { 0 };

    void destroyInput();

    void activate();
    void deactivate();
    void updatePanelVisibility();

    void resetInputState();

    void endEdit();
    void commitEdit(int64_t);
    void cancelEdit();
    void resetEditState();
    void commitString(uint32_t, const char*);

    int64_t getPreeditCursorPosition() const;

    void handleKeyEvent(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const;

    static const int64_t invalidCursorPosition;

    static const struct wl_registry_listener s_registryListener;
    static const struct wl_seat_listener s_seatListener;
    static const struct wl_keyboard_listener s_keyboardListener;
    static const struct zwp_text_input_v1_listener s_textInputListener;
};

};

