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
#include "config.h"

#include <time.h>

#include <cstring>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "Editor.h"
#include "text-input-unstable-v1-client-protocol.h"
#include "TextInputWayland.h"
#include "WebPageProxy.h"

namespace WebKit {

static uint32_t keysym_modifiers_get_mask(struct wl_array* modifiers_map, const char* name)
{
	int index = 0;
    const char* data = static_cast<const char*>(modifiers_map->data);
	const char* p = data;

	while (p < data + modifiers_map->size) {
		if (strcmp(p, name) == 0)
			return 1 << index;

		index++;
		p += strlen(p) + 1;
	}

	return 0;
}

const int64_t TextInputWayland::invalidCursorPosition = std::numeric_limits<int64_t>::lowest();

const struct wl_registry_listener TextInputWayland::s_registryListener = {
    // global
    [] (void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        if (strcmp(interface, "zwp_text_input_manager_v1") == 0 && !t.m_inputManager) {
            t.m_inputManager = static_cast<struct zwp_text_input_manager_v1*>(wl_registry_bind(
                        registry, name, &zwp_text_input_manager_v1_interface, version));
            t.m_input = zwp_text_input_manager_v1_create_text_input(t.m_inputManager);
            zwp_text_input_v1_add_listener(t.m_input, &s_textInputListener, &t);

            wl_display_roundtrip(t.m_display);
        } else if (strcmp(interface, "wl_seat") == 0) {
            auto* seat = static_cast<struct wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
            auto* s = new TextInputWayland::Seat(&t, seat);
            t.m_seats.push_back(s);
            wl_seat_add_listener(seat, &s_seatListener, s);

            if (!t.m_targetSeat)
                t.m_targetSeat = s;

            wl_display_roundtrip(t.m_display);
        }
    },
    // global_remove
    [] (void* data, struct wl_registry*, uint32_t name)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        if (t.m_inputManager && wl_proxy_get_id(reinterpret_cast<struct wl_proxy*>(t.m_inputManager)) == name) {
            t.destroyInput();
        }

        for(auto* s : t.m_seats) {
            if (wl_proxy_get_id(reinterpret_cast<struct wl_proxy*>(s->m_seat)) == name) {
                t.m_seats.remove(s);
                if (t.m_targetSeat == s) {
                    t.m_targetSeat = nullptr;
                }
                delete s;
            }
        }
    },
};

const struct wl_seat_listener TextInputWayland::s_seatListener = {
    // capabilities
    [] (void* data, struct wl_seat*, uint32_t capabilities)
    {
        auto* seat = static_cast<Seat*>(data);

        if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
            if (!seat->m_keyboard) {
                seat->m_keyboard = wl_seat_get_keyboard(seat->m_seat);
                wl_keyboard_add_listener(seat->m_keyboard, &s_keyboardListener, seat);
            }
        } else if (seat->m_keyboard) {
            wl_keyboard_destroy(seat->m_keyboard);
            seat->m_keyboard = nullptr;
        }
    },
    // name
    [] (void* data, struct wl_seat*, const char* name)
    {
        auto* seat = static_cast<Seat*>(data);
        seat->m_name = name;
    }
};

const struct wl_keyboard_listener TextInputWayland::s_keyboardListener = {
    // keymap
    [] (void*, struct wl_keyboard*, uint32_t, int, uint32_t)
    { },
    // enter
    [] (void* data, struct wl_keyboard*, uint32_t, struct wl_surface* surface, struct wl_array*)
    {
        auto* seat = static_cast<Seat*>(data);
        auto& t = *seat->m_input;
        if (surface == t.m_surface) {
            t.m_hasKeyboardFocus = true;
            t.updatePanelVisibility();
            t.activate();
        }
    },
    // leave
    [] (void* data, struct wl_keyboard*, uint32_t, struct wl_surface* surface)
    {
        auto* seat = static_cast<Seat*>(data);
        auto& t = *seat->m_input;
        if (surface == t.m_surface) {
            t.m_hasKeyboardFocus = false;
            t.updatePanelVisibility();
        }
    },
    // key
    [] (void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t)
    { },
    // modifiers
    [] (void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
    { },
    // repeat_info
    [] (void*, struct wl_keyboard*, int32_t, int32_t)
    { },
};

const struct zwp_text_input_v1_listener TextInputWayland::s_textInputListener = {
    // enter
	[] (void* data, struct zwp_text_input_v1*, struct wl_surface*)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        t.m_isActive = true;
        t.m_editorState.serial++;
        t.resetEditState();
        t.resetInputState();
        t.updatePanelVisibility();
    },
	// leave
    [] (void* data, struct zwp_text_input_v1*)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        t.endEdit();
        t.m_isActive = false;
        t.updatePanelVisibility();
    },
	// modifiers_map
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, struct wl_array *map)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        t.m_shiftMask = keysym_modifiers_get_mask(map, "Shift");
        t.m_controlMask = keysym_modifiers_get_mask(map, "Control");
        t.m_mod1Mask = keysym_modifiers_get_mask(map, "Mod1");
    },
	// input_panel_state
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t state)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.m_panelShown = state ? true : false;
    },
	// preedit_string
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t serial, const char *text, const char *commit)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        if (serial == t.m_lastSerial) {
            t.m_commitString = String::fromUTF8(commit);
            t.m_pageProxy.setComposition(String::fromUTF8(text), t.m_preeditStyle, t.m_preeditCursorPosition,
                    t.m_preeditCursorPosition, 0, 0);
        }

        t.m_preeditStyle.clear();
    },
	// preedit_styling
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t index, uint32_t length, uint32_t style)
    {
        auto& t = *static_cast<TextInputWayland*>(data);

        enum Style s = StyleDefault;

        bool thick = false;
        bool apply = true;
        WebCore::Color color(0, 0, 0);

        switch(style) {
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_DEFAULT:
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_NONE:
                apply = false;
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_ACTIVE:
                s = StyleActive;
                color = WebCore::Color(0, 0, 0xff);
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INACTIVE:
                s = StyleInactive;
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_HIGHLIGHT:
                s = StyleHighlight;
                color = WebCore::Color(0xff, 0xff, 0);
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_UNDERLINE:
                s = StyleUnderline;
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_SELECTION:
                s = StyleSelection;
                break;
            case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INCORRECT:
                s = StyleIncorrect;
                color = WebCore::Color(0xff, 0, 0);
                thick = true;
                break;
        }

        if (t.m_client)
            apply = t.m_client->styleTextInput(s, color, thick);

        if (apply)
            t.m_preeditStyle.append(WebCore::CompositionUnderline(index, index + length, color, thick));
    },
	// preedit_cursor
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, int32_t index)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.m_preeditCursorPosition = index;
    },
	// commit_string
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t serial, const char *text)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.commitString(serial, text);
    },
	// cursor_position
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, int32_t index, int32_t anchor)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.m_commitCursorPosition = index;
        t.m_commitAnchorPosition = anchor;
    },
	// delete_surrounding_text
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, int32_t index, uint32_t length)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.m_deleteIndex = index;
        t.m_deleteLength = length;
    },
	// keysym
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t serial, uint32_t time, uint32_t sym, uint32_t state, uint32_t modifiers)
    {
        auto& t = *static_cast<TextInputWayland*>(data);
        t.handleKeyEvent(serial, time, sym, state, modifiers);
    },
	// language
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t serial, const char *language)
    { },
	// text_direction
    [] (void *data, struct zwp_text_input_v1 *zwp_text_input_v1, uint32_t serial, uint32_t direction)
    { },
};

TextInputWayland::TextInputWayland(WebPageProxy& page, Client* client, struct wl_display* display, struct wl_surface* surface)
    : TextInput(page)
    , m_client(client)
    , m_display(display)
    , m_surface(surface)
    , m_commitCursorPosition(invalidCursorPosition)
    , m_commitAnchorPosition(invalidCursorPosition)
{
    if (m_display && m_surface) {
        m_registry = wl_display_get_registry(m_display);
        wl_registry_add_listener(m_registry, &s_registryListener, this);
        wl_display_roundtrip(m_display);
    }
}

TextInputWayland::~TextInputWayland()
{
    destroyInput();

    if (m_registry)
        wl_registry_destroy(m_registry);

    for(auto* s : m_seats)
        delete s;
}

TextInputWayland::Seat::~Seat()
{
    if (m_seat)
        wl_seat_destroy(m_seat);

    if (m_keyboard)
        wl_keyboard_destroy(m_keyboard);
}

void TextInputWayland::enable(bool enable)
{
    deactivate();
    m_enabled = enable;
    activate();
}

void TextInputWayland::editorStateChanged()
{
    const auto& e = m_pageProxy.editorState();

    // If the content is not editable, treat it like there is no composition
    // string (even if we actually do have one). We can't commit it until the
    // editor reports that we are actually in an editable field.
    if (!e.shouldIgnoreSelectionChanges) {
        m_selectionWasNone = e.selectionIsNone;

        m_isEditable = e.isContentEditable && !e.selectionIsNone;

        if (!m_isEditable) {
            m_canCommit = false;
            updatePanelVisibility();
            deactivate();
            return;
        }
    }

    m_canCommit = e.hasComposition;

    // If this state change was initiated by a composition action we sent,
    // ignore it.
    if (!m_commitString.isEmpty() && e.isClientInitiated)
        return;

    endEdit();

    if (!e.shouldIgnoreSelectionChanges) {
        if (m_selectionWasNone || !m_isActive || !e.hasComposition) {
            m_editorState.surroundingText = e.surroundingText;
            m_editorState.selectionStart = e.selectionStart;
            m_editorState.selectionEnd = e.selectionEnd;

            if (!e.isMissingPostLayoutData)
                m_editorState.caretRectAtStart = e.postLayoutData().caretRectAtStart;
        }

        m_selectionWasNone = false;
    }

    m_editorState.isInPasswordField = e.isInPasswordField;

    m_editorState.serial++;

    resetEditState();
    resetInputState();

    updatePanelVisibility();
    activate();
}

void TextInputWayland::destroyInput()
{
    if (m_input)
        zwp_text_input_v1_destroy(m_input);
    m_input = nullptr;

    if (m_inputManager)
        zwp_text_input_manager_v1_destroy(m_inputManager);
    m_inputManager = nullptr;
}

void TextInputWayland::activate()
{
    if (!m_input || m_isActive || !m_targetSeat || !m_hasKeyboardFocus || !m_enabled || !m_isEditable)
        return;

    zwp_text_input_v1_activate(m_input, m_targetSeat->m_seat, m_surface);
}

void TextInputWayland::deactivate()
{
    if (!m_input || !m_isActive || !m_targetSeat)
        return;

    zwp_text_input_v1_deactivate(m_input, m_targetSeat->m_seat);
}

void TextInputWayland::updatePanelVisibility()
{
    if (!m_input)
        return;

    bool show = m_isActive && m_enabled && m_hasKeyboardFocus && m_isEditable;

    if (show != m_panelShown) {
        if (show)
            zwp_text_input_v1_show_input_panel(m_input);
        else
            zwp_text_input_v1_hide_input_panel(m_input);

        m_panelShown = show;
    }
}

void TextInputWayland::resetInputState()
{
    if (!m_input || m_lastSerial == m_editorState.serial || !m_isActive)
        return;

    m_lastSerial = m_editorState.serial;

    zwp_text_input_v1_reset(m_input);

    if (m_editorState.selectionStart > 0 && m_editorState.selectionEnd > 0)
        zwp_text_input_v1_set_surrounding_text(m_input, m_editorState.surroundingText.utf8().data(), m_editorState.selectionStart, m_editorState.selectionEnd);

    uint32_t hint = ZWP_TEXT_INPUT_V1_CONTENT_HINT_NONE;
    uint32_t purpose = ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_NORMAL;

    if (m_editorState.isInPasswordField) {
        hint = ZWP_TEXT_INPUT_V1_CONTENT_HINT_PASSWORD;
        purpose = ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD;
    }

    zwp_text_input_v1_set_content_type(m_input, hint, purpose);

    const auto& rect = m_editorState.caretRectAtStart;
    zwp_text_input_v1_set_cursor_rectangle(m_input, rect.x(), rect.y(), rect.width(), rect.height());

    zwp_text_input_v1_commit_state(m_input, m_editorState.serial);
}

void TextInputWayland::endEdit()
{
    if (!m_commitString.isEmpty())
        commitEdit(getPreeditCursorPosition());
    else
        cancelEdit();
}

void TextInputWayland::commitEdit(int64_t position)
{
    if (m_canCommit) {
        m_pageProxy.confirmComposition(m_commitString.utf8().data(), position, 0);
        m_canCommit = false;
        m_commitString = emptyString();
    }
}

void TextInputWayland::cancelEdit()
{
    if (m_canCommit) {
        m_pageProxy.cancelComposition();
        m_canCommit = false;
        m_commitString = emptyString();
    }
}

void TextInputWayland::commitString(uint32_t serial, const char* text)
{
    if (serial == m_lastSerial) {
        int64_t position = -1;

        if (m_editorState.selectionStart >= 0 && m_commitCursorPosition != invalidCursorPosition) {
            // Positive cursor position values are relative to the end of the
            // commit string, negative values are relative to the beginning
            position = m_editorState.selectionStart + m_commitCursorPosition;
            if (m_commitCursorPosition >= 0)
                position += strlen(text);
        }

        m_commitString = String::fromUTF8(text);
        commitEdit(position);

        if (position >= 0) {
            // Update these to the newly expected values.
            m_editorState.selectionStart = position;
            m_editorState.selectionEnd = position;
        }

        if (m_editorState.selectionStart >= 0 && m_deleteLength > 0) {
            int64_t start = m_editorState.selectionStart + m_deleteIndex;
            int64_t end = start + m_deleteLength;

            m_pageProxy.deleteSurroundingText(start, end);
        }

        resetEditState();
    }

    m_commitCursorPosition = invalidCursorPosition;
    m_commitAnchorPosition = invalidCursorPosition;
    m_deleteIndex = 0;
    m_deleteLength = 0;
}

void TextInputWayland::resetEditState()
{
    m_preeditCursorPosition = -1;
    m_preeditStyle.clear();
    m_commitString = emptyString();
    m_commitCursorPosition = invalidCursorPosition;
    m_commitAnchorPosition = invalidCursorPosition;
    m_deleteIndex = 0;
    m_deleteLength = 0;
}

int64_t TextInputWayland::getPreeditCursorPosition() const
{
    if (m_preeditCursorPosition >= 0 && m_editorState.selectionStart >= 0)
        return m_preeditCursorPosition + m_editorState.selectionStart;
    return -1;
}

void TextInputWayland::handleKeyEvent(uint32_t serial, uint32_t time, uint32_t sym, uint32_t state, uint32_t modifiers) const
{
    if (!m_client)
        return;

    KeyboardModifiers mods;

    if (modifiers & m_shiftMask)
        mods.shift = true;

    if (modifiers & m_controlMask)
        mods.control = true;

    if (modifiers & m_mod1Mask)
        mods.mod1 = true;

    m_client->handleTextInputKeySym(m_pageProxy, serial, time, sym, state == WL_KEYBOARD_KEY_STATE_PRESSED, mods);
}

}; // namespace WebKit
